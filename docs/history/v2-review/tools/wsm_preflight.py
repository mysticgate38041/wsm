"""Read-only WSM preflight. This tool never installs or loads a module."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import platform
import struct
import subprocess
import sys
from pathlib import Path


def parse_adb_devices(text: str) -> list[dict[str, str]]:
    """Parse only ADB device rows; attachment is not app compatibility."""
    result = []
    started = False
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("List of devices attached"):
            started = True
            continue
        if not started or not line or line.startswith("*"):
            continue
        parts = line.split()
        if len(parts) >= 2 and parts[1] in {"device", "offline", "unauthorized", "recovery", "sideload", "bootloader"}:
            result.append({"serial": parts[0], "state": parts[1]})
    return result


def inspect_elf(data: bytes, expected_abi: str) -> dict:
    """Inspect ELF64 headers and PT_LOAD segments, not runtime compatibility."""
    errors: list[str] = []
    result = {"status": "FAIL", "scope": "ELF_HEADER_AND_LOAD_SEGMENTS_ONLY",
              "abi": None, "load_segments": 0, "errors": errors}
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        errors.append("Expected ELF64 little-endian version 1")
        return result
    values = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    kind, machine, version, _, phoff, _, _, ehsize, phsize, phnum, _, _, _ = values
    abi = {62: "x86_64", 183: "arm64-v8a"}.get(machine)
    result["abi"] = abi
    if kind != 3 or version != 1 or ehsize != 64:
        errors.append("Expected ET_DYN with a valid ELF64 header")
    if abi is None or expected_abi not in {"x86_64", "arm64-v8a"} or abi != expected_abi:
        errors.append("ELF machine does not match a supported expected ABI")
    if not 0 < phnum < 65535 or phsize != 56 or phoff < 64:
        errors.append("Invalid or unsupported program header layout")
        return result
    if phoff + phnum * phsize > len(data):
        errors.append("Truncated program header table")
        return result
    for index in range(phnum):
        ptype, flags, offset, address, _, filesz, memsz, align = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + index * phsize)
        if ptype != 1:
            continue
        result["load_segments"] += 1
        if align < 16384 or align & (align - 1):
            errors.append(f"LOAD {index}: alignment is not a power of two >= 16 KiB")
        if (offset - address) % 16384 or (align and (offset - address) % align):
            errors.append(f"LOAD {index}: offset/address alignment mismatch")
        if filesz > memsz or offset + filesz > len(data):
            errors.append(f"LOAD {index}: invalid file/memory bounds")
        if address + memsz > (1 << 64):
            errors.append(f"LOAD {index}: virtual address range exceeds 64 bits")
        if flags & 3 == 3:
            errors.append(f"LOAD {index}: writable and executable")
    if not result["load_segments"]:
        errors.append("No PT_LOAD segment")
    result["status"] = "FAIL" if errors else "PASS"
    return result


def run_readonly(argv: list[str], timeout: float = 20) -> dict:
    """Run an explicit discovery command without a shell."""
    try:
        proc = subprocess.run(argv, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout)
        return {"command": argv, "status": "PASS" if proc.returncode == 0 else "FAIL",
                "exit_code": proc.returncode, "stdout": proc.stdout,
                "stderr": proc.stderr}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": argv, "status": "FAIL", "exit_code": None,
                "stdout": "", "stderr": str(error)}


def collect_report(project: Path, ndk: Path, adb: Path) -> dict:
    """Inventory only; no root, shell on device, install, process attach or patch."""
    project, ndk, adb = Path(project), Path(ndk), Path(adb)
    blockers: list[str] = []
    report = {"schema_version": 1,
              "collected_at": dt.datetime.now().astimezone().isoformat(),
              "scope": "READ_ONLY_HOST_PREFLIGHT",
              "status": "BLOCKED", "runtime_status": "NOT_TESTED",
              "host": {"platform": platform.platform(), "python": sys.version},
              "paths": {"project": str(project), "ndk": str(ndk), "adb": str(adb)},
              "source_files": [], "libraries": [], "devices": [],
              "commands": [], "blockers": blockers,
              "limitations": ["Does not validate Android module execution",
                              "ELF check does not validate symbol semantics or relocation correctness",
                              "Device attachment does not establish app or bridge compatibility",
                              "ADB may start its host daemon; no device-changing commands are issued"]}
    if not project.is_dir():
        blockers.append("PROJECT_MISSING")
    else:
        for relative in ("jni/loader.cpp", "jni/engine.cpp", "jni/zygisk.hpp",
                         "jni/Android.mk", "jni/Application.mk", "scripts/build.ps1"):
            path = project / relative
            try:
                data = path.read_bytes()
            except OSError as error:
                blockers.append("SOURCE_UNREADABLE:" + relative)
                report["source_files"].append({"path": relative, "error": str(error)})
                continue
            report["source_files"].append({"path": relative, "bytes": len(data),
                                           "sha256": hashlib.sha256(data).hexdigest()})
        for abi in ("arm64-v8a", "x86_64"):
            for name in ("libwsm_loader.so", "libwsm_engine.so"):
                path = project / "build" / "libs" / abi / name
                if not path.is_file():
                    blockers.append("BINARY_MISSING:" + abi + "/" + name)
                    continue
                try:
                    data = path.read_bytes()
                    check = inspect_elf(data, abi)
                    check.update({"path": str(path), "bytes": len(data),
                                  "sha256": hashlib.sha256(data).hexdigest()})
                except OSError as error:
                    check = {"path": str(path), "status": "FAIL", "error": str(error)}
                report["libraries"].append(check)
                if check["status"] != "PASS":
                    blockers.append("ELF_INVALID:" + abi + "/" + name)
    properties = ndk / "source.properties"
    if not properties.is_file():
        blockers.append("NDK_MISSING")
    else:
        try:
            report["ndk_properties"] = properties.read_text(encoding="utf-8")
        except OSError as error:
            report["ndk_properties_error"] = str(error)
            blockers.append("NDK_PROPERTIES_UNREADABLE")
        extension = ".exe" if sys.platform == "win32" else ""
        host_tag = "windows-x86_64" if sys.platform == "win32" else "linux-x86_64"
        for tool in ("clang++", "llvm-readelf"):
            executable = ndk / "toolchains" / "llvm" / "prebuilt" / host_tag / "bin" / (tool + extension)
            command = run_readonly([str(executable), "--version"])
            report["commands"].append(command)
            if command["status"] != "PASS":
                blockers.append("TOOL_UNAVAILABLE:" + tool)
    if not adb.is_file():
        blockers.append("ADB_MISSING")
    else:
        command = run_readonly([str(adb), "devices", "-l"])
        report["commands"].append(command)
        if command["status"] != "PASS":
            blockers.append("ADB_QUERY_FAILED")
        else:
            report["devices"] = parse_adb_devices(command["stdout"])
            if not any(row["state"] == "device" for row in report["devices"]):
                blockers.append("NO_READY_DEVICE")
    report["status"] = "BLOCKED" if blockers else "PREFLIGHT_ONLY_PASS"
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--adb", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    report = collect_report(args.project, args.ndk, args.adb)
    # Refuse accidental report overwrite; evidence is an immutable snapshot.
    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
    except OSError as error:
        print(f"Cannot create report: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"status": report["status"], "runtime_status": report["runtime_status"],
                      "blockers": report["blockers"], "report": str(args.output)}, indent=2))
    return 2 if report["blockers"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
