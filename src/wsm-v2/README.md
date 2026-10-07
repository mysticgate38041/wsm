# WSMenu 6.1.0 RC2

Publikasi GitHub memakai CI build baru dan snapshot mode yang terverifikasi. Panduan menyeluruh terbaru berada di [README repo](../../README.md), [CI/CD](../../docs/CI_CD.md), dan [matriks 47 fitur](../../docs/FEATURES.md). Untuk clone tanpa dump eksternal, tambahkan `-CatalogSnapshot` pada build script. Checksum/log di laporan lama adalah bukti historis; checksum aset release mengikuti hasil build CI pada tag yang diterbitkan.

**Kandidat terbaru:** `dist/wsm-v6.1.0-rc2.zip` dan checksum pendamping. [Audit dump asli dan validasi RC2](docs/ORIGINAL_DUMP_AUDIT.md) menghubungkan folder `C:/Users/Administrator/Downloads/Mod/gt_dump` ke provenance WSM, evidence native/Lua dan seluruh 47 fitur. Seluruh 155 input katalog cocok dengan dump asli.

RC2 memperbaiki blocker getter critical damage: prologue aslinya mengandung ADRP yang ditolak RC1. Wrapper sekarang merelokasi ADR/ADRP dengan range checks, tetap menolak control-flow/literal prologue yang tidak didukung. Empat unit Android, 16 uji paket, Java/DEX dan 8 kasus installer PASS. Emitter ARM64 juga dieksekusi pada APK probe terisolasi melalui native bridge dan PASS; probe sudah dihapus. Modul RC2 belum dipasang pada game, dan finalisasi seluruh 47 efek gameplay masih belum terpenuhi.

Uji relokasi baru: `build/wsm-reloc-test`. APK probe dapat direproduksi dengan `tests/arm64-reloc-fixture/build_probe.py`. Bukti terbaru: `evidence/dump-finalization-20261007/`.

## Kandidat RC1 (historis)

**Kandidat aktif:** `dist/wsm-v6.1.0-rc1.zip` dan checksum pendamping. [Matriks dan validasi 47 fitur](docs/FINALIZATION_47.md) menjelaskan perubahan terbaru: 18 kontrol native, tab katalog seluruh 47 fitur desain, Critical Damage Scale hero-only, full-signature binding, target versionCode 423, perbaikan Time Scale dan completion/profile/PANIC epoch.

**Finalisasi 47 fitur belum terpenuhi.** Status katalog: 14 partial, 2 prototype, 16 belum diimplementasikan, 10 authority/persistensi belum terverifikasi dan 5 mekanik target belum ditemukan. Kandidat telah dibangun dan lolos 16 uji paket, uji Java, tiga unit Android dan 8 kasus installer harness; kandidat belum dipasang atau diuji efek gameplay-nya. Package verifier mempertahankan qualification gate ini.

Bangun kandidat dengan `scripts/build.ps1`; generator katalog dijalankan sebelum source checkpoint. Tiga unit Android tersedia dalam `build/wsm-runtime-test`, `build/wsm-patch-test`, `build/wsm-binding-test`. Lihat `docs/FINALIZATION_47.md` untuk bukti dan syarat finalisasi. `catalog 0..11`, `critdmg 0` / `critdmg 1..5`, serta `epoch E command` ditambahkan ke command sebelumnya.

## Baseline 6.0.0 (historis)

Bagian di bawah mendokumentasikan rilis 6.0.0 dan bukti pada versi tersebut. Nama ZIP, jumlah kontrol/tes dan klaim pemasangan di bagian baseline tidak menyatakan status kandidat 6.1.0 RC1. Backup README baseline ada di `evidence/finalization-20261007-162612/baseline/README.md`.

Jalur implementasi aktif tetap `src/wsm-v2`. Versi 6 mengganti kontrol/UI dan paket rilis pada jalur ini; `src/wsm` tetap arsip POC. Dokumen bertanggal 6 Oktober menjelaskan versi sebelumnya dan bukan status rilis 6.

**Artefak:** `dist/wsm-v6.0.0.zip`, checksum `dist/wsm-v6.0.0.zip.sha256`. Paket berisi loader dan engine x86_64/arm64-v8a, helper ARM64, serta menu DEX. Paket sudah dipasang melalui Magisk dan diuji pada LDPlayer API 34 dengan Zygisk Next 1.5.0. Hasil per fitur dan batas kualifikasi ada di `docs/RELEASE_VALIDATION.md`; kelulusan ACK tidak menyatakan semua efek gameplay telah terukur.

## Perubahan yang tersedia

- Satu worker memiliki semua mutasi. UI/file transport hanya mengirim command ke antrean terbatas, mendapat ID, lalu membaca completion; `accepted` tidak dianggap `applied`.
- Epoch per scene, pemeriksaan package/version/foreground, reset saat pause dan pergantian hero/stage, PANIC prioritas, timeout bus yang dikarantina, dan state fault yang membutuhkan restart.
- Ledger opsi karakter hanya menghapus bit yang dimiliki WSM; OFF time scale menghapus modifier bernama `wsm`, tidak menghapus modifier milik game.
- ARM64 helper dibundel, 32 slot menggantikan 24 slot yang terlalu sedikit, satu instruksi branch 4 byte tidak menimpa getter berikutnya, patch alias memulihkan protection dan mencoba rollback jika sebagian gagal. Tidak perlu menyalin helper secara manual ke libdir game atau menaruh pointer bus di storage publik.
- Menu Android native: 17 kontrol, pencarian, kategori, slider, tiga profil manual, teleport relatif, sweep, self-test, diagnostik, log terbatas, collapse, drag, dan hotkey saat menu berfokus. Activity dan callback dilepas pada pause/destroy; callback lama tidak mengubah scene baru.
- Nilai speed 1–5×, radius 5–40 m, pulse power 1–99, time scale 0.1–5×; nilai slider benar-benar diteruskan. Nilai default radius saat OFF 20 m; power saat OFF 1 × 100.000.
- Build toolchain terpin, lima ELF diperiksa untuk ABI/export/16 KiB LOAD + RELRO alignment, DEX header/hash diperiksa, ZIP deterministik, manifest integritas dan uji paket yang dimutasi.

## Pemasangan

1. Cocokkan SHA-256 ZIP dengan berkas `.sha256`. Target saat ini **com.kakaogames.gdts 3.54.0**, Android API ≥26, proses x86_64 atau arm64-v8a; ABI 32-bit tidak dikirim.
2. Gunakan pengelola modul Android dalam sistem yang sudah boot. Magisk ≥26 diperlukan untuk Zygisk API v4 pada jalur Magisk; provider alternatif perlu kualifikasi tersendiri.
3. Cadangkan modul WSM lama (`wsm_gt`) dan nonaktifkan injektor GT lain. ID `wsm_gt` dipertahankan agar pembaruan memakai slot modul yang sama.
4. Pasang ZIP, aktifkan provider Zygisk, lalu reboot. Instalasi baru tidak dilakukan otomatis oleh build script.
5. Buka game versi cocok, masuk gameplay sampai status `ready`, lalu uji satu kontrol setiap kali. Semua kontrol mulai OFF dan profil tidak dimuat otomatis. Tab Sistem menjelaskan status, self-test, dan batas fitur.
6. Uji OFF dan PANIC, background/resume, ganti stage/hero, serta cold start sebelum menganggap konfigurasi layak dipakai. Hook berlabel eksperimental tetap memerlukan verifikasi efek di game.

Pemulihan: tutup game; nonaktifkan/hapus `wsm_gt` melalui pengelola modul, lalu reboot. Jika Android gagal boot, gunakan mekanisme pemulihan pengelola modul untuk menonaktifkan `wsm_gt`. Jangan menganggap PANIC sukses bila status `fault`/restoration gagal; restart target untuk menghapus patch dalam proses.

## Build dan validasi

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Ndk D:\Android\ndk\android-ndk-r27c
```

`build.ps1` menerima `-Jdk`, `-Sdk`, `-Python`, `-Jobs`; default lokal tercantum pada script. NDK exact 27.2.12479018, Zygisk header terpin, JDK 17, Android 34 platform/build-tools diperlukan. Script memeriksa native, membangun semua kelas menu dan DEX, menjalankan state test, membangun dua executable unit Android, mengemas dan menjalankan 11 regresi paket.

```powershell
adb -s emulator-5554 push build/wsm-runtime-test /data/local/tmp/
adb -s emulator-5554 push build/wsm-patch-test /data/local/tmp/
adb -s emulator-5554 shell "chmod 700 /data/local/tmp/wsm-*-test && /data/local/tmp/wsm-runtime-test && /data/local/tmp/wsm-patch-test"
python tests/device_installer_test.py --adb C:\LDPlayer\LDPlayer14\adb.exe --serial emulator-5554 --output evidence/installer-new.json
```

Unit Android ini tidak memasang modul atau mengubah game. Harness installer menggunakan direktori sementara di `/data/local/tmp` dan stub fungsi manager. Bukti terakhir dirangkum di `docs/RELEASE_VALIDATION.md`; log build asli disimpan di `evidence/`.

## Operasi dan diagnostik

```powershell
python scripts/wsmctl.py --adb C:\LDPlayer\LDPlayer14\adb.exe --serial emulator-5554 --diagnostics --output evidence/diagnostics-new.json
python scripts/wsmctl.py --adb C:\LDPlayer\LDPlayer14\adb.exe --serial emulator-5554 --su /data/local/tmp/su status
python scripts/wsmctl.py --adb C:\LDPlayer\LDPlayer14\adb.exe --serial emulator-5554 --su /data/local/tmp/su selftest
```

`--diagnostics` hanya membaca props, versi aplikasi, PID, dan log WSM. Command transport membutuhkan root dan WSM 6 aktif, memakai path app-private dengan request ID; tool menunggu ACK yang cocok dan completion. Pilih `--su` yang benar di perangkat Anda. Berkas output existing ditolak. Daftar command dan makna state ada di `docs/COMMANDS_V6.md`.

## Batas implementasi

`applied` berarti konfigurasi/call/patch dilaporkan berhasil, bukan bukti efek gameplay terukur. Snapshot mencantumkan `observed: null`; angka loot merupakan kandidat/permintaan, bukan reward terkonfirmasi. Gold/gem/inventori/progres server, ESP, freecam, dan ranked bypass tidak diimplementasikan. Tidak ada angka FPS palsu atau auto-enable profil.

Resolver dan guard tidak membuktikan thread-safety Unity/Houdini. Mutasi masih menggunakan worker IL2CPP yang attached, belum ada dispatcher pada Unity main thread. Trampoline memakai arena BSS milik helper atau alokasi dekat tanpa MAP_FIXED, berubah RW menjadi RX sebelum dipublikasikan; halaman yang pernah dipublikasikan disimpan hingga proses keluar (batas 512). Wrapper speed memanggil implementasi getter asli dan mengalikan hasil khusus hero. Cache/konkurensi translator dan long-session soak masih perlu kualifikasi lebih luas; perangkat ARM64 fisik belum tersedia untuk uji. Time Scale memerlukan instance GlobalTimeManager yang hidup; pada scene uji instance tersebut tidak tersedia, sehingga ON ditolak dan OFF tetap idempotent. Quirk SIGKILL saat beberapa cold start masih terlihat dan dicatat terpisah. Versi game lain ditolak untuk mutasi; perubahan versi memerlukan audit RVA/layout dan pengujian ulang.
