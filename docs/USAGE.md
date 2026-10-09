# Instalasi, operasi dan pemulihan

Panduan untuk rilis `v6.3.0-rc1`. Target: `com.kakaogames.gdts` 3.54.0 / 423, Android API ≥26, x86_64 atau arm64-v8a. Build dan preview UI tidak membuktikan seluruh efek gameplay; hasil aktual dicatat dalam [laporan modernisasi](MODERNIZATION_V6_3.md) dan [laporan runtime bertanggal](RUNTIME_REPORT_20261008.md).

1. Unduh ZIP dan checksum dari [GitHub Releases](https://github.com/mysticgate38041/wsm/releases), lalu cocokkan SHA-256 dengan `Get-FileHash -Algorithm SHA256`.
2. Cocokkan package/versi/code target dan ABI. Installer membutuhkan Android API ≥26, bootmode dan Magisk ≥26 untuk jalur Zygisk API v4; provider alternatif perlu kualifikasi tersendiri.
3. Cadangkan modul lama `wsm_gt`, pasang ZIP memakai pengelola modul, aktifkan provider Zygisk dan reboot secara manual.
4. Buka gameplay dan tunggu `ready`. Semua kontrol mulai OFF; profil tidak diterapkan otomatis. Uji kontrol satu per satu, kemudian OFF/PANIC, background/resume, ganti stage/hero dan cold start.
5. Untuk pemulihan, tutup game, nonaktifkan/hapus `wsm_gt` melalui pengelola modul dan reboot. Jika status fault/restoration gagal, restart proses diperlukan.

Command dan diagnostik lengkap: [COMMANDS_V6](../src/wsm-v2/docs/COMMANDS_V6.md). Transport memakai root ADB dan file app-private dengan request ID serta completion correlation. `catalog 0..11` bersifat read-only. Katalog membedakan critical pulse dari 100% semua hit, refill periodik dari resource tak berkurang, pulse damage dari multiplier seluruh serangan, dan permintaan consume dari reward terkonfirmasi.

[Indeks dokumentasi](README.md) · [Build dan pengujian](BUILDING.md)
