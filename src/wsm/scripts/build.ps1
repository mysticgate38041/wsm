param(
    [string]$Ndk = $env:ANDROID_NDK_HOME,
    [ValidateRange(1, 64)][int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Assert-RelroAlignment {
    param([string[]]$ProgramHeaders, [string]$Abi)
    foreach ($segment in @($ProgramHeaders | Where-Object { $_ -match '^\s*GNU_RELRO\s' })) {
        $fields = $segment.Trim() -split '\s+'
        if ($fields.Count -lt 8 -or $fields[2] -notmatch '^0x[0-9a-fA-F]+$' -or $fields[5] -notmatch '^0x[0-9a-fA-F]+$') {
            throw "Cannot parse GNU_RELRO bounds: $Abi"
        }
        $address = [Convert]::ToUInt64($fields[2].Substring(2), 16)
        $memorySize = [Convert]::ToUInt64($fields[5].Substring(2), 16)
        if (($address + $memorySize) % 16384 -ne 0) {
            throw "GNU_RELRO end is not 16 KiB aligned: $Abi"
        }
    }
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($Ndk)) {
    throw 'Provide -Ndk <Android NDK directory> or ANDROID_NDK_HOME. Use NDK r27+.'
}
$ndkRoot = (Resolve-Path -LiteralPath $Ndk).Path
$ndkBuild = Join-Path $ndkRoot 'ndk-build.cmd'
$readElf = Join-Path $ndkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
foreach ($requiredTool in @($ndkBuild, $readElf)) {
    if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) { throw "Missing tool: $requiredTool" }
}
$properties = Get-Content -Raw -LiteralPath (Join-Path $ndkRoot 'source.properties')
if ($properties -notmatch 'Pkg\.Revision\s*=\s*(\d+)\.') { throw 'Cannot read NDK revision.' }
if ([int]$Matches[1] -lt 27) { throw 'Use NDK r27+ for this project.' }
$header = Join-Path $projectRoot 'jni/zygisk.hpp'
$expectedHeaderHash = 'FC6523882C0F8659B4FCD1E04FFB07D3A948090E5A8C08817AF6E2B506A078D0'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $header).Hash -ne $expectedHeaderHash) {
    throw 'zygisk.hpp changed; restore the pinned header before building.'
}
$buildRoot = Join-Path $projectRoot 'build'
$libsRoot = Join-Path $buildRoot 'libs'
$objRoot = Join-Path $buildRoot 'obj'
& $ndkBuild '-C' $projectRoot "-j$Jobs" "NDK_OUT=$objRoot" "NDK_LIBS_OUT=$libsRoot"
if ($LASTEXITCODE -ne 0) { throw "ndk-build failed with exit $LASTEXITCODE" }

$machines = [ordered]@{
    'x86_64'    = 'Advanced Micro Devices X86-64'
    'arm64-v8a' = 'AArch64'
}
$loaders = [ordered]@{}
$engines = [ordered]@{}
foreach ($abi in $machines.Keys) {
    $loader = Join-Path $libsRoot "$abi/libwsm_loader.so"
    $engine = Join-Path $libsRoot "$abi/libwsm_engine.so"
    foreach ($library in @($loader, $engine)) {
        if (-not (Test-Path -LiteralPath $library -PathType Leaf)) { throw "Missing binary: $library" }
    }
    $elfLoader = (& $readElf '--file-header' $loader) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $elfLoader -notmatch $machines[$abi]) { throw "Wrong ELF architecture (loader): $abi" }
    $elfEngine = (& $readElf '--file-header' $engine) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $elfEngine -notmatch $machines[$abi]) { throw "Wrong ELF architecture (engine): $abi" }
    $programHeaders = @(& $readElf '--program-headers' '--wide' $loader)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect LOAD alignment: $abi" }
    $loadSegments = @($programHeaders | Where-Object { $_ -match '^\s*LOAD\s' })
    if ($loadSegments.Count -eq 0) { throw "No ELF LOAD segments: $abi" }
    foreach ($segment in $loadSegments) {
        if ($segment -notmatch '(0x[0-9a-fA-F]+)\s*$') { throw "Cannot parse LOAD alignment: $abi" }
        $alignment = [Convert]::ToUInt64($Matches[1].Substring(2), 16)
        if ($alignment -lt 16384) { throw "ELF alignment below 16 KiB: $abi" }
    }
    Assert-RelroAlignment -ProgramHeaders $programHeaders -Abi $abi
    $symbols = (& $readElf '--dyn-syms' $loader) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $symbols -notmatch '\bGLOBAL\s+DEFAULT\s+\d+\s+zygisk_module_entry\b') {
        throw "Zygisk entry missing in loader: $abi"
    }
    $symbolsEngine = (& $readElf '--dyn-syms' $engine) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $symbolsEngine -notmatch '\bGLOBAL\s+DEFAULT\s+\d+\s+JNI_OnLoad\b') {
        throw "JNI_OnLoad missing in engine: $abi"
    }
    if ($symbolsEngine -notmatch '\bGLOBAL\s+DEFAULT\s+\d+\s+wsm_engine_main\b') {
        throw "wsm_engine_main missing in engine: $abi"
    }
    $dynamic = (& $readElf '--dynamic' $loader) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect dependencies: $abi" }
    if ($dynamic -match 'libc\+\+_shared\.so|\(TEXTREL\)') { throw "Unexpected STL dependency or text relocation: $abi" }
    $loaders[$abi] = $loader
    $engines[$abi] = $engine
}

$outputRoot = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$zipPath = Join-Path $outputRoot 'wsm-v0.1.0-poc.zip'
$tempZipPath = Join-Path $outputRoot ('package-' + [Guid]::NewGuid().ToString('N') + '.zip')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($tempZipPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    $templateRoot = Join-Path $projectRoot 'module-template'
    foreach ($file in Get-ChildItem -LiteralPath $templateRoot -File -Recurse) {
        $entryName = $file.FullName.Substring($templateRoot.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entryName) | Out-Null
    }
    foreach ($abi in $loaders.Keys) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $loaders[$abi], "zygisk/$abi.so") | Out-Null
    }
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $engines['arm64-v8a'], 'engine/arm64-v8a.so') | Out-Null
} finally { $archive.Dispose() }
Move-Item -LiteralPath $tempZipPath -Destination $zipPath -Force
Write-Host "WSM loader/engine built and verified."
Write-Host "Installable module ZIP: $zipPath"
