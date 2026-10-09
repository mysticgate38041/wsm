# Build dan pengujian

Jalankan perintah dari root repo. Source produksi berada di `src/wsm-v2`; nama direktori bukan nomor rilis. Versi, stamp, source engine dan daftar fixture ditentukan oleh [build_manifest.json](../src/wsm-v2/scripts/build_manifest.json).

## Toolchain

| Input | Versi/kontrak |
|---|---|
| Host build Android | Windows, PowerShell |
| Android NDK | r27c, exact `27.2.12479018` |
| Java | JDK 17; source/target Java 8 |
| Android SDK | platform 34, build-tools 34.0.0 |
| Native/DEX minimum | Android API 26 |
| CMake dan Ninja | CMake ≥3.22; dibutuhkan oleh kedua backend untuk fixture |
| Python | ≥3.10 |
| Native ABI/STL | x86_64 dan arm64-v8a, `c++_static` |

Toolchain diinstal terpisah. Sesuaikan path pada contoh berikut:

```powershell
git clone https://github.com/mysticgate38041/wsm.git
cd wsm
$buildOptions = @{
    Ndk = 'C:/Android/ndk/27.2.12479018'
    Jdk = 'C:/tools/jdk-17'
    Sdk = 'C:/Android/sdk'
    Python = (Get-Command python).Source
    CMake = 'C:/tools/cmake/bin/cmake.exe'
    Ninja = 'C:/tools/ninja.exe'
    Jobs = 2
    CatalogSnapshot = $true
}
& ./src/wsm-v2/scripts/build.ps1 @buildOptions -NativeBuild ndk-build
# Backend alternatif, dengan gate paket yang sama:
& ./src/wsm-v2/scripts/build.ps1 @buildOptions -NativeBuild CMake
```

`-CatalogSnapshot` memakai snapshot katalog yang sudah di-commit. Tanpa flag tersebut, generator memerlukan database API lokal di `analysis/gt354-api/catalog-final/guardian_tales_354.sqlite`. Clone publik tidak membawa database itu. Kedua mode membaca `premium_menu_design/src/data.ts`; pertahankan path tersebut.

## Hasil dan gate build

| Output | Lokasi |
|---|---|
| ZIP dan checksum | `src/wsm-v2/dist/wsm-v6.3.0-rc1.zip` dan `.zip.sha256` |
| Receipt source/binary | `src/wsm-v2/build/build-receipt.json` |
| Fixture x86_64 | `src/wsm-v2/build/fixtures/x86_64/` |
| Fixture ARM64 | `src/wsm-v2/build/fixtures/arm64-v8a/` |
| Menu DEX | `src/wsm-v2/menu/wsm_menu.dex` |

Build memeriksa lima ELF, export/ABI/dependency, alignment LOAD/RELRO 16 KiB, DEX release, dua suite Java, 11 build/cache tests dan 54 package tests. Empat belas fixture native dikompilasi per ABI. Kompilasi tidak mengeksekusi fixture Android atau game.

Generated inputs hanya ditulis jika isinya berubah. Cache Java/DEX memverifikasi hash input, opsi dan output; kedua suite Java tetap dieksekusi saat cache hit. Source yang berubah selama build membuat receipt ditolak. ZIP lokal dan ZIP CI dapat memiliki hash berbeda karena toolchain/provenance; gunakan checksum dari aset rilis yang sama.

## Pemeriksaan tanpa perangkat

```powershell
python -B src/wsm-v2/scripts/generate_build_config.py --check
python -B -X utf8 -m unittest discover -s src/wsm-v2/tests -p test_catalog.py -v
python -B -X utf8 -m unittest discover -s src/wsm-v2/tests -p test_build.py -v
python -B -X utf8 -m unittest discover -s src/wsm-v2/tests -p test_release.py -v
```

Pemeriksaan generated package dilewati bila paket belum tersedia. Build Android dan job release mewajibkannya lewat `WSM_REQUIRE_RELEASE=1`. Pemeriksaan host ini tidak mengubah modul atau state perangkat.

## Fixture native pada POSIX

```bash
cmake -S src/wsm-v2/jni -B src/wsm-v2/build/host-tests -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build src/wsm-v2/build/host-tests --parallel 2
ctest --test-dir src/wsm-v2/build/host-tests --output-on-failure
```

Host fixture memerlukan Linux/POSIX; executable Android tidak dapat dijalankan langsung di Windows. Eksekusi ARM64 melalui translasi emulator tidak menggantikan pengujian hardware ARM64 fisik.

## Preview UI

[Fixture menu-preview](../src/fixture/menu-preview/README.md) memakai view/controller produksi dan backend simulasi. Build APK, orientasi, serta batas hasil dijelaskan di sana.

Publikasi dan verifikasi aset: [CI/CD](CI_CD.md). Kembali ke [indeks dokumentasi](README.md).
