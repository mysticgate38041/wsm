# Kontribusi dan pemeliharaan

Implementasi produksi berada di `src/wsm-v2`. Gunakan [indeks dokumentasi](docs/README.md) dan [panduan build](docs/BUILDING.md) untuk konteks saat ini; arsip sesi bukan daftar pekerjaan aktif.

## Sumber utama

- Edit `src/wsm-v2/scripts/build_manifest.json` untuk versi, stamp, source engine dan fixture. Jalankan `generate_build_config.py`, lalu periksa `--check`; file generated tidak diedit sendiri-sendiri.
- Katalog memakai `premium_menu_design/src/data.ts` dan snapshot/evidence. Path referensi desain tetap diperlukan oleh generator, termasuk mode `-CatalogSnapshot`.
- Runtime/menu aktif berada di `src/wsm-v2/jni`, `payload` dan `menu`. `src/wsm` adalah POC historis.
- Panduan build berada di `docs/BUILDING.md`; panduan rilis di `docs/CI_CD.md`. README root dan README modul menjadi pintu masuk singkat.

## Pemeriksaan perubahan

Periksa `git diff --check`, tautan dokumen dan generated-input drift. Jalankan pengujian yang sesuai dengan perubahan; perubahan dokumentasi tidak memerlukan pengujian game. CI tetap menjalankan kontrak host dan matrix build Android pada PR ke `main`.

Untuk perubahan native/menu/build, gunakan fixture dan suite pada [panduan build](docs/BUILDING.md), lalu review receipt sumber/binary. Pisahkan hasil unit, preview simulasi, translasi emulator dan hardware/game aktual. `accepted` atau `applied` tidak menggantikan pengukuran efek gameplay.

PR menjelaskan masalah, perilaku akhir, pengujian dan batas hasil. Gunakan branch `codex/…` untuk pekerjaan Codex. Versi baru diterbitkan melalui workflow tag setelah checks lulus; tag/aset lama tidak ditimpa.

## Berkas dan evidence

- Simpan output sementara, laporan lokal yang belum ditinjau dan unduhan kerja di `.publish/`. Hasil build, APK/DEX/ELF, kunci debug dan dependency cache diabaikan Git.
- Simpan panduan yang berlaku di `docs/`; catatan sesi lama di `docs/history/` dengan indeks. Laporan bukti bertanggal boleh tetap di lokasi sumbernya agar rujukan dan provenance terjaga.
- Evidence yang dipublikasikan harus mempunyai konteks versi/artefak, hasil dan batasnya. Jangan mengunggah token, kredensial, data akun atau dump game mentah.
- Jangan memformat ulang evidence yang diikat manifest. `.gitattributes` mempertahankan bytes pada folder evidence; `.editorconfig` mengecualikan snapshot historis dari normalisasi otomatis.
- Dua paket review di `docs/history/` dan backup kecil yang dirujuk laporan merupakan arsip disengaja. Jangan menghapus atau menggabungkannya hanya karena isinya mirip.
- Path/ukuran/hash di inventaris historis adalah observasi saat inventaris dibuat. Perubahan struktur saat ini tidak membenarkan penulisan ulang catatan tersebut.

Notice pihak ketiga dan status lisensi proyek tetap mengikuti [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
