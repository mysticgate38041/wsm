# WSM v2 — paket review dan preflight

**Ini bukan ZIP modul yang dapat dipasang.** Isinya rancangan modernisasi dan pemeriksa host read-only.

Mulai dari `WSM_V2_MODERNIZATION.md`. Dokumen itu memuat audit source dengan lokasi baris, arsitektur yang diusulkan, kontrak lifecycle/ABI/UI, seluruh backlog fitur dari lampiran, gate uji, dan sumber primer.

## Isi

- `WSM_V2_MODERNIZATION.md`: spesifikasi dan audit.
- `tools/wsm_preflight.py`: program stdlib Python; memeriksa host/source/ELF/ADB tanpa memasang modul.
- `tests/test_preflight.py`: unit/regression tests dan mutation smoke test.
- `evidence/preflight-2026-10-06.json`: hasil pemeriksaan proyek asli.
- `evidence/tests-final.txt`, `evidence/tests-summary.json`: hasil test yang dijalankan.
- `VERIFICATION.md`: batas kesimpulan dan pemeriksaan akhir.

## Jalankan

Python 3.10+ dibutuhkan secara sintaks; sesi ini diuji pada Python 3.14.7 di Windows. Tidak membutuhkan pip.

```bash
python -m unittest discover -s tests -v
python tools/wsm_preflight.py --project "C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm" --ndk "D:/Android/ndk/android-ndk-r27c" --adb "D:/_migration/profile/Downloads/platform-tools-latest-windows/platform-tools/adb.exe" --output evidence/preflight-next.json
```

- Exit `0`: preflight host lulus, **bukan** runtime game lulus.
- Exit `2`: blocker ditemukan dan laporan tersimpan.
- Exit `1`: laporan tidak dapat dibuat.
- Nama laporan existing ditolak untuk mencegah menimpa bukti.
- ADB hanya menjalankan `devices -l`; daemon host mungkin dinyalakan. Tidak ada adb shell/root/install/push/reboot.

Batas parser: ELF64 little-endian, dua ABI, header dan PT_LOAD. Bukan validator ELF menyeluruh. Current NDK discovery hanya mendukung layout Windows/Linux; platform lain belum diuji.

## Hasil sesi

36 test methods lulus; satu di antaranya menguji 1.000 mutasi byte deterministik. Proyek asli: empat binary yang diharapkan belum ada, perangkat ADB siap tidak ditemukan, runtime **NOT_TESTED**. Source asli tidak dimodifikasi.

Tidak ada engine v2, fitur gameplay, atau UI runtime yang diklaim sudah diimplementasikan dalam paket ini. Rancangan ditulis untuk membuat langkah implementasi berikutnya dapat diuji, bukan memberi jaminan kosong.
