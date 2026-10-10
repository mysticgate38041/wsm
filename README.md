# WSM — WS Menu

Modul Android dengan loader Zygisk, engine native, menu Java/DEX dan diagnostik runtime. Implementasi aktif berada di [`src/wsm-v2`](src/wsm-v2); nama folder tersebut dipertahankan untuk kompatibilitas build.

**Rilis: [v6.4.0-rc1](https://github.com/mysticgate38041/wsm/releases/tag/v6.4.0-rc1) · prerelease · versionCode 60401**

**Target:** Guardian Tales `com.kakaogames.gdts`, versi `3.54.0` / `423`, Android API ≥26, ABI x86_64 dan arm64-v8a.

[Dokumentasi](docs/README.md) · [Build](docs/BUILDING.md) · [Instalasi dan pemulihan](docs/USAGE.md) · [CI/CD](docs/CI_CD.md) · [Kontribusi](CONTRIBUTING.md)

## Status saat ini

Menu menyediakan **18 kontrol** dengan kategori, pencarian, slider, profil manual dan PANIC. Runtime memakai antrean terbatas, epoch, snapshot status terpisah dan telemetry. Arsitektur/UI v6.3 dijelaskan dalam [laporan modernisasi](docs/MODERNIZATION_V6_3.md).

Verifikasi v6.3: dua backend build, 14 fixture native pada masing-masing ABI di LDPlayer, dua suite Java, serta 54 package + 9 catalog + 11 build/cache tests. Preview Android menguji ON/OFF seluruh 18 kontrol dengan backend simulasi. ARM64 pada emulator memakai translasi; efek gameplay menyeluruh, hardware ARM64 fisik dan peningkatan FPS/CPU/RAM belum terverifikasi penuh.

Desain memuat **47 fitur**: 14 `partial`, 2 `prototype`, 16 `not_implemented`, 10 `authority_unverified` dan 5 `target_absent`. Katalog tetap `completed=0` / `INCOMPLETE_47_FEATURE_SCOPE`. ACK `applied` menunjukkan hasil konfigurasi/backend, bukan bukti seluruh efek gameplay. Lihat [matriks fitur](docs/FEATURES.md) dan [laporan runtime 8 Oktober](docs/RUNTIME_REPORT_20261008.md) untuk hasil game yang bertanggal.

![Menu landscape pada aplikasi preview dengan backend simulasi](docs/images/v6.3-ui-landscape-on.png)

## Mulai dari sini

| Kebutuhan | Panduan |
|---|---|
| Mengunduh ZIP, checksum dan provenance | [GitHub Releases](https://github.com/mysticgate38041/wsm/releases) |
| Membangun dari clone tanpa dump eksternal | [Toolchain dan build snapshot](docs/BUILDING.md) |
| Memasang, mengoperasikan atau memulihkan modul | [Panduan operasi](docs/USAGE.md) |
| Memahami runtime dan menu | [Modernisasi v6.3](docs/MODERNIZATION_V6_3.md), [migrasi arsitektur v6.2](docs/ARCHITECTURE_V3.md) |
| Memeriksa bukti dan keterbatasan | [Indeks dokumentasi](docs/README.md), [receipt lokal v6.3](docs/validation/v6.3.0-rc1-local.json) |
| Memeriksa build atau menerbitkan versi baru | [GitHub Actions](https://github.com/mysticgate38041/wsm/actions/workflows/wsm.yml), [prosedur CI/CD](docs/CI_CD.md) |

## Struktur repo

| Lokasi | Fungsi |
|---|---|
| [`src/wsm-v2`](src/wsm-v2) | Source produksi, menu, installer, manifest, build dan tests |
| [`src/fixture`](src/fixture) | Fixture Android; preview menu menggunakan backend simulasi |
| [`src/wsm`](src/wsm) | POC historis; tidak masuk build rilis aktif |
| [`premium_menu_design`](premium_menu_design) | Referensi React dan input desain untuk generator katalog |
| [`docs`](docs/README.md) | Panduan terkini, laporan rilis, validasi dan indeks arsip |
| [`docs/history`](docs/history/README.md) | Catatan sesi dan paket review historis |
| [`analysis/dump-source-audit`](analysis/dump-source-audit/README.md) | Inventaris dan provenance audit dump |
| [`.github/workflows/wsm.yml`](.github/workflows/wsm.yml) | Pengujian, matrix build dan publikasi prerelease |

Dump mentah, APK/library game, toolchain, database API lokal, kunci debug dan hasil build tidak dibundel. Beberapa backup dan log historis yang dirujuk laporan tetap disimpan sebagai evidence. ZIP rilis tersedia melalui GitHub Releases. File kerja sementara berada di `.publish/` dan diabaikan Git; lihat [aturan berkas dan evidence](CONTRIBUTING.md#berkas-dan-evidence).

Repo ini publik. Status lisensi proyek dan notice komponen tetap tercantum dalam [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
