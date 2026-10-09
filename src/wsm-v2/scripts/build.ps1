param(
    [string]$Ndk = $(if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } else { 'D:\Android\ndk\android-ndk-r27c' }),
    [string]$Jdk = "$env:LOCALAPPDATA\Temp\gt_cheat_analysis\luaj\jdk-17.0.20.1+1",
    [string]$Sdk = 'C:\wsmbuild\sdk',
    [string]$Python = 'C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [ValidateRange(1, 16)][int]$Jobs = 2,
    [ValidateSet('ndk-build', 'CMake')][string]$NativeBuild = 'ndk-build',
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [switch]$CatalogSnapshot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ndkRoot = (Resolve-Path -LiteralPath $Ndk).Path
$javaRoot = (Resolve-Path -LiteralPath $Jdk).Path
$readelf = Join-Path $ndkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
$ndkBuild = Join-Path $ndkRoot 'ndk-build.cmd'
$androidJar = Join-Path $Sdk 'platforms/android-34/android.jar'
$d8Jar = Join-Path $Sdk 'build-tools/34.0.0/lib/d8.jar'
foreach ($tool in @($readelf,$ndkBuild,$androidJar,$d8Jar,$Python,(Join-Path $javaRoot 'bin/javac.exe'),(Join-Path $javaRoot 'bin/java.exe'))) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required tool missing: $tool" }
}
if ((Get-Content -Raw -LiteralPath (Join-Path $ndkRoot 'source.properties')) -notmatch 'Pkg\.Revision\s*=\s*27\.2\.12479018(?:\s|$)') { throw 'Pinned NDK 27.2.12479018 required.' }
if ((Get-FileHash -LiteralPath (Join-Path $project 'jni/zygisk.hpp')).Hash -ne 'FC6523882C0F8659B4FCD1E04FFB07D3A948090E5A8C08817AF6E2B506A078D0') { throw 'Pinned Zygisk header differs.' }
$cmakeExe = (Get-Command $CMake -ErrorAction Stop).Source
$ninjaExe = (Get-Command $Ninja -ErrorAction Stop).Source
& $Python '-B' '-X' 'utf8' (Join-Path $project 'scripts/generate_build_config.py')
if ($LASTEXITCODE) { throw 'Build manifest generation failed.' }
$manifest = Get-Content -Raw -LiteralPath (Join-Path $project 'scripts/build_manifest.json') | ConvertFrom-Json
$catalogArgs = @()
if ($CatalogSnapshot) { $catalogArgs += '--snapshot' }
& $Python '-B' '-X' 'utf8' (Join-Path $project 'scripts/build_feature_catalog.py') @catalogArgs
if ($LASTEXITCODE) { throw '47-feature catalog generation failed.' }
& $Python '-B' '-X' 'utf8' (Join-Path $project 'scripts/package_release.py') '--checkpoint'
if ($LASTEXITCODE) { throw 'Source checkpoint failed.' }
if ($NativeBuild -eq 'ndk-build') {
    & $ndkBuild '-C' $project "-j$Jobs" 'APP_ABI=x86_64 arm64-v8a' "NDK_OUT=$project/build/obj" "NDK_LIBS_OUT=$project/build/libs"
    if ($LASTEXITCODE) { throw 'Native ndk-build failed.' }
}
# Both backends use the same dependency-aware fixture graph. CMake also builds
# libraries when selected; ndk-build remains its own independent library gate.
foreach ($abi in @('x86_64','arm64-v8a')) {
    $cmakeOut = Join-Path $project "build/cmake-$NativeBuild-$abi"
    $libraries = if ($NativeBuild -eq 'CMake') { 'ON' } else { 'OFF' }
    & $cmakeExe '-S' (Join-Path $project 'jni') '-B' $cmakeOut '-G' 'Ninja' "-DCMAKE_MAKE_PROGRAM=$ninjaExe" "-DCMAKE_TOOLCHAIN_FILE=$ndkRoot/build/cmake/android.toolchain.cmake" "-DANDROID_ABI=$abi" '-DANDROID_PLATFORM=android-26' '-DANDROID_STL=c++_static' '-DCMAKE_BUILD_TYPE=Release' "-DWSM_BUILD_LIBRARIES=$libraries" '-DWSM_BUILD_ANDROID_TESTS=ON'
    if ($LASTEXITCODE) { throw "CMake configure failed: $abi" }
    & $cmakeExe '--build' $cmakeOut '--parallel' "$Jobs"
    if ($LASTEXITCODE) { throw "CMake build failed: $abi" }
    if ($NativeBuild -eq 'CMake') {
        $abiOut = Join-Path $project "build/libs/$abi"
        New-Item -ItemType Directory -Force -Path $abiOut | Out-Null
        foreach ($library in @('libwsm_loader.so','libwsm_engine.so') + $(if ($abi -eq 'arm64-v8a') { @('libwsm_arm64.so') } else { @() })) {
            Copy-Item -LiteralPath (Join-Path $cmakeOut "libs/$abi/$library") -Destination (Join-Path $abiOut $library) -Force
        }
    }
    $fixtureOut = Join-Path $project "build/fixtures/$abi"
    New-Item -ItemType Directory -Force -Path $fixtureOut | Out-Null
    foreach ($property in $manifest.fixtures.PSObject.Properties) {
        $name = "wsm-$($property.Name)-test"
        Copy-Item -LiteralPath (Join-Path $cmakeOut "fixtures/$abi/$name") -Destination (Join-Path $fixtureOut $name) -Force
    }
}
$binaries = @(
    @('x86_64/libwsm_loader.so','X86-64','zygisk_module_entry'),
    @('arm64-v8a/libwsm_loader.so','AArch64','zygisk_module_entry'),
    @('x86_64/libwsm_engine.so','X86-64','JNI_OnLoad'),
    @('arm64-v8a/libwsm_engine.so','AArch64','JNI_OnLoad'),
    @('arm64-v8a/libwsm_arm64.so','AArch64','h64_magic')
)
foreach ($binary in $binaries) {
    $file = Join-Path $project "build/libs/$($binary[0])"
    $header = (& $readelf '--file-header' $file) -join "`n"
    if ($LASTEXITCODE -or $header -notmatch $binary[1]) { throw "Wrong ELF machine: $file" }
    $headers = @(& $readelf '--program-headers' '--wide' $file)
    $loads = @($headers | Where-Object { $_ -match '^\s*LOAD\s' })
    if ($LASTEXITCODE -or $loads.Count -eq 0) { throw "No LOAD segments: $file" }
    foreach ($line in $loads) {
        if ($line -notmatch '(0x[0-9a-fA-F]+)\s*$' -or [Convert]::ToUInt64($Matches[1].Substring(2),16) -lt 16384) { throw "LOAD alignment below 16 KiB: $file" }
    }
    foreach ($line in @($headers | Where-Object { $_ -match '^\s*GNU_RELRO\s' })) {
        $fields = $line.Trim() -split '\s+'
        if (([Convert]::ToUInt64($fields[2].Substring(2),16) + [Convert]::ToUInt64($fields[5].Substring(2),16)) % 16384) { throw "Unaligned RELRO: $file" }
    }
    $symbols = (& $readelf '--dyn-syms' $file) -join "`n"
    if ($LASTEXITCODE -or $symbols -notmatch ("\bGLOBAL\s+DEFAULT\s+\d+\s+"+$binary[2]+"\b")) { throw "Missing entry: $file" }
    $dynamic = (& $readelf '--dynamic' $file) -join "`n"
    if ($LASTEXITCODE -or $dynamic -match 'libc\+\+_shared\.so|\(TEXTREL\)') { throw "Unexpected runtime dependency: $file" }
}
& $Python '-B' '-X' 'utf8' (Join-Path $project 'scripts/package_release.py') '--check-native'
if ($LASTEXITCODE) { throw 'Native exported ABI validation failed.' }
& $Python '-B' '-X' 'utf8' (Join-Path $project 'scripts/build_menu.py') '--jdk' $javaRoot '--sdk' $Sdk
if ($LASTEXITCODE) { throw 'Menu build or Java regression suite failed.' }
& $Python '-B' '-X' 'utf8' '-m' 'unittest' 'discover' '-s' (Join-Path $project 'tests') '-p' 'test_build.py' '-v'
if ($LASTEXITCODE) { throw 'Build infrastructure tests failed.' }
& $Python '-B' '-X' 'utf8' (Join-Path $project 'scripts/package_release.py') '--record-build'
if ($LASTEXITCODE) { throw 'Build receipt failed.' }
& $Python '-B' '-X' 'utf8' (Join-Path $project 'scripts/package_release.py')
if ($LASTEXITCODE) { throw 'Release packaging failed.' }
$priorRequireRelease = $env:WSM_REQUIRE_RELEASE
try {
    $env:WSM_REQUIRE_RELEASE = '1'
    & $Python '-B' '-X' 'utf8' '-m' 'unittest' 'discover' '-s' (Join-Path $project 'tests') '-p' 'test_release.py' '-v'
    if ($LASTEXITCODE) { throw 'Release regression tests failed.' }
} finally {
    $env:WSM_REQUIRE_RELEASE = $priorRequireRelease
}
Write-Host "WSM $($manifest.release.version): both ABIs, native fixtures, Java/DEX and package verified. Gameplay qualification remains separate."
