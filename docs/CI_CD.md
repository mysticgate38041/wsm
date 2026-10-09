# Build, CI/CD dan publikasi WSM 6.3.0 RC1

Workflow `.github/workflows/wsm.yml` membangun source modular untuk rilis `v6.3.0-rc1` (module versionCode `60301`). Build lokal, native fixture, remote CI dan gameplay merupakan bukti berbeda; satu jenis hasil tidak otomatis memenuhi jenis lain.

## Workflow dan checks

| Job | Runner | Checks / hasil yang dikonfigurasi |
|---|---|---|
| `native-tests` | Ubuntu 24.04 | CMake/Ninja compile dan CTest execute empat belas fixture POSIX; package rejection/source receipt tests |
| `android-build` | Windows 2022, matrix ndk-build/CMake | NDK exact, snapshot generation/diff, catalog tests, lima ELF + DEX, dua suite Java, empat belas fixture Android per ABI, receipt, package tests dan artifact |
| `release` | Ubuntu 24.04 | Membutuhkan native-tests dan seluruh matrix sukses; exact tag guard, verifikasi ZIP/checksum/source/provenance, prerelease publik dan tiga aset dari ndk-build |

Fixture native: runtime, patch, binding, reloc, sweep, restore, flags, resolver, dispatcher, worker, pool, bus_event, status dan bootstrap. Host execution menguji algoritma/kontrak, bukan menjalankan instruction stream ARM64 atau pipeline Unity/game. Android build mengompilasi 28 executable fixture untuk dua ABI; execution memerlukan runtime Android yang sesuai dan dicatat tersendiri. Gameplay, UI Android, native bridge, installer dan ARM64 fisik memerlukan qualification terpisah.

Trigger berlaku untuk push `main`, pull request ke `main`, tag `v*` dan `workflow_dispatch`. Dispatch main membangun tanpa publish. Status eksekusi tersedia pada [GitHub Actions](https://github.com/mysticgate38041/wsm/actions/workflows/wsm.yml).

## Pin dan dependensi

| Input | Kontrak |
|---|---|
| NDK | r27c, exact `27.2.12479018`; source.properties diperiksa |
| Android native | API 26, ABI x86_64/arm64-v8a, STL `c++_static` |
| Java/DEX | JDK 17, platform 34, build-tools 34.0.0, DEX min API 26 |
| Python | ≥3.10 lokal; setup-python 3.12 pada CI |
| CMake/Ninja | CMake ≥3.22; CI memasang SDK CMake 3.22.1, lokal migrasi memakai CMake 3.31.8/Ninja 1.12.1 |
| Zygisk | API v4 header exact SHA-256 pada build.ps1; notice tetap dipertahankan |
| ELF | Entry/export exact, tanpa libc++_shared/TEXTREL, LOAD/RELRO alignment 16 KiB |

| Action | Commit yang dipakai workflow |
|---|---|
| actions/checkout v5 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| actions/setup-java v5 | `b6effb05e454b25005698d916606bdc6ffcbf961` |
| actions/setup-python v6 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| actions/upload-artifact v4 | `ea165f8d65b6e75b540449e92b4886f43607fa02` |
| actions/download-artifact v5 | `634f93cb2916e3fdff6788551b99b062d0335ce0` |

Pin action dan toolchain adalah input source saat ini. Runner image serta patch JDK/Python dapat berubah; reproducibility tidak menjamin checksum sama lintas environment. Upgrade pin perlu review dan rerun. Sumber action resmi: [Checkout](https://github.com/actions/checkout), [Setup Java](https://github.com/actions/setup-java), [Setup Python](https://github.com/actions/setup-python), [Artifacts](https://github.com/actions/upload-artifact).

## Build lokal dan portable units

ndk-build tetap default. Contoh PowerShell:

```powershell
& ./src/wsm-v2/scripts/build.ps1 -Ndk 'C:/Android/ndk/27.2.12479018' `
  -Jdk 'C:/tools/jdk-17' -Sdk 'C:/Android/sdk' `
  -Python 'C:/tools/python/python.exe' -Jobs 2 -CatalogSnapshot

& ./src/wsm-v2/scripts/build.ps1 -Ndk 'C:/Android/ndk/27.2.12479018' `
  -Jdk 'C:/tools/jdk-17' -Sdk 'C:/Android/sdk' `
  -Python 'C:/tools/python/python.exe' -Jobs 2 -CatalogSnapshot `
  -NativeBuild CMake -CMake 'C:/tools/cmake/bin/cmake.exe' `
  -Ninja 'C:/tools/ninja.exe'
```

Dua backend menjalankan gate package yang sama. Kedua backend memerlukan CMake/Ninja untuk fixture; manifest source/version diperiksa sebelum build. Cache Java/DEX memverifikasi hash seluruh input/output dan selalu mengeksekusi kedua suite Java. CMake output ABI disalin ke lokasi package standar; receipt dibentuk dari binary/source yang baru diperiksa. Hasil final adalah `src/wsm-v2/dist/wsm-v6.3.0-rc1.zip` dan `.zip.sha256`. Keluaran build juga menyertakan fixture di `build/fixtures/x86_64` serta `build/fixtures/arm64-v8a`.

Untuk POSIX/Linux dengan compiler yang tersedia:

```bash
cmake -S src/wsm-v2/jni -B src/wsm-v2/build/host-tests -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build src/wsm-v2/build/host-tests --parallel 2
ctest --test-dir src/wsm-v2/build/host-tests --output-on-failure
```

CMake host sengaja menolak Windows karena fixture memakai API POSIX. Android cross-compile merupakan jalur yang berbeda; executable tidak bisa dijalankan langsung sebagai program Windows. Fixture Android boleh dijalankan pada perangkat/emulator terisolasi tanpa memasang modul aktif ke game; ABI dan execution log harus dicocokkan dengan binary hash.

## Snapshot provenance dan receipt

CI memakai `-CatalogSnapshot`. Snapshot diperiksa terhadap hash/scalar desain asli, 47 ID berurutan, status/reason/backend, selector evidence dan qualification UNVERIFIED. Mode ini tidak membuka SQLite atau mengaudit ulang dump eksternal. Generator default memerlukan SQLite lokal `analysis/gt354-api/catalog-final/guardian_tales_354.sqlite` dengan mode read-only dan mencatat hash input aktual.

Checkpoint/receipt mencakup source modular, termasuk source assembly, include fragment dan CMakeLists. Missing required input, receipt yang tidak cocok, atau perubahan source setelah checkpoint menyebabkan gate gagal. Release JSON dan manifest per-entry tidak menggantikan qualification gameplay. `ci-build-provenance.json` mencatat commit/ref/run, backend, toolchain dan receipt; boolean runtime/dump yang belum dijalankan tetap false. Job release mencocokkan seluruh hash source dengan checkout tag, commit/ref/run dengan provenance, serta receipt binary dengan isi ZIP.

## Prosedur rilis

1. Pastikan generated source/snapshot konsisten dan seluruh checks untuk commit kandidat selesai.
2. Review qualification, notes, exact target identity, module versionCode, stamp engine/helper/package, artifact path dan tag guard. Semuanya harus menyatakan versi yang sama.
3. Buat tag annotated pada commit yang disetujui dan push tag `v6.3.0-rc1`.
4. Tunggu native-tests, seluruh matrix Android dan release job sukses. Aset release diambil dari artifact `wsm-release-candidate-ndk-build`; backend CMake tetap menjadi gate build tersendiri.
5. Cocokkan repo visibility, prerelease flag, tag commit, ZIP/checksum/provenance dan digest hasil unduhan. Catat run URL/digest dalam receipt publikasi.

```powershell
git tag -a v6.3.0-rc1 -m 'WSM 6.3.0 RC1 — modular build, incomplete 47-feature scope'
git push origin v6.3.0-rc1
gh run list --repo mysticgate38041/wsm --workflow wsm.yml
gh release view v6.3.0-rc1 --repo mysticgate38041/wsm
```

Command di atas menerbitkan commit yang telah lulus checks. Workflow menolak tag lain sampai stamp, metadata, tests, paths dan notes diperbarui secara konsisten. Notes aktif berada di [RELEASE_v6.3.0_rc1.md](RELEASE_v6.3.0_rc1.md). CD mengirim aset GitHub Release; workflow tidak memasang modul, menjalankan game atau me-reboot perangkat.

## Izin, artifact dan retry

Token default `contents: read`; hanya job release memakai `contents: write`. Checkout tidak menyimpan credential. Tidak dibutuhkan PAT tambahan, dump mentah atau kunci debug lokal. Artifact ditahan 30 hari dan action download mengambil artifact dari run yang sama.

Timeout native-tests 10 menit, setiap matrix Android 35 menit, release 10 menit. Main/PR superseded dapat dibatalkan; tag run tidak dibatalkan otomatis. Failed gate diperbaiki pada source/toolchain dan diulang; tidak dilewati dengan menandai check sukses secara manual.

Upload release existing tidak memakai `--clobber`. Jika publish hanya mengirim sebagian aset, cocokkan digest lalu unggah aset yang belum ada. Perubahan source/binary setelah release memerlukan versi/tag baru; tidak ada penghapusan release, force-push atau penulisan ulang tag otomatis.

## Modernisasi v6.3

Graph CMake bersama membangun 14 fixture per ABI dan menjalankan fixture yang sama pada host POSIX. CI juga memeriksa generated-input drift serta 11 regression test cache/build. D8 memakai `--release`; hasil Java tetap divalidasi pada setiap build. Bukti eksekusi Android dan preview UI dicatat terpisah dalam [laporan modernisasi](MODERNIZATION_V6_3.md).

## Hasil migrasi awal dan riwayat

Pada migrasi awal 8 Oktober 2026, build backend ndk-build dan CMake lulus gate lima ELF + DEX dan Java state. Sebanyak 35 package regression tests serta sembilan catalog tests lulus. Sebelas fixture native per ABI dieksekusi pada LDPlayer: x86_64 11/11 dan arm64-v8a melalui translation 11/11 lulus, total 22/22. Kernel host fixture adalah x86_64 dengan page size 4 KiB. Installer harness terisolasi lulus 8/8 tanpa memasang modul.

Fixture patch ARM64 translation membandingkan permission sesudah restoration dengan permission aktual sebelum patch; requested RX dapat teramati sebagai R di lingkungan ini. Hasil ini menguji kontrak restoration data/permission aktual dan tidak membuktikan eksekusi trampoline ARM64 pada hardware fisik atau behavior perangkat ARM64 fisik/16 KiB.

ZIP migrasi awal berukuran 470.267 byte dengan SHA-256 `c4a59c8abe207647a6a826f904c5829e834225fbecb9e74c53824d3ad4372470`. Ini artefak lokal historis, bukan checksum aset rilis CI. Pengujian lanjutan memakai paket berbeda dan dijelaskan dalam [laporan runtime](RUNTIME_REPORT_20261008.md). Suite rilis v6.2 historis mencakup 54 package tests (53 fixture/receipt tests dan satu generated-package test), sembilan catalog tests dan 12 native tests. Generated-package test dilewati pada job host tanpa paket, dan wajib lulus pada build Android serta job release.

Dokumen [RC3 crash repair](CRASH_REPAIR_RC3.md) dan release lama menyimpan hasil serta keterbatasan saat itu. Checksum lama bukan checksum binary RC1. Katalog tetap `completed=0` dan `INCOMPLETE_47_FEATURE_SCOPE`; metadata/API, compilation, native fixtures dan ACK tidak membuktikan seluruh efek desain pada target game.
