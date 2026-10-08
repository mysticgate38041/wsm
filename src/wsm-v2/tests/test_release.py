"""Package rejection/receipt units use explicit synthetic fixtures, never runtime evidence.

The generated Android package is checked separately, and required by build.ps1/CI.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import struct
import tempfile
import unittest
import warnings
import zipfile
import zlib
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("package_release", Path(__file__).resolve().parents[1] / "scripts/package_release.py")
pkg = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pkg)


def synthetic_elf(machine, name="engine/x86_64.so", extra=(), version_nodes=()):
    """Structural ELF fixture with dynsym/dynstr, not an executable library.

    Extra symbols are (name, binding, visibility, section index, symbol type).
    """
    symbols = [(symbol, 1, 0, 3, 2) for symbol in sorted(pkg.ELF_EXPORTS[name])] + list(extra)
    strings = bytearray(b"\0")
    offsets = {}
    for symbol in [entry[0] for entry in symbols] + list(version_nodes):
        if symbol not in offsets:
            offsets[symbol] = len(strings)
            strings.extend(symbol.encode() + b"\0")
    entries = bytearray(24)  # STN_UNDEF
    for symbol, binding, visibility, index, kind in symbols:
        entries.extend(struct.pack("<IBBHQQ", offsets[symbol], binding << 4 | kind,
                                   visibility, index, 0, 0))
    versions = bytearray()
    for index, node in enumerate(version_nodes):
        following = 28 if index + 1 < len(version_nodes) else 0
        versions.extend(struct.pack("<HHHHIII", 1, 0, index + 2, 1, 0, 20, following))
        versions.extend(struct.pack("<II", offsets[node], 0))
    data = bytearray(64)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 3, machine, 1)
    sections = [(0,) * 10]
    for kind, body, link, entsize in [(3, strings, 0, 0), (11, entries, 1, 24), (1, b"\xc3", 0, 0)]:
        sections.append((0, kind, 0, 0, len(data), len(body), link, 0, 1, entsize))
        data.extend(body)
    if versions:
        sections.append((0, 0x6ffffffd, 0, 0, len(data), len(versions), 1, len(version_nodes), 1, 0))
        data.extend(versions)
    struct.pack_into("<Q", data, 40, len(data))
    struct.pack_into("<6H", data, 52, 64, 0, 0, 64, len(sections), 0)
    for section in sections:
        data.extend(struct.pack("<IIQQQQIIQQ", *section))
    return bytes(data) + pkg.STAMP.encode()


def synthetic_dex():
    data = bytearray(112)
    data[:8] = b"dex\n035\0"
    struct.pack_into("<III", data, 32, len(data), 112, 0x12345678)
    data[12:32] = hashlib.sha1(data[32:]).digest()
    struct.pack_into("<I", data, 8, zlib.adler32(data[12:]) & 0xffffffff)
    return bytes(data)


class ElfExportTests(unittest.TestCase):
    """Public ABI admission/rejection from actual ELF structures in memory."""

    def test_all_documented_entrypoints(self):
        for name, exports in pkg.ELF_EXPORTS.items():
            with self.subTest(name=name):
                data = synthetic_elf(62 if "x86_64" in name else 183, name)
                self.assertEqual(pkg.elf_exports(data, name), sorted(exports))

    def test_unexpected_global_function(self):
        data = synthetic_elf(62, extra=[("__cxa_throw", 1, 0, 3, 2)])
        with self.assertRaisesRegex(ValueError, "unexpected exports __cxa_throw"):
            pkg.elf_exports(data, "engine/x86_64.so")

    def test_unexpected_weak_function(self):
        data = synthetic_elf(62, extra=[("_Znwm", 2, 0, 3, 2)])
        with self.assertRaisesRegex(ValueError, "unexpected exports _Znwm"):
            pkg.elf_exports(data, "engine/x86_64.so")

    def test_unexpected_global_object(self):
        data = synthetic_elf(62, extra=[("_ZTIf", 1, 0, 3, 1)])
        with self.assertRaisesRegex(ValueError, "unexpected exports _ZTIf"):
            pkg.elf_exports(data, "engine/x86_64.so")

    def test_imports_and_private_symbols_are_allowed(self):
        data = synthetic_elf(62, extra=[("malloc", 1, 0, 0, 2), ("private", 0, 0, 3, 2),
                                       ("hidden", 1, 2, 3, 2)])
        self.assertEqual(pkg.elf_exports(data, "engine/x86_64.so"), ["JNI_OnLoad"])

    def test_version_definition_nodes_are_allowed(self):
        data = synthetic_elf(62, extra=[("WSM_1", 1, 0, 0xfff1, 1)], version_nodes=["WSM_1"])
        self.assertEqual(pkg.elf_exports(data, "engine/x86_64.so"), ["JNI_OnLoad"])

    def test_unbacked_absolute_symbol_is_rejected(self):
        data = synthetic_elf(62, extra=[("not_a_version", 1, 0, 0xfff1, 1)])
        with self.assertRaisesRegex(ValueError, "unexpected exports not_a_version"):
            pkg.elf_exports(data, "engine/x86_64.so")

    def test_missing_entrypoint(self):
        data = synthetic_elf(62, "zygisk/x86_64.so")
        with self.assertRaisesRegex(ValueError, "unexpected exports zygisk_module_entry"):
            pkg.elf_exports(data, "engine/x86_64.so")

    def test_entrypoint_must_be_global_function(self):
        for binding, kind in [(2, 2), (1, 1)]:
            with self.subTest(binding=binding, kind=kind):
                data = bytearray(synthetic_elf(62))
                shoff = struct.unpack_from("<Q", data, 40)[0]
                symoff = struct.unpack_from("<Q", data, shoff + 2 * 64 + 24)[0]
                data[symoff + 24 + 4] = binding << 4 | kind
                with self.assertRaisesRegex(ValueError, "GLOBAL DEFAULT function"):
                    pkg.elf_exports(data, "engine/x86_64.so")

    def test_undefined_entrypoint_is_missing(self):
        data = bytearray(synthetic_elf(62))
        shoff = struct.unpack_from("<Q", data, 40)[0]
        symoff = struct.unpack_from("<Q", data, shoff + 2 * 64 + 24)[0]
        struct.pack_into("<H", data, symoff + 24 + 6, 0)
        with self.assertRaisesRegex(ValueError, "missing exports JNI_OnLoad"):
            pkg.elf_exports(data, "engine/x86_64.so")

    def test_truncated_section_table(self):
        data = synthetic_elf(62)
        shoff = struct.unpack_from("<Q", data, 40)[0]
        with self.assertRaisesRegex(ValueError, "out-of-bounds table"):
            pkg.elf_exports(data[:shoff + 8], "engine/x86_64.so")

    def test_invalid_symbol_name_offset(self):
        data = bytearray(synthetic_elf(62))
        shoff = struct.unpack_from("<Q", data, 40)[0]
        symoff = struct.unpack_from("<Q", data, shoff + 2 * 64 + 24)[0]
        struct.pack_into("<I", data, symoff + 24, 0xffffffff)
        with self.assertRaisesRegex(ValueError, "invalid symbol name offset"):
            pkg.elf_exports(data, "engine/x86_64.so")


class PackageFixtureTests(unittest.TestCase):
    """Synthetic ELF/DEX structures check packaging invariants, not executable validity."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wsm-package-unit-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in pkg.REQUIRED_SOURCES:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"synthetic packaging input\n")
        (self.root / "module-template/module.prop").write_text(
            "id=wsm_gt\nversion=" + pkg.MODULE_VERSION + "\nversionCode=" + str(pkg.MODULE_VERSION_CODE) + "\n")
        catalog = {"requested": 47, "completed": 0,
                   "features": [{"id": name, "runtimeAcceptance": "UNVERIFIED"} for name in pkg.FEATURE_IDS]}
        (self.root / "features/feature_catalog.json").write_text(json.dumps(catalog))
        for name, relative in pkg.ENTRIES.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(synthetic_dex() if name.endswith(".dex") else synthetic_elf(62 if "x86_64" in name else 183, name))
        self.patcher = patch.object(pkg, "ROOT", self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)
        pkg.checkpoint()
        pkg.record_build()
        with contextlib.redirect_stdout(io.StringIO()):
            pkg.build()
        self.source = self.root / ("dist/" + pkg.STAMP + ".zip")
        with zipfile.ZipFile(self.source) as archive:
            self.entries = {name: archive.read(name) for name in archive.namelist()}

    def modified(self, edit, rehash=False):
        entries = dict(self.entries)
        edit(entries)
        if rehash:
            entries["verify.list"] = "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n"
                                             for name, data in sorted(entries.items()) if name != "verify.list").encode()
        path = self.root / "modified.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
        with self.assertRaises((ValueError, KeyError, json.JSONDecodeError)):
            pkg.verify(path)

    def edit_json(self, name, mutation):
        def edit(entries):
            document = json.loads(entries[name])
            mutation(document)
            entries[name] = json.dumps(document).encode()
        self.modified(edit, True)

    def test_complete_fixture_package(self):
        self.assertEqual(pkg.verify(self.source)["entries"], 14)

    def test_incomplete_feature_catalog(self):
        self.edit_json("features/feature_catalog.json", lambda doc: doc["features"].pop())

    def test_false_feature_completion(self):
        self.edit_json("features/feature_catalog.json", lambda doc: doc.__setitem__("completed", 47))

    def test_wrong_requested_feature_count(self):
        self.edit_json("features/feature_catalog.json", lambda doc: doc.__setitem__("requested", 18))

    def test_replaced_design_feature(self):
        self.edit_json("features/feature_catalog.json", lambda doc: doc["features"][0].__setitem__("id", "different"))

    def test_unproven_runtime_qualification(self):
        self.edit_json("features/feature_catalog.json", lambda doc: doc["features"][0].__setitem__("runtimeAcceptance", "PASS"))

    def test_incorrect_target_version_code(self):
        self.edit_json("release.json", lambda doc: doc["target"].__setitem__("versionCode", 424))

    def test_incorrect_target_package(self):
        self.edit_json("release.json", lambda doc: doc["target"].__setitem__("package", "other.package"))

    def test_incorrect_target_version(self):
        self.edit_json("release.json", lambda doc: doc["target"].__setitem__("version", "0"))

    def test_missing_guest_payload(self):
        self.modified(lambda entries: entries.pop("payload/arm64-v8a.so"))

    def test_native_tampering(self):
        self.modified(lambda entries: entries.__setitem__("engine/x86_64.so", entries["engine/x86_64.so"] + b"tampered"))

    def test_archive_runtime_export_even_with_valid_hash(self):
        self.modified(lambda entries: entries.__setitem__("zygisk/x86_64.so",
            synthetic_elf(62, "zygisk/x86_64.so", extra=[("__cxa_guard_acquire", 1, 0, 3, 2)])), True)

    def test_helper_missing_api_even_with_valid_hash(self):
        self.modified(lambda entries: entries.__setitem__("payload/arm64-v8a.so",
            synthetic_elf(183, "engine/arm64-v8a.so")), True)

    def test_duplicate_entry(self):
        path = self.root / "duplicate.zip"
        path.write_bytes(self.source.read_bytes())
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            with zipfile.ZipFile(path, "a") as archive:
                archive.writestr("module.prop", self.entries["module.prop"])
        with self.assertRaisesRegex(ValueError, "duplicate entries"):
            pkg.verify(path)

    def test_unexpected_entry(self):
        self.modified(lambda entries: entries.__setitem__("extra", b"x"))

    def test_incomplete_manifest(self):
        self.modified(lambda entries: entries.__setitem__("verify.list", b""))

    def test_duplicate_manifest(self):
        self.modified(lambda entries: entries.__setitem__("verify.list", entries["verify.list"] + entries["verify.list"].splitlines(keepends=True)[0]))

    def test_wrong_machine_even_with_valid_hash(self):
        def edit(entries):
            data = bytearray(entries["engine/x86_64.so"])
            data[18:20] = b"\xb7\x00"
            entries["engine/x86_64.so"] = bytes(data)
        self.modified(edit, True)

    def test_truncated_elf_even_with_valid_hash(self):
        self.modified(lambda entries: entries.__setitem__("engine/x86_64.so", b"\x7fELF\x02\x01" + pkg.STAMP.encode()), True)

    def test_corrupt_dex_even_with_valid_hash(self):
        def edit(entries):
            data = bytearray(entries["dex/wsm_menu.dex"])
            data[-1] ^= 1
            entries["dex/wsm_menu.dex"] = bytes(data)
        self.modified(edit, True)

    def test_wrong_module_version(self):
        self.modified(lambda entries: entries.__setitem__("module.prop", b"version=v6.1.0-rc3\nversionCode=60103\n"), True)

    def test_missing_modular_source_in_release(self):
        self.edit_json("release.json", lambda doc: doc["sources"].pop("jni/dispatcher.cpp"))

    def test_unproven_game_runtime_in_release(self):
        self.edit_json("release.json", lambda doc: doc["evidence"].__setitem__("newGameRuntime", "PASS"))

    def test_wrong_ndk_in_release(self):
        self.edit_json("release.json", lambda doc: doc.__setitem__("ndk", "26.0"))

    def test_missing_abi_in_release(self):
        self.edit_json("release.json", lambda doc: doc["abis"].pop())

    def test_malformed_source_hash(self):
        self.edit_json("release.json", lambda doc: doc["sources"].__setitem__("jni/module.cpp", "invalid"))

    def test_missing_source_refuses_checkpoint(self):
        (self.root / "jni/dispatcher.cpp").unlink()
        with self.assertRaisesRegex(ValueError, "missing required modular source"):
            pkg.checkpoint()

    def test_restoration_policy_and_fixture_are_required(self):
        for name in ['jni/wsm_restore.h', 'tests/restore_test.cpp']:
            with self.subTest(name=name):
                path = self.root / name
                data = path.read_bytes()
                path.unlink()
                try:
                    with self.assertRaisesRegex(ValueError, 'missing required modular source'):
                        pkg.checkpoint()
                finally:
                    path.write_bytes(data)

    def test_changed_source_refuses_receipt(self):
        (self.root / "jni/module.cpp").write_bytes(b"changed after checkpoint")
        with self.assertRaisesRegex(ValueError, "source changed during build"):
            pkg.record_build()

    def test_changed_source_refuses_packaging(self):
        (self.root / "jni/module.cpp").write_bytes(b"changed after receipt")
        with self.assertRaisesRegex(ValueError, "source differs"):
            pkg.build()

    def test_changed_binary_refuses_packaging(self):
        path = self.root / pkg.ENTRIES["engine/x86_64.so"]
        path.write_bytes(path.read_bytes() + b"changed after receipt")
        with self.assertRaisesRegex(ValueError, "binary differs"):
            pkg.build()

    def test_missing_binary_refuses_receipt(self):
        (self.root / pkg.ENTRIES["payload/arm64-v8a.so"]).unlink()
        with self.assertRaises(FileNotFoundError):
            pkg.record_build()

    def test_uppercase_assembly_is_hashed(self):
        path = self.root / "jni/fixture.S"
        path.write_text("// synthetic assembly input\n")
        pkg.checkpoint()
        pkg.record_build()
        path.write_text("// changed assembly input\n")
        with self.assertRaisesRegex(ValueError, "source differs"):
            pkg.build()

    def test_cmake_is_hashed(self):
        self.assertIn("jni/CMakeLists.txt", pkg.source_hashes())
        (self.root / "jni/CMakeLists.txt").write_text("# changed build definition\n")
        with self.assertRaisesRegex(ValueError, "source differs"):
            pkg.build()

    def test_additional_modular_header_is_hashed(self):
        (self.root / "jni/new_module.hpp").write_text("// new source input\n")
        with self.assertRaisesRegex(ValueError, "source differs"):
            pkg.build()

    def test_receipt_cannot_reuse_older_stamp(self):
        path = self.root / "build/build-receipt.json"
        receipt = json.loads(path.read_text())
        receipt["build"] = "wsm-v6.1.0-rc3"
        path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, "build receipt metadata mismatch"):
            pkg.build()

    def test_receipt_cannot_omit_export_validation(self):
        path = self.root / "build/build-receipt.json"
        receipt = json.loads(path.read_text())
        receipt.pop("nativeExports")
        path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, "native exports differ"):
            pkg.build()

    def test_receipt_rejects_runtime_exports_after_new_checkpoint(self):
        path = self.root / pkg.ENTRIES["zygisk/x86_64.so"]
        path.write_bytes(synthetic_elf(62, "zygisk/x86_64.so", extra=[("_Znwm", 2, 0, 3, 2)]))
        pkg.checkpoint()
        with self.assertRaisesRegex(ValueError, "unexpected exports _Znwm"):
            pkg.record_build()

    def test_deterministic_fixture_repack(self):
        before = self.source.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            pkg.build()
        self.assertEqual(before, self.source.read_bytes())


class GeneratedReleaseTests(unittest.TestCase):
    def test_generated_android_release(self):
        path = pkg.ROOT / ("dist/" + pkg.STAMP + ".zip")
        if not path.is_file():
            if os.environ.get("WSM_REQUIRE_RELEASE") == "1":
                self.fail("The generated Android release is required for this build")
            self.skipTest("Android package not built; fixture tests do not establish runtime or build acceptance")
        self.assertEqual(pkg.verify(path)["entries"], 14)
        with zipfile.ZipFile(path) as archive:
            self.assertEqual(json.loads(archive.read("release.json"))["sources"], pkg.source_hashes())
            self.assertEqual(archive.read("features/feature_catalog.json"), (pkg.ROOT / "features/feature_catalog.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
