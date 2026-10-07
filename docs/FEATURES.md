# Matriks lengkap 47 fitur WSM RC2

Snapshot terkini berasal dari `src/wsm-v2/features/feature_catalog.json`. Seluruh acceptance runtime kandidat tetap UNVERIFIED; status partial/prototype tidak menyatakan seluruh semantik desain selesai.

| ID | Nama desain | Backend | Status | Cakupan / pekerjaan tersisa |
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
| critdmg | Maximum Critical Damage | critdmg | prototype | Getter hero ×1–5, slot 26, relokasi ADR/ADRP; efek damage belum diuji pada game. |
| dura | Infinite Durability | — | target_absent | Durability ditemukan pada GuildMeteor; tidak ditemukan kontrak durability senjata/armor. |
| gbreak | Always Guard Break | — | not_implemented | Belum ada kontrak guard/posture yang membuktikan satu-hit break. |
| ammo | Infinite Ammo | — | not_implemented | MagazineSize/MaxBullet ditemukan; reload, konsumsi per action dan ownership hero belum dibuktikan. |
| parry | Auto-Dodge / Perfect Parry | — | not_implemented | ActivateDodge tersedia untuk FugitiveCharacterController; timing/parry universal belum dibuktikan. |
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
| sp | Infinite Skill / Attribute Points | — | target_absent | Awakening/tree ditemukan; belum ada kontrak poin skill/atribut tak terbatas yang sesuai desain. |
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

Semantik/rentang desain, kategori, selector, jumlah match dan deklarasi/RVA/line API ada dalam JSON. Status target_absent berarti bukti yang tersedia belum menemukan mekanik yang sesuai; bukan bukti mekanik tidak mungkin ada dengan nama lain.

18 kontrol native mencakup 16 ID desain ditambah Damage Guard dan Radius Pulse. Katalog tidak menyimulasikan state ON bagi backend yang belum tersedia. `catalog 0..11` mengembalikan empat fitur per halaman (halaman terakhir tiga).

Untuk qualification final: tentukan contract dan owner, implementasi + restore, lifecycle/thread checks, ukur efek ON/OFF sesuai desain dan validasi negative/scene/PANIC cases. Untuk currency/progress/inventory, buktikan transaksi dan persistensi. Catatan lama RC1/baseline dibaca sebagai riwayat bertanggal.
