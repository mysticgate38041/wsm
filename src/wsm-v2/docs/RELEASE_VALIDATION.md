# Validasi WSM 6 — 7 Oktober 2026

Status: **paket dapat dipasang dan runtime LDPlayer diuji; kualifikasi efek gameplay masih parsial**. Implementasi aktif berada di `src/wsm-v2`; `src/wsm` tetap arsip. Status lama pada dokumen 6 Oktober tidak dipakai sebagai bukti versi ini.

ZIP `dist/wsm-v6.0.0.zip`: 188,065 bytes, 13 entries. SHA-256:

`32527547e3c5328fefcfd44a219c7922a2f3ac4ecfd058a11d4b7b818a9d1f5f`

Target lokal: `emulator-5554`, LDPlayer 14, Android API 34, proses host x86_64 dan game ARM64 melalui Houdini; Guardian Tales `com.kakaogames.gdts` 3.54.0 / versionCode 423, Magisk 31000, Zygisk Next 1.5.0.

## Build dan pemasangan

| Pemeriksaan | Hasil | Bukti |
|---|---|---|
| 5 library NDK r27c / 2 ABI, exports, tanpa libc++_shared atau TEXTREL | PASS | `evidence/v6-build-final.log` |
| LOAD ≥16 KiB; akhir RELRO kelipatan 16 KiB | PASS | Build + readelf |
| Java/JDK17, D8 API26, DEX header/SHA1/Adler32 | PASS | Build + package verifier |
| UI accepted/applied, rejection, timeout, stale epoch, fault | PASS | `tests/ControlStateTest.java` |
| 11 regresi paket: tamper/duplicate/missing/ABI/version/DEX, stale sources, deterministic repack | PASS | `tests/test_release.py`, log build |
| 36 preflight lama termasuk 1.000 mutasi ELF parser | PASS | `v2-review/tests/test_preflight.py` |
| Antrean/PANIC/epoch/history/snapshot, 4 produsen / 4.000 requests pada Android | PASS | `evidence/v6-android-branch-tests.txt` |
| 3 alias mmap, readback/proteksi/rollback partial failure | PASS | `evidence/v6-android-branch-tests.txt` |
| Branch imm26/alignment, near allocation, atomic 4-byte write, adjacent bytes | PASS | `evidence/v6-android-branch-tests.txt` |
| Installer unit: 2 ABI diterima; negative cases dan tamper ditolak | PASS 8/8 | `evidence/v6-installer-release.json` |
| Instalasi Magisk nyata dan integrity check | PASS | `evidence/v6-delivered-deployment.json` |
| 10 file runtime/metadata aktif cocok dengan ZIP | PASS | `evidence/v6-delivered-deployment.json` |
| HANDSHAKE protocol3/build/UID/PID/ABI; menu DEX pada target | PASS | `evidence/v6-delivered-startup.log` |

Installer unit memakai stub manager di direktori shell sementara, bukan uji provider atau ARM64 fisik. Loader benar-benar diuji melintasi reboot. Pembaruan engine/helper berikutnya dipasang melalui Magisk, lalu file aktif diganti atomik saat target berhenti dan target di-cold-restart; loader tetap identik. Versi terakhir memperbaiki GOD ON berulang. Loader, helper dan DEX-nya identik dengan versi yang diukur untuk movement dan readback; engine delta serta perbandingan hash tercatat dalam deployment JSON. `release.json` tetap konservatif: build/package checker tidak melakukan kualifikasi menyeluruh pada setiap target.

## Bukti runtime dan batas tiap fitur

| Fitur / operasi | Teruji | Batas bukti |
|---|---|---|
| Helper ARM64 fd-load + heartbeat | PASS; multiply 6×7=42 | In-process pada Houdini; belum perangkat ARM64 fisik |
| Executable selftest | PASS: branch, hero-only leaf/wrapped getter, GOD out-params, neighbor, restore | Menggunakan kode sintetis milik helper; tidak merusak game |
| Damage guard (`godmode`) | Metadata/prologue cocok; ON/OFF, hanya 4 byte berubah; 2 alias dipulihkan | Kerusakan HP dalam pertempuran belum diukur ulang untuk rilis ini |
| Speed 2× | Semua 3 getter terpasang, hanya 4 byte berubah, OFF byte-for-byte restore | Walk: input D-pad 600 ms menghasilkan 2.3 vs 4.8 unit (≈2.09×). Dash/soft-dash belum diukur individual |
| No-CD | 3 target metadata; ON/OFF, single instruction dan restore pada setiap target | Efek seluruh jenis skill/gate belum dibuktikan |
| Freeze AI | Target `PickNTriggerBattleAction`; single-instruction patch dan restore | Perilaku seluruh jenis monster belum diuji |
| God/hp/stam/mana/poise/immune | ON/OFF diterima untuk 2 pemain, faults 0 | Uji rilis ini memverifikasi konfigurasi/API, bukan semua efek tempur |
| dmg/aura/crit/ohk/onehp | ON/OFF diterima; parameter 25 dan 30 benar-benar diteruskan | Efek damage/critical setiap varian musuh belum diukur |
| Auto-loot | ON/OFF diterima; candidate/request tercatat | Scene uji items=0; tidak ada klaim reward terkumpul |
| Teleport relatif | Readback posisi dan reset posisi PASS, faults 0 | Scene uji tunggal |
| Sweep | ACK targets=12, faults=0 | Jumlah target/call bukan konfirmasi kematian atau reward |
| PANIC | Restore complete dan kontrol OFF | Concurrency semua skenario ekstrem belum dikualifikasi |
| Background/resume | Patch God + 3 getter dipulihkan; PID sama, epoch 3→4→5, semua OFF, ready/faults0 | Satu siklus; rotation/destruction dan scene/hero variants belum semua diuji |
| UI pencarian, slider, toggle dan PANIC | Pencarian Speed menampilkan kontrol terkait; slider 3× + toggle terkonfirmasi pada snapshot; PANIC mengembalikan semua OFF/faults0 | Satu alur pada landscape LDPlayer; bukan semua rentang/keyboard/lifecycle |
| Simpan/muat profil | Profil Explore menyimpan 17 kontrol OFF; load mengirim command OFF dan kembali ready/faults0 | Explore saja; profil lain dan konfigurasi aktif campuran belum diuji |
| Time Scale ON | **TIDAK TERSEDIA pada scene ini**, ditolak | Instance/API GlobalTimeManager tidak tersedia; UI tetap OFF. OFF idempotent berhasil |

Bukti: `evidence/v6-final-selftest.json`, `v6-final-god-proof.json`, `v6-final-speed-proof.json`, `v6-final-nocd-freeze-proof.json`, `v6-final-feature-controls.json`, `v6-final-lifecycle.json`. Integrasi build terakhir dan GOD ON berulang: `evidence/v6-delivered-integration.json`, status akhir `v6-delivered-final-status.json`.

Bukti UI: `evidence/v6-delivered-profile-save.json`, `v6-delivered-profile-load-status.json`, `v6-delivered-ui-slider-panic.json`, `v6-search.png`, `v6-delivered-final-ui.png`. Profil Explore dimuat ulang setelah pengujian; pencarian dikosongkan dan semua fitur ditinggalkan OFF. Screenshot akhir memperlihatkan menu normal serta status ready PID10276 / epoch3.

`applied` berarti konfigurasi/call/patch berhasil dilaporkan, bukan otomatis bukti efek gameplay. Snapshot menyimpan `observed: null`; loot memakai candidate/request counts. Tidak ada modifikasi gold/gem/server progression, ESP, freecam atau bypass ranked.

## Perbaikan yang ditemukan di perangkat

- Offset file pada dump berbeda 0x4000 dari RVA PT_LOAD. Produksi sekarang membaca `MethodInfo.methodPointer` pada offset 0; offset +0x10 adalah invoker dan bukan target patch.
- Getter speed aktual mempunyai logika dan prologue lengkap. Wrapper mempertahankan implementasi asli (modifier/obfuscation/hotfix), lalu mengalikan hasil khusus hero; pure getter tetap memakai emitter pendek yang tervalidasi.
- Houdini mereservasi address space sekitar game. Arena BSS milik helper menyediakan halaman sendiri dalam jarak branch, tanpa menimpa mapping aplikasi. Fallback near allocation memakai NOREPLACE, tanpa MAP_FIXED.
- Satu word branch diubah secara atomik; byte fungsi berikutnya tidak ditimpa. Page emitter diminta RW→RX sebelum publikasi dan disimpan hingga proses keluar, maksimal 512. Mapping guest Houdini dapat tampak r-- pada `/proc/maps`, berbeda dari ABI ARM64 fisik.
- Counter pointer memakai literal 64-bit, bukan ADRP berjarak tidak tervalidasi. Selftest mengeksekusi emitter yang sama dengan produksi.
- CLI root menulis command dengan UID/GID aplikasi dan mode0600. Query static field dilakukan ketika command ready memerlukannya, bukan pada setiap tick awal. PANIC hanya membersihkan modifier waktu milik WSM.
- GOD ON berulang mempertahankan slot dan state yang dimiliki, sehingga profile load tidak mengubah UI menjadi OFF saat patch masih aktif.

## Kondisi perangkat dan pekerjaan kualifikasi tersisa

Old module backup: `evidence/backups/wsm-before-v6-20261007-040732.tar`, SHA-256 `6722daff03c5f2e0a723e566e44841a2f2b5199d30180818371648c21ce63955`. Module ID `wsm_gt` dipertahankan. `guardiantales_nifuji` dinonaktifkan untuk isolasi dan tetap disabled; `zygisksu` tetap provider aktif. Tidak ada fixture APK yang dipasang.

Beberapa cold starts masih dibunuh SIGKILL sebelum probe IL2CPP; retry kemudian hidup. Ini sesuai quirk yang pernah dicatat, namun bukan kelulusan startup stabil atau pembuktian penyebab. Tidak ada klaim soak panjang, semua variasi game atau ABI fisik selesai. Mutasi managed masih pada worker IL2CPP attached, bukan dispatcher Unity main thread; guard/rollback tidak membuktikan keselamatan setiap API Unity atau konkurensi translator.

Kualifikasi lanjut: efek tempur/skill/reward yang nyata, scene/hero transitions, profil lain/konfigurasi aktif campuran, seluruh rentang slider/hotkeys dan variasi lifecycle, queue/timeouts di perangkat, long-session soak dan ARM64 fisik. Time Scale perlu audit instance/API aktif pada scene yang menyediakannya.
