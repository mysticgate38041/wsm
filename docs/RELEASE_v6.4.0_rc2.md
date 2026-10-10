# WSM 6.4.0 RC2 — perbaikan klasifikasi GM speed, kamera ResizeTo, fd hygiene

RC2 memperbaiki temuan verifikasi RC1 (laporan QA live pada sesi battle) tanpa mengubah scope. Target tetap `com.kakaogames.gdts` 3.54.0 (423), API 26+, x86_64 dan arm64-v8a. Module versionCode 60402, protocol 3.

## Perbaikan

- **Klasifikasi GM speed & preset**: `gm max speed` dan load preset dengan speed ON sebelumnya dikembalikan `rejected` padahal patch sudah aktif (ack sukses ON = `SPEED ON ...`, bukan `OK`). Klasifikasi kini memakai state hasil aktual (`g_speed_on` + nilai) alih-alih prefix ack, sehingga status yang dilaporkan mencerminkan kenyataan; re-verifikasi live dijadwalkan pada sesi device berikutnya.
- **Kamera `fov`**: `OverrideDefaultCameraSize` saja tidak mengubah `get_Size()` pada target live (`size=4.00->4.00`). Set sekarang juga memanggil `ResizeTo(size, 0, true)` (instan) dan reset memanggil `ResetDefaultCameraSize` + `ResizeToDefault(0, true)`; ack menambahkan `over=` sebagai diagnostik. Efek zoom tetap wajib diverifikasi visual di device.
- **fd hygiene (F1b)**: dirfd modul dari framework ternyata bertahan di tabel fd proses game (terlihat scanner se-uid). Setelah fase pre selesai, file di belakang nomor fd diganti `/dev/null` via `dup2` — path `wsm_gt` hilang dari `/proc/<pid>/fd` tanpa risiko double-close.
- **Installer**: banner `customize.sh` kini membaca versi dinamis dari `module.prop` (tidak lagi tertinggal), teks RC dihapus.
- **Wording**: ack `gm reset` menjadi "all controls off; slider values retained" — nilai slider memang dipertahankan sesuai desain (UI menyediakan aksi terpisah "Reset nilai kontrol nonaktif").

## Dasar (bukti sesi RC1)

- Battery live 1-per-1 di battle: 18/18 kontrol ON `applied`, snapshot ON lengkap, OFF semua pulih; `gm all` 12/12 ON/OFF; waypoint save/load `ok=1`; preset 18/18 (tanpa speed) dan 17/18 (dengan speed ON — bug yang kini diperbaiki); `gm max speed` rejected padahal `SPEED ON` (bug yang kini diperbaiki).
- Stealth audit live: nonce 0600 root ✓, environ tanpa `WSM_*` ✓, transport berisi frame `E1:` ✓, logcat higienis ✓. Residu teridentifikasi: map `.so` modul (level framework, terverifikasi tidak dipindai daemon) dan fd modul (diperbaiki F1b).

Verifikasi: build lokal dua ABI + Java/DEX + paket PASS; battery fixture 15/15 di device; CI tag menerbitkan aset dengan checksum dan provenance. Gameplay acceptance per-fitur tetap UNVERIFIED (katalog 47 incomplete); ekonomi server-authoritative dan mode ranked/co-op tetap di luar scope. ZIP, `.sha256` dan `ci-build-provenance.json` berasal dari job ndk-build pada commit/tag/run yang sama. Release/tag lama tidak ditimpa.
