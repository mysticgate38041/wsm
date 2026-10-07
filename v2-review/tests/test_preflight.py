"""Read-only preflight tests. Fixtures are synthetic, not runtime evidence."""
import contextlib
import importlib.util
import io
import json
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


def module():
    spec = importlib.util.find_spec("wsm_preflight")
    if spec is None:
        raise AssertionError("Missing read-only preflight implementation")
    import wsm_preflight
    return wsm_preflight


def elf_fixture(machine=183, align=16384, offset=0, address=0, flags=5):
    ident = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)
    header = struct.pack("<HHIQQQIHHHHHH", 3, machine, 1, 0, 64, 0, 0,
                         64, 56, 1, 0, 0, 0)
    program = struct.pack("<IIQQQQQQ", 1, flags, offset, address, 0,
                          120, 120, align)
    return ident + header + program


class ElfTests(unittest.TestCase):
    def test_accepts_aligned_arm64_shared_library(self):
        result = module().inspect_elf(elf_fixture(), "arm64-v8a")
        self.assertEqual(result["abi"], "arm64-v8a")
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["scope"], "ELF_HEADER_AND_LOAD_SEGMENTS_ONLY")
        self.assertEqual(result["load_segments"], 1)


class ElfBoundaryTests(unittest.TestCase):
    def inspect(self, data, abi="arm64-v8a"):
        return module().inspect_elf(data, abi)

    def changed(self, offset, fmt, value):
        data = bytearray(elf_fixture())
        struct.pack_into(fmt, data, offset, value)
        return bytes(data)

    def test_x86_64(self):
        self.assertEqual(self.inspect(elf_fixture(machine=62), "x86_64")["status"], "PASS")

    def test_truncated_header(self):
        self.assertEqual(self.inspect(b"\x7fELF")["status"], "FAIL")

    def test_bad_magic(self):
        self.assertEqual(self.inspect(b"BAD!" + elf_fixture()[4:])["status"], "FAIL")

    def test_wrong_abi(self):
        self.assertEqual(self.inspect(elf_fixture(machine=62))["status"], "FAIL")

    def test_unknown_abi_cannot_pass_with_none(self):
        self.assertEqual(self.inspect(elf_fixture(machine=999), None)["status"], "FAIL")

    def test_big_endian_rejected(self):
        self.assertEqual(self.inspect(self.changed(5, "B", 2))["status"], "FAIL")

    def test_elf32_rejected(self):
        self.assertEqual(self.inspect(self.changed(4, "B", 1))["status"], "FAIL")

    def test_not_shared_library(self):
        self.assertEqual(self.inspect(self.changed(16, "H", 2))["status"], "FAIL")

    def test_invalid_header_size(self):
        self.assertEqual(self.inspect(self.changed(52, "H", 1))["status"], "FAIL")

    def test_no_program_headers(self):
        self.assertEqual(self.inspect(self.changed(56, "H", 0))["status"], "FAIL")

    def test_invalid_program_size(self):
        self.assertEqual(self.inspect(self.changed(54, "H", 8))["status"], "FAIL")

    def test_truncated_program_headers(self):
        self.assertEqual(self.inspect(elf_fixture()[:-1])["status"], "FAIL")

    def test_program_offset_outside_file(self):
        self.assertEqual(self.inspect(self.changed(32, "Q", 10000))["status"], "FAIL")

    def test_no_load_segments(self):
        self.assertEqual(self.inspect(self.changed(64, "I", 4))["status"], "FAIL")

    def test_4k_alignment_rejected_by_16k_gate(self):
        self.assertEqual(self.inspect(elf_fixture(align=4096))["status"], "FAIL")

    def test_zero_alignment(self):
        self.assertEqual(self.inspect(elf_fixture(align=0))["status"], "FAIL")

    def test_non_power_two_alignment(self):
        self.assertEqual(self.inspect(elf_fixture(align=24576))["status"], "FAIL")

    def test_64k_alignment_accepted(self):
        self.assertEqual(self.inspect(elf_fixture(align=65536))["status"], "PASS")

    def test_incongruent_virtual_address(self):
        self.assertEqual(self.inspect(elf_fixture(address=1))["status"], "FAIL")

    def test_writable_executable_segment(self):
        self.assertEqual(self.inspect(elf_fixture(flags=7))["status"], "FAIL")

    def test_read_write_segment_accepted(self):
        self.assertEqual(self.inspect(elf_fixture(flags=6))["status"], "PASS")

    def test_file_size_exceeds_memory(self):
        self.assertEqual(self.inspect(self.changed(104, "Q", 100))["status"], "FAIL")

    def test_file_size_exceeds_file(self):
        self.assertEqual(self.inspect(self.changed(96, "Q", 1000))["status"], "FAIL")

    def test_virtual_address_range_cannot_wrap_64_bits(self):
        data = bytearray(elf_fixture(address=(1 << 64) - 16384))
        struct.pack_into("<Q", data, 104, 32768)
        self.assertEqual(self.inspect(data)["status"], "FAIL")


class DeviceTests(unittest.TestCase):
    def test_parses_device_states_without_claiming_game_support(self):
        output = ("* daemon started successfully\nList of devices attached\n"
                  "emulator-5554 device product:sdk model:Android\n"
                  "abc unauthorized usb:1-1\nxyz offline\n")
        self.assertTrue(hasattr(module(), "parse_adb_devices"),
                        "Missing ADB state parser")
        result = module().parse_adb_devices(output)
        self.assertEqual([r["state"] for r in result],
                         ["device", "unauthorized", "offline"])
        self.assertEqual(result[0]["serial"], "emulator-5554")


class ReportTests(unittest.TestCase):
    def test_missing_environment_returns_blockers_without_creating_target(self):
        self.assertTrue(hasattr(module(), "collect_report"),
                        "Missing evidence collector")
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "absent"
            report = module().collect_report(missing, missing, missing)
            self.assertFalse(missing.exists())
            self.assertEqual(report["runtime_status"], "NOT_TESTED")
            self.assertEqual(report["status"], "BLOCKED")
            self.assertIn("PROJECT_MISSING", report["blockers"])
            self.assertIn("NDK_MISSING", report["blockers"])
            self.assertIn("ADB_MISSING", report["blockers"])
            self.assertEqual(report["source_files"], [])


class CommandTests(unittest.TestCase):
    def test_command_success(self):
        result = module().run_readonly([sys.executable, "-c", "print('probe-ok')"])
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["stdout"].strip(), "probe-ok")

    def test_command_nonzero(self):
        result = module().run_readonly([sys.executable, "-c", "raise SystemExit(7)"])
        self.assertEqual(result["exit_code"], 7)
        self.assertEqual(result["status"], "FAIL")

    def test_command_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            result = module().run_readonly([str(Path(directory) / "missing.exe")])
        self.assertEqual(result["status"], "FAIL")

    def test_command_timeout(self):
        result = module().run_readonly([sys.executable, "-c", "import time; time.sleep(2)"], timeout=0.01)
        self.assertEqual(result["status"], "FAIL")
        self.assertIsNone(result["exit_code"])

    def test_empty_adb_list(self):
        self.assertEqual(module().parse_adb_devices("List of devices attached\n"), [])

    def test_adb_garbage_ignored(self):
        self.assertEqual(module().parse_adb_devices("error device\n"), [])

    def test_binary_mutations_do_not_crash_parser(self):
        randomizer = random.Random(354)
        for _ in range(1000):
            data = bytearray(elf_fixture())
            for _ in range(randomizer.randint(1, 8)):
                data[randomizer.randrange(len(data))] = randomizer.randrange(256)
            self.assertIn(module().inspect_elf(data, "arm64-v8a")["status"], {"PASS", "FAIL"})

    def test_cli_reports_blocker_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            output = path / "report.json"
            args = ["--project", str(path / "absent"), "--ndk", str(path / "absent"),
                    "--adb", str(path / "absent"), "--output", str(output)]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module().main(args), 2)
                before = output.read_bytes()
                self.assertEqual(module().main(args), 1)
            self.assertEqual(output.read_bytes(), before)
            self.assertEqual(json.loads(before)["runtime_status"], "NOT_TESTED")

    def test_partial_project_hashes_actual_files(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "jni").mkdir()
            source = path / "jni" / "loader.cpp"
            source.write_bytes(b"local fixture\n")
            report = module().collect_report(path, path / "missing-ndk", path / "missing-adb")
            rows = [item for item in report["source_files"] if item.get("sha256")]
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["bytes"], len(b"local fixture\n"))
            self.assertEqual(report["status"], "BLOCKED")
            self.assertEqual(source.read_bytes(), b"local fixture\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
