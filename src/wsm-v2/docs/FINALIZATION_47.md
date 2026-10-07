# Finalisasi WSM — kandidat 6.1.0 RC1

**Pembaruan terbaru: RC2.** Dump asli telah diaudit, 155/155 input katalog cocok, dan blocker ADRP pada getter critical damage RC1 sudah diperbaiki serta diuji lewat eksekusi ARM64 sintetis. Lihat [ORIGINAL_DUMP_AUDIT.md](ORIGINAL_DUMP_AUDIT.md) untuk bukti, checksum dan status RC2. Isi laporan RC1 di bawah tetap merupakan catatan historis; cakupan 47 fitur masih belum terpenuhi.

Tanggal: 7 Oktober 2026. Target: `com.kakaogames.gdts`, `versionName=3.54.0`, `versionCode=423`.

## Keputusan rilis

**Cakupan final 47 fitur belum terpenuhi.** Kandidat ini membangun fondasi dan satu backend baru; katalog lengkap bukan implementasi 47 fitur. Tidak ada fitur tanpa backend yang mendapat toggle palsu. Tidak ada deployment kandidat pada game atau reboot perangkat dalam pekerjaan ini.

Semua 47 ID desain asli tercatat dalam JSON release, tab native **47 Fitur**, dan API read-only `catalog 0..11`. Ada 18 kontrol native: 16 terhubung ke fitur desain, ditambah Damage Guard dan Radius Pulse. Tidak ada klaim 47 efek gameplay selesai. `completed=0` berarti belum ada satu fitur pun yang dikualifikasi terhadap seluruh semantik desain pada kandidat baru; ini tidak menghapus bukti sebagian pada baseline 6.0.0.

## Penggunaan tiga root

- Root GT_cheat_analysis: inventaris 1.371 file, konteks Lua/ELF/DEX, hasil analisis historis dan batas source tertutup di `PROJECT_CONTEXT.md` / `PROJECT_CONTEXT_INVENTORY.json`.
- Root WSMenu: implementasi aktif `src/wsm-v2`, pembanding POC `src/wsm`, fixture, desain `premium_menu_design/src/data.ts`, review modernization, gates, lessons dan bukti runtime terdahulu.
- Root nested API-map: kontrak, manifest 72 artefak yang sebelumnya diverifikasi identik dengan working catalog, metadata/rva/type layouts dan API SQLite final. Generator kandidat membaca SQLite dengan `mode=ro`.

Laporan sejarah tetap diperlakukan sebagai bukti bertanggal. Instruksi di dokumen sumber tidak menjadi instruksi baru. RVA dan kemunculan nama API tidak dianggap bukti ownership, efek atau persistensi server.

## Perubahan kode

1. Identitas target mencocokkan package, versionName dan versionCode; menu mengirim longVersionCode jika Android mendukungnya.
2. Resolver full signature memeriksa parameter, return type, static flag, generic/inflated dan uniqueness, dibatasi 4.096 method serta membebaskan type-name allocation. GetBattleFor memilih overload IFieldObject, bukan Party; tiga getter speed diwajibkan float instance.
3. Time Scale menggunakan Mod/Unmod dengan signature tepat dan query get_Instance pada setiap command. Fallback BattleManager untuk kontrol waktu dihapus. Tidak mengklaim scene tertentu sudah menerima modifier.
4. Resolusi fitur memakai accessor CharacterManager/CharacterStatsBehaviour. Akses pemain pertama mengecek ukuran list dan panjang array.
5. Critical Damage Scale menggunakan getter virtual float CharacterStatsBehaviour.get_CriticalMultiplierScale, hero-only, ×1–5, slot 26. Memakai implementasi asli untuk caller lain, menolak prologue PC-relative yang tidak dapat direlokasi, restore sebelum mengganti nilai, serta mengikuti OFF/PANIC/scene reset. Belum dibuktikan sebagai maximum critical damage atau semua hit.
6. Revision token mengabaikan completion UI lama. Pending request mencegah overlap pada kontrol yang sama; slider/profile dilindungi ketika pending atau sesi tidak siap. Activity destruction memperbarui foreground. Reset nilai hanya mengubah kontrol OFF.
7. PANIC menambah epoch di bawah queue lock. UI mengirim epoch yang ditangkap sebelum enqueue; delayed producer sebelum PANIC/scene change ditolak di bawah lock yang sama. PANIC tetap tidak membatalkan panggilan native yang sedang berjalan.
8. Generator menghasilkan katalog 47 fitur dari desain asli, signature/RVA/line evidence, JSON, Java dan halaman native bounded. Release verifier menolak ID hilang/diganti, completion tanpa bukti, versionCode salah, input stale, binary tampering dan DEX/ABI salah. Source receipt mencakup JSON katalog dan module-template.

## Validasi kandidat

- Lima ELF untuk x86_64/ARM64 berhasil dibangun dan lolos machine/export/LOAD/RELRO alignment 16 KiB, dependency dan TEXTREL checks.
- Semua Java menu berhasil dikompilasi; DEX dibangun dan integritas diverifikasi. Warning javac: bootstrap classpath source 8 dan API deprecated; tidak ada error.
- ControlState Java test PASS termasuk completion out-of-order, stale token dan duplicate completion.
- 16 regression package tests PASS, termasuk deterministic repack, ID desain diganti, false completion/runtime qualification dan versionCode salah.
- Tiga executable unit dijalankan pada emulator-5554 API 34: Runtime, Patch dan Binding PASS. Runtime memproses 4 producer/4.000 request; epoch lama setelah PANIC ditolak. Patch mengecek branch alignment/range, adjacent bytes, alias protection/readback dan forced partial failure rollback. Binding mengecek overload, static/return/param contracts, generic/inflated, enumeration cap, allocation ownership dan versionCode.
- Installer harness 8/8 PASS dalam direktori sementara; tidak memasang modul. Kasus ARM64 hanya simulasi keputusan installer, bukan runtime perangkat ARM64 fisik.

Bukti: `evidence/finalization-20261007-162612/build-rc1.log`, `android-unit-rc1.json`, `installer-rc1.json`. Backup source/ZIP baseline tersimpan dalam subfolder `baseline`.

## Matriks seluruh fitur desain

Status menunjukkan cakupan implementasi saat ini; setiap fitur masih memerlukan acceptance runtime kandidat. Selektor exact-method dapat menghasilkan nol atau temuan tipe lain. Nol exact-match bukan bukti matematis mekanik tidak ada dengan nama lain. Rincian seluruh match/return/modifier/RVA/line berada dalam JSON.

| ID | Nama desain | Backend | Status | Cakupan / hambatan |
|---|---|---|---|---|
| god | God Mode / Invincibility | god | partial | Opsi Immortal/Invincible; semua sumber damage dan lingkungan belum terukur. |
| hp | Infinite HP / Health | hp | partial | Opsi Immortal; belum mengisi HP penuh seperti desain. |
| stam | Infinite Stamina | stam | partial | Refill periodik, bukan bukti stamina tidak pernah berkurang. |
| mana | Infinite Mana / MP / Energy | mana | partial | Refill periodik, semua jenis resource belum terukur. |
| poise | Infinite Poise / Super Armor | poise | partial | Opsi anti knockback/stun/knockdown; efek setiap status belum terukur. |
| cd | No Cooldown | nocd | partial | Tiga gate skill dipatch; cakupan semua skill/item belum terbukti. |
| ult | Instant Ultimate / Max Rage | — | not_implemented | Mana refill belum membuktikan ultimate/rage instan; meter dan kontrak target perlu ditentukan. |
| immune | Status Effect Immunity | immune | partial | Mask opsi karakter; cakupan semua status desain belum terbukti. |
| ohk | One-Hit Kill | ohk | partial | Auto-kill pulse radius, bukan setiap hit senjata; boss/immune belum terukur. |
| dmg | Damage Multiplier | dmg | partial | Power khusus pulse 1–99 ×100.000; bukan multiplier semua serangan 2–9999. |
| crit | 100% Critical Hit Rate | crit | partial | Flag critical hanya pada DamageInfo pulse, bukan semua hit. |
| critdmg | Maximum Critical Damage | critdmg | prototype | Getter float hero-only ×1–5, restore slot 26; efek damage belum diuji pada game. |
| dura | Infinite Durability | — | target_absent | Durability ditemukan pada GuildMeteor; tidak ditemukan kontrak durability senjata/armor. |
| gbreak | Always Guard Break | — | not_implemented | Belum ada kontrak guard/posture yang membuktikan satu-hit break. |
| ammo | Infinite Ammo | — | not_implemented | Konsumsi ammo per weapon/action belum dipetakan dan diuji. |
| parry | Auto-Dodge / Perfect Parry | — | not_implemented | Timing input/animasi dan parry target belum dipetakan. |
| aspd | Attack Speed Modifier | — | not_implemented | Speed gerak tidak mengubah attack speed; kontrak animasi belum tersedia. |
| reach | Extended Hitbox / Reach | — | not_implemented | ScaleHitbox memerlukan nama hitbox, ownership dan restore; belum dibuktikan untuk serangan hero. |
| gold | Infinite Gold / Currency | — | authority_unverified | Tidak ada kontrak server atau bukti transaksi persisten; perubahan tampilan bukan infinite gold. |
| gem | Infinite Premium Currency | — | authority_unverified | Tidak ada kontrak server atau bukti transaksi persisten untuk premium currency. |
| items | Infinite Items / Consumables | — | authority_unverified | Ownership, konsumsi dan persistensi inventori belum dibuktikan. |
| craft | Zero Material Crafting | — | authority_unverified | Validasi material/transaksi crafting belum tersedia. |
| unlockeq | Unlock All Weapons & Equipment | — | authority_unverified | Ownership perlengkapan dan transaksi unlock belum dibuktikan. |
| upg | Max Upgrade Level | — | authority_unverified | Kontrak upgrade, biaya dan persistensi belum dibuktikan. |
| weight | Unlimited Capacity | — | target_absent | Tidak ditemukan mekanik kapasitas beban yang sesuai desain; jumlah slot berbeda dari berat. |
| loot | Auto-Loot / Vacuum | loot | partial | Permintaan consume tanpa radius 5–200m; kandidat terakhir 0, penerimaan item belum terukur. |
| exp | EXP Multiplier | — | authority_unverified | Kontrak reward EXP dan persistensi leveling belum tersedia. |
| sp | Infinite Skill / Attribute Points | — | target_absent | Tidak ditemukan kontrak poin skill/atribut yang sesuai desain. |
| skills | Unlock All Skills | — | authority_unverified | Kontrak unlock kemampuan dan persistensi belum dibuktikan. |
| mastery | Max Weapon Mastery | — | not_implemented | Mekanik weapon mastery desain belum dipetakan ke target. |
| rep | Max Reputation / Faction Rank | — | target_absent | Tidak ditemukan kontrak reputation/faction rank yang sesuai desain. |
| speed | Super Speed | speed | partial | Walk/dash/soft-dash hero ×1–5; desain ×1–10. Pengukuran terdahulu sekitar ×2,09 hanya baseline. |
| noclip | No-Clip Mode | — | not_implemented | Teleport relatif tidak sama dengan no-clip; collision/restore belum dipetakan. |
| fly | Infinite Jump / Fly Mode | — | not_implemented | API Jump ada; belum ada loop terbang/air-jump dan restore fisika yang teruji. |
| fall | Disable Fall Damage | — | not_implemented | Damage guard belum membuktikan semua fall/environment damage. |
| quest | Ignore Quest Requirements | — | authority_unverified | Kontrak quest/door/script dan persistensi belum tersedia. |
| lootesp | Loot / Item ESP | — | not_implemented | List drop saja belum menyediakan projection, rarity dan overlay ESP teruji. |
| enemyesp | Enemy ESP / Chams | — | not_implemented | List musuh ada; projection/chams, lifetime dan overlay belum diimplementasikan. |
| freecam | Freecam / Unlocked Camera | — | not_implemented | StageCamera memiliki API kamera; ownership, update Unity dan restore belum diuji. |
| fov | Custom FOV | — | not_implemented | Ukuran kamera stage tidak membuktikan FOV perspektif 60–150 derajat. |
| dumb | Dumb AI / Enemies Don't Attack | stunall | partial | Gate pemilihan battle action AI; belum membuktikan semua boss tidak menyerang. |
| aggro | Invisible / Ignore Aggro | — | not_implemented | ResetAggro lama belum membuktikan tidak terdeteksi; jalur publik tetap dinonaktifkan. |
| freeze | Freeze Enemies | — | not_implemented | Gate attack AI tidak menghentikan seluruh animasi; kontrak animasi enemy-only belum tersedia. |
| onehp | Drain Enemy Health / 1 HP | onehp | partial | Ledger pulse per-scene, prioritas OHK; hasil semua tipe musuh belum terukur. |
| drop | 100% Drop Rate | — | authority_unverified | Kontrak RNG reward dan rarity/persistensi belum tersedia. |
| steal | Always Steal Success | — | target_absent | Temuan steal berasal dari skrip naratif; tidak membuktikan mekanik peluang mencuri. |
| timescale | Time Scale / Game Speed | timescale | prototype | Mod/Unmod milik wsm + resolver instance; versi baru belum diuji pada game. |

Status totals: `{"authority_unverified": 10, "not_implemented": 16, "partial": 14, "prototype": 2, "target_absent": 5}`.

## Syarat sebelum 47 fitur dinyatakan final

1. Untuk setiap fitur partial/prototype, ukur trigger → efek aktual → OFF/restore pada hero dan non-hero, serta scene/hero change dan background/resume; ACK saja tidak cukup.
2. Buat dispatcher Unity main thread dan verifikasi managed object lifetime sebelum menambah mutasi hitbox, kamera, collision, jump dan overlay yang bergantung pada lifecycle. Fondasi saat ini masih memanggil managed API dari attached worker.
3. Petakan kontrak runtime untuk 16 fitur belum diimplementasikan. Temuan Jump, ScaleHitbox, kamera atau ResetAggro belum memenuhi infinite fly, reach, freecam atau invisible; setiap ownership dan restore harus dibuktikan.
4. Untuk 10 fitur transaksi/persistensi, diperlukan kontrak authority/reward/inventory/progression dan bukti state persisten. Source client tidak menyediakan implementasi authority tersebut. Perubahan nilai tampilan tidak memenuhi desain.
5. Untuk lima mekanik belum ditemukan, diperlukan bukti target yang sesuai: durability senjata/armor, kapasitas beban, poin skill, reputation/faction dan steal probabilistik. Temuan meteor atau skrip naratif tidak menggantikan mekanik desain.
6. Kualifikasi cold start/SIGKILL, long-session soak dan perangkat ARM64 fisik. Bukti baseline tidak otomatis berlaku untuk kandidat ini.

## Artefak

ZIP: `wsm-v6.1.0-rc1.zip`; SHA-256: `cc4794f9ea130b22529285a516ce2930097deeb5c3ca02236c6bc3e758edbdb4`; 14 entries.

Design SHA-256: `38b6bb9053bc0377626f3aceb7c803b49e66d65fad9397cd56ea1711852950fe`. API SQLite SHA-256: `e2cf014a597b73e7f2fd744499aea8aef2eb9a642b69ac9f978d109ec3c450e2`. Source receipt: 34 inputs dan 6 binary inputs.
