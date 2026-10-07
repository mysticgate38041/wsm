# Dump asli Guardian Tales dan penerapannya pada WSM RC2

Tanggal: 7 Oktober 2026. Kandidat terbaru: **WSM 6.1.0 RC2**. Folder sumber dibaca tanpa dimodifikasi.

## Lokasi dan hubungan dengan katalog lama

Folder yang tersedia di disk adalah `C:/Users/Administrator/Downloads/Mod/gt_dump` (underscore). Path `gt-dump` pada gambar tidak tersedia di filesystem sesi ini. Seluruh pekerjaan memakai folder `gt_dump` yang berisi manifest Guardian Tales `com.kakaogames.gdts`, versi **3.54.0**, code **423**, minSdk 24, targetSdk 36; WSM tetap membutuhkan API 26+.

Dump ini merupakan sumber asli katalog API yang sudah digunakan WSM. Seluruh **155/155 input** katalog cocok ukuran dan SHA-256 pada pemeriksaan baru. Jadi dump ini bukan kumpulan deklarasi versi berbeda yang otomatis menambahkan API hilang. Nilai tambah audit baru adalah pembacaan binary asli, prologue disassembly, source Lua gameplay, penelusuran nama lebih luas dan penerapan perbaikan konkret.

Inventaris mencatat **55.924 file, 7.130.490.825 byte**. Setiap path dan ukuran tersimpan di `analysis/dump-source-audit/inventory.json`; checksum 155 input, native evidence, Lua references dan pencarian seluruh 47 fitur berada di `audit.json`. Inventaris semua file tidak berarti seluruh texture/media, SDK third party dan body binary telah didekompilasi secara semantik.

## Isi utama

| Bagian | Isi dan kegunaan |
|---|---|
| `raw` | APK base, ARM64 split dan manifest versi. |
| `apk_base` | DEX Android/SDK, assets Unity dan sembilan source Lua gameplay. |
| `apk_config/lib/arm64-v8a` | 32 library native termasuk binary IL2CPP asli. |
| `out/il2cpp_dump` | dump.cs 79.990.654 byte, il2cpp.h 152.033.178 byte, dua metadata 39.087.968 byte dan 149 DummyDll. |
| `out/carve` | Tahapan pemulihan metadata dan dua salinan ELF dengan relocation diterapkan. |
| `out/assets_export` | 13.448 artefak asset, termasuk struktur UI dan komponen serialized. |
| `out/data_export` | 1.394 artefak, termasuk layout Awakening, localization, konfigurasi dan helper Lua. |
| `out/jadx_src` | 35.176 file Java/resource decompiled, banyak SDK Android; bukan 35.176 source gameplay managed. |
| `ghidra_proj` | Project/database analisis native, sekitar 3,84 GB. Bukan C# source yang otomatis tersedia sebagai text. |
| `tools` | Dumper, AssetStudio, APK/JADX tools, JRE, native signatures dan salinan keluaran dump. |

Katalog mencakup 36.384 types, 293.384 method declarations, 253.660 fields, 62.796 properties, 171.098 generic instances dan 451.126 ScriptMethod rows. Jumlah ScriptMethod tidak sama dengan jumlah method managed unik atau seluruh implementasi C# yang pulih.

## Cara dump dipulihkan dan batasnya

Recipe akhir menjelaskan payload metadata tersembunyi dalam mscorlib.dll-resources.dat, key XOR 52 byte, mask per block, metadata v29.1 dengan method record 36 byte yang dikonversi menjadi 32 byte untuk Dumper, serta pemulihan registration pointers dari ELF relocations. `libil2cpp_reloc_norela.so` mematikan relocation pass kedua untuk mencegah double application.

Log akhir menunjukkan dumping/struct/dummy generation selesai, tetapi tetap memuat **20 error attributeIndex**. Klaim “100% dump / 0 errors” pada recipe berlaku sebagai klaim dokumen sumber dan perlu dibatasi: jumlah type/address dapat lengkap sementara pemulihan attribute tidak tanpa error. Body `{ }` pada dump.cs dan DummyDll adalah placeholder deklarasi; algoritme gameplay perlu dibaca dari native code, Lua atau runtime. Tidak ada server source/transaksi persisten yang pulih hanya dari dump client ini.

## Temuan native yang langsung memperbaiki WSM

Getter **Oak.CharacterStatsBehaviour.get_CriticalMultiplierScale** berada pada RVA `0x8c86d58` dan offset file `0x8c82d58`. Empat instruksi awal:

```asm
str  d8, [sp, #-0x20]!
str  x30, [sp, #0x8]
stp  x20, x19, [sp, #0x10]
adrp x20, 0xa279000
```

Instruksi keempat ADRP membuktikan bahwa guard RC1 akan menolak getter asli ini sebelum hook terpasang. Prefix native 32 byte dibandingkan dengan binary ARM64 APK asli dan cocok; temuan bukan akibat perubahan relocation copy. Seluruh 11 method native yang dipilih juga cocok antara salinan analisis dan binary asli.

RC2 menambahkan relocator ADR/ADRP yang mempertahankan alamat tujuan dan destination register pada trampoline. B/BL, B.cond/BC.cond, CB/TB, BR/BLR dan seluruh literal-load/PRFM tetap ditolak. ADR yang keluar rentang ±1 MB atau ADRP yang keluar rentang ±4 GB ditolak. Relokasi seluruh empat word diselesaikan sebelum output dipublikasikan. Lompatan kembali memakai branch langsung sehingga tidak menghancurkan x16 hasil prologue; jika branch kembali keluar rentang, emitter menolak sebelum patch dipublikasikan. Wrapper getter speed dan critical damage menggunakan jalur ini; guard hook legacy lain tetap dipertahankan.

Method CriticalChanceWoMult juga memiliki ADRP pada posisi keempat, sedangkan tiga getter speed tidak memiliki ADR/ADRP pada prefix. Temuan ini disimpan sebagai evidence; CriticalChanceWoMult belum diaktifkan sebagai jaminan semua hit critical.

## Source Lua gameplay

Sembilan source di `apk_base/assets/GameScript` dibaca untuk referensi managed dan kontrak gameplay, total 309.181 byte / 9.076 baris. Di antaranya battle_init.lua (7.839 baris) dan ManualKnightBattleAction.lua (887 baris).

- `battle_init.lua:651` mengambil CriticalChance dari CharacterStatsBehaviour; tidak membuktikan flag critical pulse WSM memengaruhi setiap jenis serangan.
- `battle_init.lua:1092` memublikasikan DamageCommand, dan `:6454` membentuk DamageInfo dengan 15 parameter, termasuk modifier, critical, not_mortal dan no_critical. Lua mengonfirmasi adanya jalur damage dengan semantik berbeda dari multiplier getter atau pulse.
- ManualKnightBattleAction mengatur state timing, search range, hit distance, collision size dan dash per action. Getter movement speed tidak otomatis menjadi attack speed atau reach.
- Layout Awakening dalam data export memberi kandidat skill progression. Awakening/tree menggunakan konsep game yang perlu dibedakan dari infinite skill/attribute points desain generik.

Referensi lengkap dan line evidence ada dalam `audit.json`; helper seperti profiler, memory dan LuaPanda tidak diperlakukan sebagai implementasi fitur WSM.

## Penelusuran seluruh 47 fitur

Pencarian baru memakai substring nama owner/method, termasuk sinonim. Kandidat dibatasi owner Oak, UnityEngine dan GlobalTimeManager; maksimal 100 contoh per fitur disimpan, dengan flag truncated dan jumlah total. Pencarian ini adalah discovery, bukan pemetaan otomatis atau runtime qualification.

Temuan yang memperjelas status:

- Ammo: `WeaponSpec.get_MagazineSize()` mengembalikan ObscuredInt, dan `DesertelfHunterCharacterStatsBehaviour.get/set_MaxBullet` hanya kontrak karakter tertentu. Backend infinite ammo universal belum tersedia.
- Dodge: `FugitiveCharacterController.ActivateDodge` dan dodge state khusus ditemukan, tetapi bukan kontrak perfect parry universal.
- Durability tetap milik meteor guild; tidak ditemukan durability senjata/armor yang sesuai desain.
- Weight/capacity menghasilkan banyak kandidat bernama mirip; tidak menetapkan mekanik beban inventori tak terbatas.
- Reputation/faction hits merupakan nama seperti Satisfaction/action-change, bukan rank faksi.
- Steal/pickpocket ditemukan pada coroutine event/naratif, bukan probability-steal universal.
- AttackSpeed/ActionSpeed/AnimationSpeed/AnimatorSpeed tidak menghasilkan exact semantic candidate pada owner yang ditelusuri. Mengubah dt/time scale global tidak dianggap attack-speed hero-only.

| ID | Kandidat nama luas | Backend WSM | Status saat ini |
|---|---:|---|---|
| god | 95 | god | partial |
| hp | 1954 | hp | partial |
| stam | 362 | stam | partial |
| mana | 3140 | mana | partial |
| poise | 333 | poise | partial |
| cd | 146 | nocd | partial |
| ult | 436 | — | not_implemented |
| immune | 640 | immune | partial |
| ohk | 3061 | ohk | partial |
| dmg | 126 | dmg | partial |
| crit | 516 | crit | partial |
| critdmg | 172 | critdmg | prototype |
| dura | 2 | — | target_absent |
| gbreak | 880 | — | not_implemented |
| ammo | 14 | — | not_implemented |
| parry | 17 | — | not_implemented |
| aspd | 0 | — | not_implemented |
| reach | 616 | — | not_implemented |
| gold | 282 | — | authority_unverified |
| gem | 919 | — | authority_unverified |
| items | 84 | — | authority_unverified |
| craft | 538 | — | authority_unverified |
| unlockeq | 1641 | — | authority_unverified |
| upg | 677 | — | authority_unverified |
| weight | 106 | — | target_absent |
| loot | 224 | loot | partial |
| exp | 3738 | — | authority_unverified |
| sp | 215 | — | target_absent |
| skills | 472 | — | authority_unverified |
| mastery | 112 | — | not_implemented |
| rep | 6 | — | target_absent |
| speed | 146 | speed | partial |
| noclip | 421 | — | not_implemented |
| fly | 482 | — | not_implemented |
| fall | 0 | — | not_implemented |
| quest | 2694 | — | authority_unverified |
| lootesp | 38 | — | not_implemented |
| enemyesp | 446 | — | not_implemented |
| freecam | 68 | — | not_implemented |
| fov | 33 | — | not_implemented |
| dumb | 9014 | stunall | partial |
| aggro | 386 | — | not_implemented |
| freeze | 5 | — | not_implemented |
| onehp | 27 | onehp | partial |
| drop | 1299 | — | authority_unverified |
| steal | 7 | — | target_absent |
| timescale | 55 | timescale | prototype |

Status cakupan tetap 14 partial, 2 prototype, 16 belum diimplementasikan, 10 authority/persistensi belum terverifikasi dan 5 mekanik target belum ditemukan. Nama yang ditemukan dan perbaikan emitter tidak mengubah fitur menjadi selesai tanpa implementasi dan pengukuran efek.

## Validasi RC2

- Lima ELF native x86_64/ARM64 berhasil dibangun; machine/export/16 KiB LOAD/RELRO serta dependency checks PASS.
- Java/DEX dan ControlState tests PASS.
- 16 regression package tests PASS; source/binary receipt dan ZIP 14-entry integrity PASS.
- Empat executable Android PASS: runtime (4 producer / 4.000 request), alias patch/rollback, full-signature binding dan ADR/ADRP relocation.
- Relocation unit menguji signed boundary, target conservation, register preservation, roundtrip, prologue critical GT asli, semua varian literal/branch terlarang dan kegagalan tanpa partial write.
- **Instruksi ARM64 benar-benar dieksekusi melalui native bridge emulator:** APK instrumentasi ARM64-only `com.wsm.relocprobe` mengompilasi emitter payload produksi, lalu menguji branch, neighbor, hero predicate, wrapped getter dengan ADRP pada register x16, hasil caller lain dan restore. Hasil `nativeResult=0`, `status=PASS`, scope `ARM64_SYNTHETIC_ONLY_NO_GAME_MEMORY`. APK probe telah dihapus setelah pengujian. Ini bukan uji perangkat ARM64 fisik dan bukan pengukuran damage game.
- Installer harness 8/8 PASS pada direktori sementara. Modul RC2 belum dipasang pada game, tidak ada reboot atau mutasi gameplay dalam audit ini.

Bukti: `src/wsm-v2/evidence/dump-finalization-20261007/` (build-rc2.log, android-unit-rc2.json, arm64-instrumentation.txt, installer-rc2-final.json, probe-cleanup.txt). Source test yang dapat direproduksi ada di `tests/arm64-reloc-fixture/`.

## Status finalisasi

Dump asli sekarang terhubung ke provenance katalog, evidence native dan matriks fitur dalam kandidat RC2. **Finalisasi seluruh 47 efek gameplay belum tercapai.** Masih diperlukan backend 31 fitur, acceptance terhadap semantik desain untuk 16 fitur yang terhubung, main-thread dispatch/object lifetime, cold-start/soak, kontrak persistensi dan kualifikasi perangkat ARM64 fisik. Kandidat ini tidak memakai ON palsu atau angka runtime simulasi untuk menutup celah tersebut.

Paket: `wsm-v6.1.0-rc2.zip`; SHA-256 `0a6ee1a3b1897197a26519c9a387c1556116166ab3a32b0c6cb16dc94f75ab94`.
