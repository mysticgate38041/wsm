# Validasi persiapan publikasi GitHub — 7 Oktober 2026

Build lokal setelah penambahan snapshot CI, catalog tests, test temporary-directory override dan canonical LF: **PASS**. Tidak ada perubahan backend gameplay. SHA-256 ZIP lokal tahap ini: `91f94f6e416915fb2923f0f1ee2e1024cb25844baac0de07f3e37540d98c4959`.

- Sembilan catalog snapshot tests PASS.
- Lima ELF dan Java/DEX berhasil dibangun/diverifikasi; ControlState test PASS.
- Enam belas package regression tests PASS.
- Empat unit Android dijalankan ulang dan PASS; lihat `android-units.json`.
- Delapan kasus installer harness PASS; lihat `installer.json`.
- Tidak ada pemasangan modul kandidat atau perubahan proses game.

Release GitHub berasal dari build CI pada commit/tag, dengan checksum serta `ci-build-provenance.json` tersendiri. Log/synthetic ARM64 evidence sebelumnya tetap historis pada `../dump-finalization-20261007`; tahap publikasi ini tidak mengulang uji gameplay atau mengaudit dump eksternal di CI.
