"""Deterministic module packaging; refuse stale or incomplete build inputs.

ZIP hashes detect accidental changes; they are not a signing trust boundary.
ELF exports are checked from the binaries when recording and verifying a package.
Android alignment/dependencies are also verified by build.ps1.
"""
from pathlib import Path
import hashlib
import json
import posixpath
import re
import stat
import struct
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
BUILD_MANIFEST = json.loads((ROOT / "scripts/build_manifest.json").read_text(encoding="utf-8"))
STAMP = BUILD_MANIFEST["release"]["stamp"]
MODULE_VERSION = BUILD_MANIFEST["release"]["version"]
MODULE_VERSION_CODE = BUILD_MANIFEST["release"]["versionCode"]
NDK_REVISION = "27.2.12479018"
TARGET = {"package": "com.kakaogames.gdts", "version": "3.54.0", "versionCode": 423}
QUALIFICATION = "INCOMPLETE_47_FEATURE_SCOPE"
FEATURE_IDS = "god hp stam mana poise cd ult immune ohk dmg crit critdmg dura gbreak ammo parry aspd reach gold gem items craft unlockeq upg weight loot exp sp skills mastery rep speed noclip fly fall quest lootesp enemyesp freecam fov dumb aggro freeze onehp drop steal timescale".split()
ENTRIES = {
    "zygisk/x86_64.so": "build/libs/x86_64/libwsm_loader.so",
    "zygisk/arm64-v8a.so": "build/libs/arm64-v8a/libwsm_loader.so",
    "engine/x86_64.so": "build/libs/x86_64/libwsm_engine.so",
    "engine/arm64-v8a.so": "build/libs/arm64-v8a/libwsm_engine.so",
    "payload/arm64-v8a.so": "build/libs/arm64-v8a/libwsm_arm64.so",
    "dex/wsm_menu.dex": "menu/wsm_menu.dex",
}
# Public APIs are source entrypoints, never symbols from the static C++ archives.
ELF_EXPORTS = {
    "zygisk/x86_64.so": {"zygisk_module_entry"},
    "zygisk/arm64-v8a.so": {"zygisk_module_entry"},
    "engine/x86_64.so": {"JNI_OnLoad"},
    "engine/arm64-v8a.so": {"JNI_OnLoad"},
    "payload/arm64-v8a.so": {"h64_magic", "h64_victim"},
}
TEMPLATE_ENTRIES = {
    "module.prop", "customize.sh", "skip_mount",
    "META-INF/com/google/android/update-binary", "META-INF/com/google/android/updater-script",
}
REQUIRED_SOURCES = {
    "scripts/build_manifest.json", "scripts/generate_build_config.py", "scripts/build_menu.py",
    "jni/build_sources.mk", "jni/build_sources.cmake", "jni/wsm_version.h",
    "jni/runtime_snapshot.h", "jni/runtime_status.h", "tests/status_test.cpp",
    "jni/bootstrap_progress.h", "tests/bootstrap_test.cpp",
    "jni/Android.mk", "jni/Application.mk", "jni/CMakeLists.txt", "jni/module.cpp",
    "jni/engine.cpp", "jni/il2cpp_resolver.cpp", "jni/aob_scanner.cpp",
    "jni/hybrid_resolver.cpp", "jni/feature_flags.cpp", "jni/dispatcher.cpp",
    "jni/payload_worker.cpp", "jni/trampoline_pool.cpp", "jni/zygisk.hpp",
    "jni/wsm_protocol.h", "jni/wsm_restore.h", "tests/restore_test.cpp",
    "payload/h64.cpp", "menu/WsmMenu.java", "menu/ControlState.java",
    "menu/FeatureCatalog.java", "scripts/build.ps1", "scripts/package_release.py",
    "scripts/build_feature_catalog.py", "features/feature_catalog.json",
    "module-template/module.prop", "module-template/customize.sh",
    "module-template/skip_mount", "module-template/META-INF/com/google/android/update-binary",
    "module-template/META-INF/com/google/android/updater-script",
}
SOURCE_SUFFIXES = {".cpp", ".c", ".h", ".hpp", ".inc", ".s", ".java", ".mk",
                   ".ps1", ".py", ".sh", ".json", ".xml", ".cmake"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def elf_exports(data, name):
    """Check the defined dynamic ABI of our ELF64 little-endian shared libraries.

    Section tables are required in release inputs. Imports and local/hidden symbols
    are not exports. ABS version nodes are exempt only when backed by GNU verdef.
    This structural gate does not establish that a binary executes correctly.
    """
    def reject(reason):
        raise ValueError("ELF ABI " + reason + ": " + name)

    def region(offset, size):
        if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
            reject("out-of-bounds table")
        return data[offset:offset + size]

    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        reject("invalid header")
    machine = 62 if "x86_64" in name else 183
    if struct.unpack_from("<HHI", data, 16) != (3, machine, 1):
        reject("wrong shared-library header")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    ehsize, _, _, shentsize, shnum, _ = struct.unpack_from("<6H", data, 52)
    if ehsize != 64 or shentsize != 64 or shnum == 0 or shoff < 64:
        reject("missing section table")
    table = region(shoff, shnum * shentsize)
    sections = [struct.unpack_from("<IIQQQQIIQQ", table, i * 64) for i in range(shnum)]
    dynsyms = [section for section in sections if section[1] == 11]  # SHT_DYNSYM
    if len(dynsyms) != 1:
        reject("requires one dynamic symbol table")
    symbols = dynsyms[0]
    if symbols[9] != 24 or symbols[5] == 0 or symbols[5] % 24 or symbols[6] >= shnum:
        reject("invalid dynamic symbol table")
    strings_section = sections[symbols[6]]
    if strings_section[1] != 3:  # SHT_STRTAB
        reject("invalid dynamic string table")
    strings = region(strings_section[4], strings_section[5])

    def string_at(offset):
        if offset >= len(strings):
            reject("invalid symbol name offset")
        end = strings.find(b"\0", offset)
        if end < 0:
            reject("unterminated symbol name")
        try:
            return strings[offset:end].decode("utf-8")
        except UnicodeDecodeError:
            reject("invalid symbol name encoding")

    version_nodes = set()
    for section in sections:
        if section[1] != 0x6ffffffd:  # SHT_GNU_verdef
            continue
        if section[6] != symbols[6]:
            reject("invalid version string table")
        versions = region(section[4], section[5])
        offset = 0
        seen = set()
        while True:
            if offset in seen or offset + 20 > len(versions):
                reject("invalid version definition")
            seen.add(offset)
            version, _, _, count, _, aux, following = struct.unpack_from("<HHHHIII", versions, offset)
            if version != 1 or count == 0 or aux < 20 or offset + aux + 8 > len(versions):
                reject("invalid version auxiliary")
            version_nodes.add(string_at(struct.unpack_from("<I", versions, offset + aux)[0]))
            if following == 0:
                break
            if following < 20:
                reject("invalid version definition link")
            offset += following

    exports = {}
    entries = region(symbols[4], symbols[5])
    for offset in range(0, len(entries), 24):
        symbol_name, info, other, index, value, size = struct.unpack_from("<IBBHQQ", entries, offset)
        binding, kind = info >> 4, info & 15
        if index == 0 or binding not in {1, 2} or other & 3 != 0:
            continue  # UND, LOCAL, or non-DEFAULT visibility
        symbol = string_at(symbol_name)
        if index == 0xfff1 and kind in {0, 1} and value == 0 and size == 0 and symbol in version_nodes:
            continue  # SHN_ABS GNU version definition node
        if not symbol or symbol in exports:
            reject("empty or duplicate exported symbol")
        exports[symbol] = (binding, kind, index)
    allowed = ELF_EXPORTS[name]
    unexpected = set(exports) - allowed
    if unexpected:
        reject("unexpected exports " + ", ".join(sorted(unexpected)))
    missing = allowed - set(exports)
    if missing:
        reject("missing exports " + ", ".join(sorted(missing)))
    if any(exports[symbol][0] != 1 or exports[symbol][1] != 2 or
           not 0 < exports[symbol][2] < shnum for symbol in allowed):
        reject("entrypoint must be a defined GLOBAL DEFAULT function")
    return sorted(exports)


def check_native():
    return {name: elf_exports((ROOT / ENTRIES[name]).read_bytes(), name) for name in ELF_EXPORTS}


def check_source_manifest(sources):
    if not isinstance(sources, dict) or not REQUIRED_SOURCES.issubset(sources):
        raise ValueError("missing required modular source inputs")
    for name, digest in sources.items():
        if not isinstance(name, str) or "\\" in name or name.startswith("/") or ".." in name.split("/"):
            raise ValueError("invalid source path")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("invalid source hash: " + name)


def safe_zip_path(name):
    if not name or "\\" in name or name.startswith("/"):
        return False
    normalized = posixpath.normpath(name)
    return normalized == name and normalized not in {"", ".", ".."} and not normalized.startswith("../")


def verify(path):
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if not safe_zip_path(info.filename):
                raise ValueError("unsafe zip path: " + info.filename)
            mode = (info.external_attr >> 16) & 0o170000
            if mode == stat.S_IFLNK:
                raise ValueError("symlink zip entry: " + info.filename)
        if archive.testzip():
            raise ValueError("ZIP CRC failure")
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("duplicate entries")
        expected = set(ENTRIES) | TEMPLATE_ENTRIES | {"release.json", "verify.list", "features/feature_catalog.json"}
        if set(names) != expected:
            raise ValueError("missing or unexpected module entries")
        lines = [line.split(None, 1) for line in archive.read("verify.list").decode().splitlines()]
        if any(len(line) != 2 for line in lines):
            raise ValueError("malformed integrity manifest")
        manifest = {name: digest for digest, name in lines}
        if len(lines) != len(manifest) or set(manifest) != expected - {"verify.list"}:
            raise ValueError("incomplete or duplicate integrity manifest")
        for name, digest in manifest.items():
            if sha(archive.read(name)) != digest:
                raise ValueError("hash mismatch: " + name)
        release = json.loads(archive.read("release.json"))
        if (release.get("build") != STAMP or release.get("protocol") != 3 or
                release.get("minApi") != 26 or release.get("ndk") != NDK_REVISION or
                release.get("abis") != ["x86_64", "arm64-v8a"]):
            raise ValueError("release metadata mismatch")
        if release.get("target") != TARGET:
            raise ValueError("unexpected target")
        check_source_manifest(release.get("sources"))
        props = dict(line.split("=", 1) for line in archive.read("module.prop").decode().splitlines()
                     if "=" in line and not line.startswith("#"))
        if props.get("version") != MODULE_VERSION or props.get("versionCode") != str(MODULE_VERSION_CODE):
            raise ValueError("module metadata mismatch")
        catalog = json.loads(archive.read("features/feature_catalog.json"))
        ids = [feature["id"] for feature in catalog["features"]]
        if catalog.get("requested") != 47 or ids != FEATURE_IDS or catalog.get("completed") != 0:
            raise ValueError("incomplete or unqualified feature catalog")
        if any(feature.get("runtimeAcceptance") != "UNVERIFIED" for feature in catalog["features"]):
            raise ValueError("runtime qualification must be evidence-backed in a later release")
        if release.get("qualification") != QUALIFICATION:
            raise ValueError("missing qualification gate")
        if release.get("evidence", {}).get("newGameRuntime") != "UNVERIFIED_TARGET":
            raise ValueError("unproven game runtime qualification")
        for name in ENTRIES:
            data = archive.read(name)
            if name.endswith(".so"):
                if STAMP.encode() not in data:
                    raise ValueError("stale build stamp: " + name)
                machine = 62 if "x86_64" in name else 183
                if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != machine:
                    raise ValueError("wrong ELF: " + name)
                elf_exports(data, name)
            else:
                if len(data) < 112 or data[:4] != b"dex\n" or data[4:7] not in {b"035", b"037", b"038"} or data[7] != 0:
                    raise ValueError("invalid DEX magic/version")
                if struct.unpack_from("<III", data, 32) != (len(data), 112, 0x12345678):
                    raise ValueError("invalid DEX header")
                if hashlib.sha1(data[32:]).digest() != data[12:32] or zlib.adler32(data[12:]) & 0xffffffff != struct.unpack_from("<I", data, 8)[0]:
                    raise ValueError("invalid DEX integrity")
        return {"package": str(path), "sha256": sha(Path(path).read_bytes()),
                "entries": len(names), "integrity": "PASS"}


def source_hashes():
    sources = {}
    for folder in ["jni", "menu", "payload", "scripts", "tests", "features"]:
        for path in (ROOT / folder).rglob("*"):
            if path.is_file() and (path.suffix.lower() in SOURCE_SUFFIXES or path.name == "CMakeLists.txt"):
                sources[path.relative_to(ROOT).as_posix()] = sha(path.read_bytes())
    sources.update({path.relative_to(ROOT).as_posix(): sha(path.read_bytes())
                    for path in (ROOT / "module-template").rglob("*") if path.is_file()})
    check_source_manifest(sources)
    return sources


def checkpoint():
    sources = source_hashes()
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build/source-inputs.json").write_text(json.dumps(sources, sort_keys=True), encoding="utf-8")


def binary_hashes():
    return {name: sha((ROOT / path).read_bytes()) for name, path in ENTRIES.items()}


def record_build():
    sources = source_hashes()
    if sources != json.loads((ROOT / "build/source-inputs.json").read_text(encoding="utf-8")):
        raise ValueError("source changed during build; rebuild required")
    receipt = {"build": STAMP, "ndk": NDK_REVISION, "minApi": 26,
               "sources": sources, "binaries": binary_hashes(), "nativeExports": check_native()}
    (ROOT / "build/build-receipt.json").write_text(json.dumps(receipt, sort_keys=True), encoding="utf-8")


def build():
    sources = source_hashes()
    receipt = json.loads((ROOT / "build/build-receipt.json").read_text(encoding="utf-8"))
    if receipt.get("build") != STAMP or receipt.get("ndk") != NDK_REVISION or receipt.get("minApi") != 26:
        raise ValueError("build receipt metadata mismatch")
    if receipt.get("sources") != sources:
        raise ValueError("source differs from verified build receipt; rebuild required")
    if receipt.get("binaries") != binary_hashes():
        raise ValueError("binary differs from verified build receipt")
    if receipt.get("nativeExports") != check_native():
        raise ValueError("native exports differ from verified build receipt")
    files = {path.relative_to(ROOT / "module-template").as_posix(): path.read_bytes()
             for path in (ROOT / "module-template").rglob("*") if path.is_file()}
    files = {name: (data.replace(b"\r\n", b"\n") if name.endswith(".sh") or name.startswith("META-INF/") else data)
             for name, data in files.items()}
    files.update({name: (ROOT / path).read_bytes() for name, path in ENTRIES.items()})
    files["features/feature_catalog.json"] = (ROOT / "features/feature_catalog.json").read_bytes()
    for name, data in files.items():
        if name.startswith(("zygisk/", "engine/", "payload/")) and STAMP.encode() not in data:
            raise ValueError("stale build stamp: " + name)
    release = {"build": STAMP, "protocol": 3, "ndk": NDK_REVISION, "target": TARGET,
               "minApi": 26, "abis": ["x86_64", "arm64-v8a"], "sources": sources,
               "qualification": QUALIFICATION,
               "evidence": {"package": "SOURCE_VERIFIED", "newGameRuntime": "UNVERIFIED_TARGET"}}
    files["release.json"] = (json.dumps(release, sort_keys=True, indent=2) + "\n").encode()
    files["verify.list"] = "".join(sha(data) + "  " + name + "\n" for name, data in sorted(files.items())).encode()
    dest = ROOT / ("dist/" + STAMP + ".zip")
    dest.parent.mkdir(exist_ok=True)
    temporary = dest.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2026, 10, 8, 0, 0, 0))
            entry.create_system = 3
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o100755 if name.endswith(".sh") or name.endswith("update-binary") else 0o100644) << 16
            archive.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    result = verify(temporary)
    temporary.replace(dest)
    result["package"] = str(dest)
    dest.with_suffix(".zip.sha256").write_text(result["sha256"] + "  " + dest.name + "\n", encoding="ascii")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--checkpoint":
        checkpoint()
    elif len(sys.argv) > 1 and sys.argv[1] == "--record-build":
        record_build()
    elif len(sys.argv) > 1 and sys.argv[1] == "--check-native":
        print(json.dumps(check_native(), indent=2))
    elif len(sys.argv) > 1:
        print(json.dumps(verify(Path(sys.argv[1])), indent=2))
    else:
        build()
