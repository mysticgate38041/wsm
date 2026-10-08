# Laporan runtime lokal — 8 Oktober 2026

Laporan historis ini merangkum uji paket lokal `ae0dcb92…` sebelum penggabungan hardening ZIP/CLI dan publikasi CI. Aset GitHub dibangun ulang dari commit tag dan memiliki checksum/provenance sendiri; uji gameplay di bawah tidak dianggap sebagai pengujian aset CI tersebut.

**Status: belum operasional penuh.** Target pengguna adalah 18 kontrol menu. Semua 18 menerima ON dan OFF dengan terminal `applied`, tetapi hasil ini tidak membuktikan 18 efek gameplay. Laporan ini menggantikan draft sebelumnya dan mempertahankan bukti build terdahulu secara terpisah.

## Paket dan sesi yang benar-benar diuji

- Paket lokal yang diuji: `src/wsm-v2/dist/wsm-v6.2.0-rc1.zip`, 443.218 byte, SHA-256 `ae0dcb92d5147133d69e3351dbc5032999dbb15f831814052b4e355ca0684041`.
- LDPlayer instance 0, ADB `emulator-5554`, Android 34 x86_64 dengan Houdini ARM64, halaman 4 KiB; Guardian Tales 3.54.0 / code 423.
- Seluruh 11 file modul terpasang cocok. Boot `fb1808f6-a60d-4d27-9e10-016ce02ab46c`, PID 2747. Satu pembukaan awal berhenti SIGKILL; pembukaan kedua mencapai lobby dan gameplay. Penyebab SIGKILL belum teratribusi.
- Pengguna menjelaskan RC2 biasanya perlu dibuka ulang beberapa kali. RC2 resmi dan build sebelum perbaikan juga pernah gagal startup; pola tersebut tidak membuktikan penyebab tertentu. Perbaikan ABI tidak boleh diklaim telah menyelesaikan startup.
- Stage uji: replay **Hutan Kanterbury Bagian 5** di World 1, monster Lv14. UI menyatakan masuk ulang tidak memakai stamina. Pemain tunggal, hero diskalakan ke Lv32.
- Fault counter tetap 1 dari baseline sampai akhir; ini bukan hasil zero-fault.

## Perbaikan yang sudah masuk paket

1. Ekspor runtime C++ statis disembunyikan dan gate memeriksa simbol GLOBAL maupun WEAK. Loader, engine dan helper mempertahankan hanya ekspor publik yang diharapkan.
2. Ledger opsi memperoleh pinned GC handle sebelum perubahan, memeriksa identitas target sebelum operasi, dan menahan status tidak pasti setelah invalidasi/baseline loss. PANIC tidak lagi boleh menyatakan selesai jika pemulihan belum pasti.
3. ON/OFF resource dan pulse memakai command terkorrelasi/epoch. Pulse Power/Critical telah memiliki konfigurasi dan fixture, tetapi efek nyata pada monster masih belum terbukti.

Build lokal yang diuji dengan ndk-build menghasilkan lima ELF+DEX, menjalankan 52 Python checks dan Java state checks, serta mengompilasi 12 fixture Android per ABI. Fixture restoration 20 kasus lulus pada x86_64 dan ARM64 translation; dua counterexample versi lama direproduksi. Hasil migrasi awal 22 fixture dan build CMake disimpan sebagai bukti historis, bukan diklaim ulang sebagai eksekusi seluruh fixture pada paket terbaru. Pada sesi gameplay ini, hardware ARM64 fisik/runtime 16 KiB dan remote CI belum diuji. Eksekusi CI publikasi dicatat terpisah di GitHub Actions.

## Hasil 18 kontrol

Semua baris memiliki ON/OFF `applied`. Kolom berikut membatasi klaim efeknya.

| Kontrol | Bukti efek dan batas |
|---|---|
| God Mode/Opsi (`god`) | HP 5166 pada dua tangkapan saat ON, sekitar 109 detik terpisah, meski efek hit terlihat. Setelah God OFF dan HP Protection tetap ON, HP kembali turun. |
| HP Protection (`hp`) | Hero terlihat hidup pada HP 1 pukul 22:59:00 dan 23:05:53 WIB. Status terakhir hanya HP aktif. Ini dua observasi, bukan rekaman terus-menerus. |
| Super Armor (`poise`) | Opsi milik WSM diterapkan dan dilepas, termasuk overlap NoDown. Efek knockback/stun belum diisolasi. |
| Status Immunity (`immune`) | Opsi diterapkan/dilepas dan dibersihkan PANIC. Efek status masing-masing belum diuji. |
| Stamina Refill | Perintah berhasil; perubahan meter belum diukur. |
| Mana Refill | Perintah berhasil; perubahan meter belum diukur. |
| Damage Guard (`godmode`) | Hook diterapkan/dipulihkan. Berbeda dari God/Opsi; efek combat belum diisolasi. |
| Speed | ON/OFF dan restoration diterima. Peningkatan berjalan teramati pada build 95b199b3 sebelumnya; belum diukur ulang pada ae0dcb92. |
| Time Scale | Modifier milik WSM muncul saat ON dan hilang saat OFF. Perlambatan gerak teramati pada build 95b199b3 sebelumnya. |
| No Cooldown | Tiga hook diterapkan/dipulihkan; penggunaan skill berulang belum diukur. |
| Critical Damage | Hook diterapkan/dipulihkan; multiplier damage belum diukur. |
| Auto Loot | Tidak ada kandidat drop (`items=0`); efek pengambilan/reward belum diuji. OFF hanya menghentikan permintaan berikutnya. |
| Freeze AI | HP pemain tetap berkurang selama ON. Penghentian seluruh AI tidak terbukti; screenshot tidak membedakan projectile baru dari yang sudah ada. |
| Radius Pulse (`aura`) | Nilai 5 dan 40 serta OFF diterima. Cakupan jarak belum diukur secara independen. |
| ONEHP | Log melaporkan callback nonzero; HP monster tepat 1 tidak terbukti. |
| OHK | Log melaporkan callback nonzero; kematian yang diharapkan tidak teramati. |
| Pulse Power (`dmg`) | Konfigurasi armed; besaran damage belum terbukti. |
| Critical Pulse (`crit`) | Konfigurasi armed; mayoritas diuji bersama dmg sehingga efek critical tersendiri belum terisolasi. |

Callback `MSWEEP targets=N state=3` menunjukkan pemanggilan pipeline selesai, bukan jumlah monster yang HP-nya turun atau mati. Bar HP monster tetap terlihat besar/penuh dalam tangkapan. Karena tidak ada angka HP sebelum/sesudah, laporan ini tidak mengklaim nol damage secara numerik. Dugaan field sender kosong berasal dari pemeriksaan source worker dan belum melewati review kausal lengkap atau retest perbaikan.

## Pemulihan dan kondisi akhir

Uji live menunjukkan mask kepemilikan `1→3→1→0` untuk HP/God dan `52→244→224→0` untuk Super Armor/Immunity. PANIC membersihkan opsi 224, PANIC kosong kedua tetap berhasil, dan PANIC akhir membersihkan opsi HP 1. Hasil akhir: jumlah objek, bit, dan ketidakpastian milik WSM nol, `restorePending=false`, `timeModifierOwned=false`, seluruh 18 kontrol OFF. Uji ini mencakup satu pemain pada scene stabil; kegagalan GC/scene dan baseline-loss tetap dibuktikan fixture.

Game dikembalikan ke lobby melalui UI normal, PID 2747/epoch 7 tetap hidup; `scene_pending` di lobby bukan fault. Tidak ada pembelian atau penggunaan revive. Modul ae0dcb92 tetap terpasang. Seluruh 14 file scratch Android milik run dihapus setelah hash salinan lokal dicocokkan; backup lokal dipertahankan.

## Cakupan yang masih terbuka

Kualifikasi penuh 18 kontrol masih membutuhkan perbaikan pulse yang direview, retest efek tiap fitur, observasi baseline/ON/OFF yang diskriminatif, serta lifecycle/background dan pergantian scene dengan state aktif. Source/fixture/ACK yang lulus tidak menggantikan pekerjaan tersebut.

## Asal bukti

Ringkasan ini berasal dari log instalasi dan startup, trace command/result/epoch, tangkapan gameplay, log build, hasil fixture restoration, pencocokan hash source, dan review independen pada sesi lokal 8 Oktober 2026. Log mentah, screenshot dan backup perangkat disimpan lokal; dokumen ini tidak menyatakan berkas tersebut tersedia sebagai aset publik. Batas atribusi dan efek yang belum terbukti tetap dipertahankan.
