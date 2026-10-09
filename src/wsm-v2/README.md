# WSM — implementasi aktif

Source produksi **v6.3.0-rc1** berada di direktori ini. Nama `wsm-v2` dipertahankan untuk kompatibilitas path; versi aktif ditentukan oleh [manifest build](scripts/build_manifest.json).

[README repo](../../README.md) · [Build dan pengujian](../../docs/BUILDING.md) · [Instalasi/pemulihan](../../docs/USAGE.md) · [CI/CD](../../docs/CI_CD.md)

| Direktori | Isi |
|---|---|
| `jni/` | Loader, engine, runtime state, serializer, resolver dan shared protocol |
| `payload/` | Helper ARM64 |
| `menu/` | View Android, controller, backend, scheduler, profiles dan snapshot |
| `module-template/` | Installer dan metadata modul |
| `scripts/` | Generator, build, package verifier dan diagnostik ADB |
| `tests/` | Fixture native, Java, Python dan harness perangkat |
| `features/` | Katalog desain beserta status implementasi/provenance |
| `docs/`, `evidence/` | Referensi teknis dan laporan pengujian bertanggal |

Menu menyediakan 18 kontrol. Katalog 47 tetap berstatus incomplete; hasil build atau ACK bukan bukti efek gameplay penuh. Lihat [modernisasi v6.3](../../docs/MODERNIZATION_V6_3.md), [matriks fitur](../../docs/FEATURES.md), dan [command reference](docs/COMMANDS_V6.md).

Laporan [RC2](docs/ORIGINAL_DUMP_AUDIT.md), [finalisasi desain 47](docs/FINALIZATION_47.md), dan [baseline 6.0](docs/RELEASE_VALIDATION.md) merupakan riwayat bertanggal. Nama ZIP, jumlah tes dan klaim pemasangan di sana berlaku pada versi yang dilaporkan. [Indeks dokumentasi](../../docs/README.md) memisahkan panduan terkini dari riwayat tersebut.
