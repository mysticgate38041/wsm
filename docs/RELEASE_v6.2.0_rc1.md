# WSM 6.2.0 RC1 — modular Android build

Prerelease `v6.2.0-rc1`, module versionCode `60201`. Target exact: `com.kakaogames.gdts` 3.54.0 / code 423, Android API ≥26, ABI x86_64 dan arm64-v8a.

**Belum operasional penuh untuk seluruh efek 18 kontrol.** Rilis ini membawa arsitektur modular, perbaikan restoration dan gate build/publikasi. Hasil build dan ACK command tidak membuktikan kualifikasi gameplay.

## Perubahan

- Runtime, resolver, dispatcher, observed flags, worker scheduling dan retained trampoline pool dipisahkan menjadi modul dengan kontrak dan fixture tersendiri.
- Antrean command bounded, notification generation dan epoch mencegah completion lama dianggap sebagai hasil sesi baru. PANIC menjaga status restoration yang belum pasti.
- Pinned GC ownership menjaga objek selama opsi milik WSM diterapkan/dipulihkan; overlap opsi dan cleanup memiliki fixture regression.
- Lima ELF membatasi ekspor publik, termasuk pemeriksaan simbol GLOBAL dan WEAK dari runtime C++ statis.
- Verifier menolak ZIP dengan path tidak aman, symlink, entry/manifest duplikat, metadata salah atau source/binary receipt yang tidak cocok. Validasi serial ADB dan shell quoting dari main dipertahankan.
- Build mendukung ndk-build dan CMake/Ninja. CI menjalankan 12 native tests pada Linux, sembilan catalog tests, Java state checks, build dual ABI dan 54 package tests pada masing-masing backend Android. Sebanyak 12 fixture Android per ABI dikompilasi; eksekusi perangkat dicatat terpisah.

## Aset dan CI/CD

Tag hanya diterbitkan setelah checks commit kandidat lulus. Workflow tag mengulangi native tests dan kedua backend Android sebelum menerbitkan aset dari ndk-build:

- `wsm-v6.2.0-rc1.zip`
- `wsm-v6.2.0-rc1.zip.sha256`
- `ci-build-provenance.json`

CD memverifikasi ZIP/checksum, hash source terhadap checkout tag, commit/ref/run provenance, dan receipt binary. Aset CI dibangun dari commit tag; checksum build lokal historis tidak dipakai sebagai checksum rilis ini. Status dan log tersedia di [GitHub Actions](https://github.com/mysticgate38041/wsm/actions/workflows/wsm.yml).

## Hasil runtime lokal dan batasnya

Sesi LDPlayer 8 Oktober 2026 memakai paket lokal SHA-256 `ae0dcb92d5147133d69e3351dbc5032999dbb15f831814052b4e355ca0684041` (443.218 byte), sebelum penggabungan hardening packaging/CLI. Semua 18 kontrol menerima ON/OFF, overlap opsi dan PANIC berulang dibersihkan, serta God/Opsi dan HP Protection memiliki observasi efek terbatas. Fixture restoration 20 kasus lulus pada kedua ABI LDPlayer. Ini bukti paket lokal tersebut; aset CI belum diuji ulang dalam gameplay.

Pulse damage, ONEHP/OHK, Freeze AI dan beberapa efek lain belum memenuhi kualifikasi penuh. Startup masih dapat berhenti dengan SIGKILL dan membutuhkan pembukaan ulang; penyebabnya belum diketahui. Hardware ARM64 fisik, runtime 16 KiB, lifecycle dengan fitur aktif dan performa juga belum dikualifikasi.

Katalog desain tetap memuat 47 fitur dengan `completed=0` dan `INCOMPLETE_47_FEATURE_SCOPE`; cakupan pengguna saat ini adalah 18 kontrol menu. Status itu tetap dipertahankan agar fitur yang belum terbukti tidak ditampilkan sebagai selesai.

Lihat [laporan runtime](https://github.com/mysticgate38041/wsm/blob/v6.2.0-rc1/docs/RUNTIME_REPORT_20261008.md), [arsitektur](https://github.com/mysticgate38041/wsm/blob/v6.2.0-rc1/docs/ARCHITECTURE_V3.md), [CI/CD](https://github.com/mysticgate38041/wsm/blob/v6.2.0-rc1/docs/CI_CD.md) dan [matriks fitur](https://github.com/mysticgate38041/wsm/blob/v6.2.0-rc1/docs/FEATURES.md).
