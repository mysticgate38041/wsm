# WSM 6.2.0 RC1 — migrasi arsitektur modular

WSM 6.2.0 RC1 (`wsm-v6.2.0-rc1`, module versionCode `60201`) memisahkan layanan Runtime, resolver, dispatcher, observed flags, worker scheduling dan retained trampoline pool sambil mempertahankan pipeline RC3 yang masih diperlukan. Target exact tetap `com.kakaogames.gdts` 3.54.0 /423, Android API ≥26, x86_64 dan arm64-v8a.

## Paket terkini dan hasil runtime

**Belum operasional penuh untuk 18 kontrol.** Paket terpasang terbaru: 443.218 byte, SHA-256 `ae0dcb92d5147133d69e3351dbc5032999dbb15f831814052b4e355ca0684041`. Semua 70 input source cocok dan 11 file modul terpasang cocok. Angka/hash c4a59c8a dan e40da43b di bawah adalah riwayat build sebelumnya.

Build terbaru lulus 52 Python checks serta Java/DEX/ELF gates; 12 fixture Android dikompilasi per ABI. Fixture restoration 20 kasus lulus pada dua ABI LDPlayer. Perbaikan ekspor C++ statis dan pinned GC ownership telah masuk paket.

Pada game nyata, 18 kontrol menerima ON/OFF; overlap opsi dan PANIC berulang berhasil dibersihkan. God/Opsi mempertahankan HP pada observasi terbatas, dan HP Protection mempertahankan hero hidup pada HP 1. Efek pulse, Freeze AI dan sejumlah kontrol lain belum terverifikasi penuh. Startup masih membutuhkan buka ulang; akar SIGKILL belum diketahui. Percobaan perbaikan pulse terbaru terhenti oleh pemeriksaan otomatis worker dan edit parsial tidak dipaketkan. [Laporan gameplay lokal](../.publish/WSM_GAMEPLAY_REPORT_20261008.md) mencatat bukti serta batasnya.

## Perubahan perilaku

- Entry build Zygisk memakai module.cpp; loader.cpp menjadi shim source kompatibilitas.
- Worker menggunakan antrean bounded dan notification generation dengan deadline OFF 1.000 ms, active 250 ms, serta pending UnityMain 25 ms. Shared mailbox helper memakai notification futex biasa melalui libc.
- Completion stale/duplikat dan publikasi snapshot epoch diperketat. Owner reset PANIC tetap melakukan restoration fisik state yang dimiliki; reset pending tidak dilaporkan ready.
- FeatureFlags menjadi cache koheren dari 18 kontrol yang diamati; cache tidak mengeksekusi command atau membuktikan efek gameplay.
- Binding memakai identity exact dan signature lengkap. API GlobalTimeManager di-retry oleh owner setelah identity terverifikasi, dan speed numeric 0 mengikuti jalur OFF. AOB bounded hanya menyediakan discovery diagnostik dan tidak menyediakan fallback versi/offset executable.
- Pool halaman retained mempertahankan validasi reachability/alignment dan seal RW→RX sebelum branch dipublikasikan. Published pages dipertahankan sepanjang proses.
- Pulse Power/dmg dan Critical Pulse/crit kini terintegrasi pada typed UnityMain sweep: damage tetap 1–99 ×100.000, critical=true/noCritical=false, policy immutable, ONEHP suppress, OHK priority dan Clear Stage tetap 1 juta. ON adalah konfigurasi armed; efek target belum dikualifikasi.
- Build ndk-build tetap default dan backend CMake/Ninja ditambahkan. Source receipts mewajibkan modul baru; dua belas executable fixture native dikompilasi untuk setiap ABI.

## Kualifikasi dan bukti migrasi awal

Katalog masih berisi 47 fitur dengan `completed=0`, `UNVERIFIED` dan `INCOMPLETE_47_FEATURE_SCOPE`. Build/test tidak sama dengan qualification target game. Deskripsi yang lebih sempit seperti refill periodik atau permintaan consume tetap tidak memenuhi seluruh desain infinite resource atau reward confirmed.

| Validasi migrasi awal lokal | Hasil |
|---|---|
| ndk-build + CMake, dua ABI | PASS: lima ELF + DEX dan Java state pada setiap backend |
| Package regression | 35 PASS pada setiap build final |
| Catalog snapshot | 9 PASS |
| Fixture native x86_64 | 11/11 PASS, standalone Android pada LDPlayer |
| Fixture native arm64-v8a | 11/11 PASS melalui LDPlayer translation |
| Installer harness | 8/8 PASS, terisolasi tanpa memasang modul |

Runtime fixture memakai kernel x86_64 dengan page size 4 KiB. ARM64 translation bukan hardware ARM64 fisik. Fixture patch membandingkan permission restoration dengan baseline aktual; requested RX dapat teramati sebagai R pada translation. Tidak ada assertion yang dilewati untuk hasil final, tetapi instruction execution/permission behavior ARM64 fisik dan runtime perangkat 16 KiB belum dikualifikasi.

Workflow mengonfigurasi sebelas host tests lewat CMake/CTest; tidak ada remote CI run yang diklaim pada sesi ini. Receipt, log dan digest final dicatat pada `.publish/V3_MIGRATION_REPORT_20261008.md`. ZIP migrasi awal berukuran 470.267 byte dengan SHA-256 `c4a59c8abe207647a6a826f904c5829e834225fbecb9e74c53824d3ad4372470`; hash ini melekat pada receipt source final, bukan janji digest lintas backend/environment.

Lima ELF, DEX, ABI/export, dependency/TEXTREL, LOAD/RELRO alignment 16 KiB dan receipt merupakan gate build. ZIP/checksum yang diberikan untuk review harus berasal dari receipt final yang cocok dengan source. Tidak ada instalasi/publikasi atau acceptance gameplay baru dalam catatan ini.

## Batas dan risiko yang masih ada

Deadline scheduling belum merupakan pengukuran CPU/latency. Arena pool 512 ×16 KiB berarti kapasitas ruang virtual 8 MiB dan tidak membuktikan penurunan committed RAM. Native call yang berjalan tidak dapat dibatalkan seketika; kegagalan restoration/channel memerlukan restart proses. ARM64 fisik, perangkat dengan runtime 16 KiB, native bridge pada perangkat target, UI/lifecycle dan seluruh semantik fitur tetap memerlukan fixture/perangkat yang sesuai.

Proposal ghost/protection-thread parking, stealth remap dan raw syscall evasion tidak diterapkan. Metadata/AOB tidak mengizinkan game versi baru. Backend legacy yang masih diperlukan tetap dipertahankan; ini bukan port seluruh offset/API.

Lihat [arsitektur v3 yang dikoreksi](ARCHITECTURE_V3.md), [CI/CD](CI_CD.md), [matriks fitur](FEATURES.md), serta [perbaikan RC3 historis](CRASH_REPAIR_RC3.md). Bukti release lama tetap berstatus historis dan tidak diubah menjadi hasil RC1.

Rebuild pulse 2026-10-08 menghasilkan paket 472.338 byte, SHA-256 `e40da43bef40d5737511ff909757bcf68f944be58804108c884434090c7cbc6e`. Semua 68 input source cocok, 35 package tests lulus dan dua fixture sweep dieksekusi/lulus pada kedua ABI LDPlayer. Paket telah dipasang dengan seluruh 11 hash runtime cocok. Hasil startup dan kualifikasi 18 kontrol dilaporkan terpisah pada `.publish/WSM_GAMEPLAY_REPORT_20261008.md`; hasil migrasi awal CMake tidak dipromosikan menjadi pengujian rebuild ini.
