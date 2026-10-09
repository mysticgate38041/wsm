# Fixture Android

| Fixture | Tujuan | Build |
|---|---|---|
| `com.wsm.fixture` (file di direktori ini) | Harness bootstrap/loader native pada proses terisolasi | `build_fixture.sh`, skrip lingkungan Windows/MSYS historis |
| [`com.wsm.preview`](menu-preview/README.md) | View/controller menu produksi dengan backend simulasi | `menu-preview/build_preview.py` |

Fixture bootstrap dan preview UI mempunyai tujuan berbeda. Hasil fixture tidak membuktikan efek fitur pada game. Gunakan [panduan preview](menu-preview/README.md) untuk inspeksi layout/interaksi menu; fixture native mandiri tersedia melalui [build produksi](../../docs/BUILDING.md).

APK, output kompilasi dan kunci debug dibuat lokal serta diabaikan Git. Periksa path toolchain dalam skrip sebelum menjalankan harness lama.
