# Build, CI/CD dan publikasi WSM RC2

## Workflow dan checks

File: `.github/workflows/wsm.yml`, nama **WSM CI and Release**.

| Job | Runner | Checks / hasil |
|---|---|---|
| `native-tests` | Ubuntu 24.04 | Compile/execute Runtime, Patch, Binding, Reloc; fail-fast per executable |
| `android-build` | Windows 2022 | NDK exact; snapshot generation/diff; sembilan catalog tests; lima ELF; Java/DEX; empat executable Android; receipt; 16 package tests; artifact |
| `release` | Ubuntu 24.04 | Membutuhkan kedua job sukses; hanya tag; guard exact RC2; verify ZIP/checksum; private prerelease dan tiga aset |

Native host tests menguji algoritma, queue/race, binding contracts, patch alias/rollback dan relokasi. Host test bukan eksekusi ARM64 instruction stream. ARM64 emitter, native bridge, UI Android, installer dan gameplay harus diuji dengan perangkat/fixture yang sesuai. Bukti manual tersimpan secara terpisah di `src/wsm-v2/evidence`.

## Pin dan dependensi

| Action | Commit yang diperiksa dari repo resmi |
|---|---|
| actions/checkout v5 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| actions/setup-java v5 | `b6effb05e454b25005698d916606bdc6ffcbf961` |
| actions/setup-python v6 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| actions/upload-artifact v4 | `ea165f8d65b6e75b540449e92b4886f43607fa02` |
| actions/download-artifact v5 | `634f93cb2916e3fdff6788551b99b062d0335ce0` |

JDK major 17 dan Python minor 3.12 berasal dari action setup. NDK `27.2.12479018`, platform 34/build-tools 34.0.0 exact. Pin action diperoleh dari ref resmi pada saat publikasi; upgrade perlu review dan rerun. Runner image serta patch JDK/Python dapat berubah sehingga reproducibility tidak berarti checksum lintas semua environment selalu identik. Referensi resmi: [Checkout](https://github.com/actions/checkout), [Setup Java](https://github.com/actions/setup-java), [Setup Python](https://github.com/actions/setup-python), [Artifacts](https://github.com/actions/upload-artifact).

## Prosedur rilis

1. Pastikan main memiliki katalog/generated sources konsisten dan seluruh checks lulus.
2. Review status qualification, notes, target package/version/code, versionCode module, stamp engine/helper/package, paths artifact dan tag guard. Semua harus menyatakan versi yang sama.
3. Buat tag annotated pada commit yang disetujui dan push tag. Rilis RC2 menggunakan `v6.1.0-rc2`.
4. Tunggu kedua job build/test dan publish sukses. CD membuat prerelease; aset tidak diambil dari direktori lokal yang tidak memiliki receipt.
5. Periksa visibility repo private, prerelease flag, commit tag, tiga aset dan checksum. Unduh aset dan jalankan package verifier; simpan run URL serta checksum di receipt publikasi.

```powershell
git tag -a v6.1.0-rc2 -m 'WSM 6.1.0 RC2 — verified build, incomplete 47-feature scope'
git push origin v6.1.0-rc2
gh run list --repo mysticgate38041/wsm --workflow wsm.yml
gh release view v6.1.0-rc2 --repo mysticgate38041/wsm
```

Trigger `workflow_dispatch` pada main memvalidasi build tanpa publish. Workflow khusus RC2 menolak tag lain sampai semua input rilis diperbarui. Tidak ada deploy/reboot otomatis ke perangkat Android. CD pada proyek ini berarti pengiriman ZIP ke GitHub Release, bukan pemasangan ke game.

## Izin, artifacts dan retry

Token hanya dapat membaca contents pada kedua job build. Publish job mendapat contents-write. Action download hanya memakai artifact dari run yang sama. Tidak ada token akun, debug keystore atau dump mentah di repo. Artifact CI disimpan 30 hari; aset release tetap mengikuti penyimpanan release GitHub.

Timeout host 10 menit, Android 35 menit, release 10 menit. Run main/PR yang digantikan dapat dibatalkan; run tag tidak dibatalkan otomatis. Jika source/toolchain gagal, perbaiki commit dan ulangi checks sebelum tag. Jangan menandai check sukses secara manual untuk melewati gate.

Jika release sudah ada, workflow mencoba upload tanpa `--clobber`; file bernama sama tidak diganti. Kegagalan publish setelah sebagian aset terkirim perlu diperiksa: cocokkan digest lalu unggah hanya aset yang hilang. Untuk perubahan binary/source setelah tag diterbitkan, buat versi/tag baru, bukan menulis ulang release lama. `gh release delete`, force-push atau penghapusan tag bukan bagian otomatis workflow.

## Snapshot provenance dan dump eksternal

CI membangun dengan `-CatalogSnapshot`. Snapshot diperiksa terhadap hash dan seluruh scalar desain asli, 47 ID berurutan, status/reason/backend, selector evidence dan gate UNVERIFIED. Snapshot tidak mengakses SQLite, tidak memverifikasi ulang isi external dump, dan tidak menganggap evidence yang tersimpan sebagai acceptance gameplay.

Untuk meregenerasi evidence dari dump asli, sediakan SQLite di path yang didokumentasikan, jalankan generator default, review JSON/Java/header, rerun build/checks dan commit hasilnya. Hash API dan audit pada JSON menautkan snapshot ke input lokal; inventaris serta 155 provenance checks berada di `analysis/dump-source-audit`.

## Release bytes dan riwayat validasi

ZIP memiliki manifest per-entry serta release.json source hashes. `ci-build-provenance.json` menambahkan commit, ref, run ID/attempt, toolchain dan binary receipt. File `.sha256` mencocokkan ZIP yang diterbitkan. Log lokal lama merekam checksum sebelum penambahan CI/snapshot mode; nilainya tidak harus sama dengan binary CI terbaru. Tidak ada perubahan backend gameplay dalam pekerjaan publikasi ini; perubahan source terbatas pada build portability, generator/qualification tests, temporary path test, dan line-ending canonicalization.
