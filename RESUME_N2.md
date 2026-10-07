# RESUME N2 — pasca wedge #2 (ekstraksi SDK)
*worm shadow · 6 Okt 2026 ~09:25 · status: mesin mulai wedge lagi saat ekstraksi besar (7z/unzip → proses stick → tasklist/powershell hang).*

## PELAJARAN MESIN INI (WAJIB)
- **JANGAN pakai 7z/unzip untuk ekstraksi besar** — pakai **Python zipfile** (`python -m zipfile -e src.zip dst/` atau script).
- **Jangan paralelkan ekstraksi**; satu per satu, ke **C:\wsmbuild** (sudah ada Defender exclusion).
- Langkah berat/apapun yang bisa hang → **background job + log file + notify**.
- Sisa proses stuck (7z 1124 dsb) akan hilang setelah reboot.

## STATE SAAT INI (jangan hilang)
- ✅ **WSM v2 BUILT**: `WSMenu/src/wsm-v2/dist/wsm-v2.0.0-poc1.zip` (loader x86_64+arm64, engine arm64; ELF checks pass; NDK pinned 27.2.12479018).
- ✅ **Modul TERINSTALL & TERDAFTAR di LDPlayer**: `/data/adb/modules/wsm_gt/` (zygisk+x86_64.so, zygisk+arm64-v8a.so, engine+arm64-v8a.so); ZN registry: `wsm_gt 64 fd=9 ...` ✓. Device sudah di-reboot sekali (modul aktif).
- ✅ Fixture sources: `WSMenu/src/fixture/{AndroidManifest.xml, MainActivity.java, Probe.java, build_fixture.sh}` (script → C:/wsmbuild/sdk).
- ✅ Zip SDK sudah diunduh: `%LOCALAPPDATA%/Temp/wsmtest/bt34.zip` (build-tools r34 win) + `pf34.zip` (platform-34 ext7; berisi `android-34/android.jar` 26MB).

## NEXT (setelah mesin sehat / reboot)
1. Ekstrak via PYTHON (bukan 7z!):
   - `python -m zipfile -e "$LOCALAPPDATA/Temp/wsmtest/bt34.zip" "C:/wsmbuild/sdk/_bt"`
   - pindah `_bt/android-14` → `C:/wsmbuild/sdk/build-tools/34.0.0`
   - `python -m zipfile -e "$LOCALAPPDATA/Temp/wsmtest/pf34.zip" "C:/wsmbuild/sdk/platforms"` (menghasilkan `android-34/`)
   - verify: `d8.bat`, `aapt2.exe`, `zipalign.exe`, `apksigner.bat`, `platforms/android-34/android.jar`.
2. Build fixture: `bash WSMenu/src/fixture/build_fixture.sh` (kalau lambat → bg+log).
3. Install: `adb -s emulator-5554 install -r <fixture>/build/wsm-fixture.apk`
4. `adb logcat -c` → `adb shell am start -n com.wsm.fixture/.MainActivity`
5. Tunggu ±15 dtk → `adb logcat -d | grep -E "WSM|WSMEngine|WSMFixture"` → cari: `staged` → `SYSTEM_LOAD ok` → `WSMEngine ALIVE` → `handshake ack` → `HANDSHAKE_OK` → `probe: fixture callback delivered`.
6. Screenshot bukti: `adb exec-out screencap -p > shot.png` (lihat "[engine] ..." di layar).
7. G4: force-stop → relaunch fixture (pid baru, handshake lagi).
8. G5 (read-only GT): `am start` GT → logcat → `PROBE stage=2 base=0x... sym X/9` (libil2cpp). Nifuji boleh tetap aktif (read-only tidak mengganggu).
- LDPlayer menyala lagi: `C:\LDPlayer\LDPlayer14\ldconsole.exe launch --index 0`, adb serial = `emulator-5554` (bisa juga 127.0.0.1:5555).

## Evidence
- v2 build: `WSMenu/src/wsm-v2/evidence/preflight-2026-10-06-v2-build.json` (BLOCKED hanya karena device; semua ELF PASS).
- Setelah G3/G4/G5: simpan logcat ke `WSMenu/src/wsm-v2/evidence/` (nama unik).

## UPDATE 09:50 — build v2b STUCK → reboot #2
**Hasil tes G3 pertama (fixture):** loader INJECT berhasil ke `com.wsm.fixture` (tag `WSM`), tapi berhenti di:
`exemptFd(payload) failed — load path aborted` → **ZN di stack ini mengembalikan false** (API: "error").
**FIX yang SUDAH ditulis ke kode (SEBELUM reboot, tinggal build ulang):**
1. `loader.cpp` — **staging pindah ke POST-specialize** (fd dibuat setelah specialize = kebal sapuan fd zygote; `exemptFd` DIHAPUS, gak dipakai lagi). PRE hanya baca+validasi+hash engine ke memori (`engine_data`), POST yang buat memfd payload+channel, fill HELLO, setenv, spawn worker, signal.
2. `loader.cpp` — engine per-ABI: `#if __aarch64__ → engine/arm64-v8a.so`, `#if __x86_64__ → engine/x86_64.so`; validasi ELF pakai `kExpectedMachine` sesuai ABI.
3. `build.ps1` — zip kini menyertakan `engine/x86_64.so` (sebelumnya cuma arm64) + pesan "Loader x2 + engine x2".
**Status build:** percobaan build via foreground TIMEOUT; proses `powershell.exe` pid 3064 = **STUCK & unkillable** (CPU mandek 156s, sama pola wedge clang). Reboot #2 diperlukan.
**SETELAH REBOOT — urutan:**
1. Build ulang (WAJIB background+log, JANGAN foreground):
   `cd /c/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2 && export ANDROID_NDK_HOME=D:/Android/ndk/android-ndk-r27c && powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Ndk D:/Android/ndk/android-ndk-r27c` (log → file; poll).
2. Verifikasi zip: `unzip -l dist/wsm-v2.0.0-poc1.zip` → harus ada `engine/arm64-v8a.so` + `engine/x86_64.so`.
3. LDPlayer: `C:\LDPlayer\LDPlayer14\ldconsole.exe launch --index 0` → tunggu boot → `adb connect 127.0.0.1:5555` (adb: `C:/LDPlayer/LDPlayer14/adb.exe`).
4. Install ulang modul: `adb push dist/wsm-v2.0.0-poc1.zip /data/local/tmp/wsm-v2.zip` → `su -c "magisk --install-module /data/local/tmp/wsm-v2.zip"` → **reboot device** (`adb reboot`).
5. Tes ulang fixture: `adb logcat -c` → `am start -n com.wsm.fixture/.MainActivity` → tunggu 12s → `adb logcat -d | grep -iE "WSM"` (HOST-side grep).
   **Target urutan log:** `pre-staged payload=... staging=deferred_to_post` → `post staged+signaled ...` → `SYSTEM_LOAD ok /proc/self/fd/N` → `HANDSHAKE_OK ...` → `PROBE ...` → (fixture) `ENGINE CALLBACK`.
6. Screenshot bukti: `adb exec-out screencap -p > shot.png` → lihat "[engine] ..." (via vision).
7. G4: `am force-stop com.wsm.fixture` → start lagi → ulangi (pid baru).
8. G5: GT read-only probe (Nifuji biarkan aktif).
**PENTING:** jangan pakai foreground untuk build/ekstraksi besar; jangan paralelkan; python = kanal paling stabil.
