# Dokumentasi WSM

Mulai dari [README repo](../README.md). Implementasi aktif adalah WSM **v6.4.0-rc2** di `src/wsm-v2`. Laporan bertanggal menjelaskan kondisi versi dan artefak yang diuji saat itu.

## Panduan terkini

| Dokumen | Isi |
|---|---|
| [Build dan pengujian](BUILDING.md) | Toolchain, clone tanpa dump, dua backend, fixture dan output |
| [Instalasi dan pemulihan](USAGE.md) | ZIP/checksum, pemasangan, state runtime dan pemulihan |
| [CI/CD](CI_CD.md) | Checks, provenance, trigger tag dan prosedur publikasi |
| [Modernisasi v6.3](MODERNIZATION_V6_3.md) | Runtime, controller/view, lifecycle, telemetry dan hasil pengujian |
| [Command produksi](../src/wsm-v2/docs/COMMANDS_V6.md) | Command, rentang, completion, epoch dan transport |
| [Matriks fitur](FEATURES.md) | Katalog 47, kontrol yang tersedia dan batas kualifikasi |
| [Kontribusi](../CONTRIBUTING.md) | Sumber utama, perubahan generated files dan penyimpanan evidence |

## Rilis dan bukti pengujian

- [Release notes v6.4.0-rc2](RELEASE_v6.4.0_rc2.md) dan [receipt lokal](validation/v6.4.0-rc2-local.json); riwayat: [v6.4.0-rc1](RELEASE_v6.4.0_rc1.md), [v6.3.0-rc1](RELEASE_v6.3.0_rc1.md).
- [Release notes v6.2.0-rc1](RELEASE_v6.2.0_rc1.md) dan [arsitektur migrasi v6.2](ARCHITECTURE_V3.md).
- [Runtime 8 Oktober 2026](RUNTIME_REPORT_20261008.md): observasi game beserta keterbatasannya.
- [v6.1.0-rc3](releases/v6.1.0-rc3.md) dan [laporan crash RC3](CRASH_REPAIR_RC3.md).
- [v6.1.0-rc2](releases/v6.1.0-rc2.md), [audit dump RC2](../src/wsm-v2/docs/ORIGINAL_DUMP_AUDIT.md), [finalisasi desain 47](../src/wsm-v2/docs/FINALIZATION_47.md).
- [Baseline v6.0](../src/wsm-v2/docs/RELEASE_VALIDATION.md) dan [evidence perangkat](../src/wsm-v2/evidence).

Checksum lokal/historis berlaku untuk artefak pada laporan tersebut. Aset publik harus dicocokkan dengan checksum dan provenance dari [release yang sama](https://github.com/mysticgate38041/wsm/releases). Fixture dan preview UI tidak otomatis membuktikan efek gameplay.

## Konteks dan arsip

- [Indeks arsip sesi](history/README.md): roadmap, resume, review/preflight dan catatan RE.
- [Studi tiga root workspace](context/PROJECT_CONTEXT.md) dan [inventaris historis](context/PROJECT_CONTEXT_INVENTORY.json).
- [Inventaris audit dump](../analysis/dump-source-audit/README.md): provenance sumber eksternal.
- [Referensi desain React](../premium_menu_design/README.md): juga input generator katalog.
- [POC lama](../src/wsm/README.md) dan [fixture Android](../src/fixture/README.md).

Path lokal yang dicatat dalam inventaris/evidence merupakan data historis. Berkas tersebut tidak diubah agar seolah-olah menggambarkan checkout saat ini. Dua paket review yang mirip dipertahankan karena mempunyai manifest dan bukti masing-masing.
