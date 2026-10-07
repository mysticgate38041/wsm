# STUDI KOMPARATIF TOTAL — 5 CHEAT GT vs WSM
*Sesi riset sebelum v28 · oleh worm shadow, untuk He · 2026-10-07*

Bahan: `ANALISIS_LENGKAP_GT_CHEATS.md/v2`, `zygisk_v354/ZYGISK_NIFUJI_V354_ANALYSIS.md`,
`zygisk_modmenu_v1/MODMENU_VOUNDER_ANALYSIS.md`, `gt294_analysis/GT294_TDL_ANALYSIS.md`,
`gt_dump_analysis/GT_DUMP_SYNTHESIS.md`, `WSMenu/NIFUJI_TECH_DOSSIER.md` + code WSM sendiri.

---

## 0. RINGKASAN 6 SISTEM

| # | Nama | Bentuk | Inti mekanisme |
|---|---|---|---|
| 1 | **OnlyTris v25** | Lua GG (bytecode 5.2 terenkripsi) | Value-level: scan/edit nilai memori via GameGuardian |
| 2 | **TDL v2.94** | Lua GG (Byte V4 junk-maze) | Value-level: loop searchNumber→editAll |
| 3 | **Shaizuro** | Zygisk native (Dobby hook + ImGui) | Behavior-level: hook il2cpp by-name, offset auto-update dari server |
| 4 | **Nifuji v3.54** | Zygisk + DEX (Java UI) + **injeksi XLua** | Hook by-name + **inject script Lua ke runtime game** + ESP canvas |
| 5 | **VounderS v1.0** | Zygisk C++ murni + ImGui + Dobby | Hook + set nilai + restore-old (pola `old_*`) |
| 6 | **WSM (kita)** | Zygisk-loader(x86) + engine(arm64 via houdini bridge) + payload arm64 | **Hook ABSOLUT non-ASLR** + invoke il2cpp + pulsa + menu premium |

**Benang merah semua (dari sintesis dump):** semuanya = cheat sisi-klien pada sistem yang sama
(namespace `Oak` di libil2cpp; stats tempur/gerak/visual). Server hanya menerima booleans/daftar kill
untuk PvE; ranked/coop dikirim `damages[]` + ada `MultiPlayHackReporter` di klien → sesuai kontrak scope WSM.

---

## 1. PETA LENGKAP KELAS DAMAGE (dari dump v3.54, VERIFIED)

Hierarki kunci:

```
FieldObjectStatsBehaviour            ← penulis HP level terendah (field ObscuredInt hp @0x5C)
 ├─ ApplyDamage(DamageInfo, out, out)  ← "A_real" — jalur damage properti/monster (terbukti counter 38→75)
 ├─ Damage(DamageInfo) [virtual slot19]
 ├─ CheckWillDie / ChangeHpToFixedValue / ApplyDamageToShield...
 └─ CharacterStatsBehaviour : FOS      ← subclass KARAKTER (hero/party)
     ├─ characterStatsOptions @0x550   ← BITFIELD OPSI (ObscuredInt!)
     ├─ Damage(DamageInfo) [override]  ← override slot19
     ├─ OnDamageRecorder / OnDeadEvent / set_IsDeadConfirmed
     └─ get_WalkSpeed / get_DashSpeed...

IDamagedBehaviour (interface)
 ├─ CharacterDamagedBehaviour                  ← gate intake damage KARAKTER (hero!)
 │   ├─ field `character` @0x10
 │   ├─ Oak.IDamagedBehaviour.Damage (explicit impl, slot4)
 │   ├─ protected virtual bool Damage(DamageInfo) [slot12]  ← TARGET VounderS!
 │   └─ DamageReaction / Heal / Die
 └─ MonsterDamagedBehaviour                    ← gate monster
     └─public virtual bool Damage(DamageInfo) [slot14]      ← target hook kita (slot 1) — count 0!
```

### Tabel alamat (Offset dump → runtime non-ASLR `0x400022C04000 + Offset`)

| Fungsi | Offset | **Runtime (absolut, stabil)** |
|---|---|---|
| FOS.ApplyDamage (A_real) | 0x63B24B4 | **0x400028FB64B4** ✅ terbukti |
| FOS.Damage (virtual 19) | 0x63B3398 | 0x400028FB7398 |
| FOS.CheckWillDie | 0x63B23AC | 0x400028FB63AC |
| **CharacterDamagedBehaviour.Damage (slot12)** | 0x8C5A7F0 | **0x40002B85E7F0** ← gate HERO (VounderS) |
| CharacterDamagedBehaviour iface-impl (slot4) | 0x8C5A588 | 0x40002B85E588 |
| MonsterDamagedBehaviour.Damage (slot14) | 0x8CB3930 | 0x40002B8B7930 |
| MonsterDamagedBehaviour.Die | 0x8CB68F4 | 0x40002B8BA8F4 |
| MonsterDamagedBehaviour.OnDeadEvent | 0x8CB6C40 | 0x40002B8BAC40 |
| MonsterDeadCommand.Execute | 0x6262254 | 0x400028E66254 |
| MonsterDeadCommand.set_Target | 0x6262168 | 0x400028E66168 |
| CharacterStatsBehaviour.Damage (override) | 0x6B8C2C4 | 0x40002B88C2C4 |
| CSB.OnDamageRecorder | 0x6B8CCBC | 0x40002B88CCBC |
| CSB.OnDeadEvent | 0x6B8F768 | 0x40002B88F768 |
| CSB.set_IsDeadConfirmed | 0x6B86780 | 0x40002B886780 |

**Cara tiap cheat menyerang sisi ini:**

| Fitur | Shaizuro | Nifuji | VounderS | WSM sekarang |
|---|---|---|---|---|
| Immortal/God | hook (label `Godmode`) — target runtime | via Lua inject (+ menu `God Mode`) | **hook `CharacterDamagedBehaviour.Damage`** | (a) fitur legacy `god` = `AddCharacterStatsOption(Immortal(1)\|Invincible(2))` ke party — **jalur resmi game, belum pernah dites di 3.54**; (b) eksperimen hook `FOS.ApplyDamage` (crash krn bug slot → fix v28) |
| Damage/Defense mult | hook + nilai | Lua + slider | hook `CharacterDamagedBehaviour.Damage` (+ `old_*` restore) | pulsa `set_modifier`/`GenerateDamageFromLua` (dmg), `set_critical` (crit) |
| Dumb Enemy | ? | ? | hook `MonsterBattleAIState.PickNTriggerBattleAction` | pulsa stun/aggro (`stunall`, `aggro`, `aura`) |
| Speed | hook getter | slider (get_WalkSpeed/DashSpeed) | hook `CharacterStatsBehaviour.get_WalkSpeed/get_DashSpeed` | (belum; kandidat sama) |
| ESP/Wallhack | ya (ESP Object) | ESPView canvas | Wallhack/Ghost | belum (kategori Visual 0/4) |
| Teleport | ya | — | Horizontal/Vertical pos | ✅ `set_Position` (+drone view) |
| Colosseum | ? | ? | `ColosseumClient.SendColosseumEnd` (Autowin!) | belum (scope: lokal PvE only) |

## 2. PROTEKSI & DISTRIBUSI (perbandingan mutu engineering)

| | OnlyTris | TDL | Shaizuro | Nifuji | VounderS | WSM |
|---|---|---|---|---|---|---|
| Proteksi | VM+lumpur | junk 86%+JMP | hampir nol | sha256 anti-repack + DEX XOR77 + XZ blob | XOR 0x2E (paling lemah) | staging memfd + modul magisk; engine kita sendiri |
| Auto-update | per versi script | per versi | **offset dari server** | pin versi game | "stable" | by-name+absolut dump (kita) |
| UI | GG dialog | GG dialog | ImGui native | Java View + ESP canvas | ImGui native | ImGui premium (mirip 3&5) |
| ABI | — | — | arm64+armv7 | arm64+x86_64 (emulator!) | 4 ABI | x86_64 loader + arm64 engine (houdini) |

## 3. 🎯 PELAJARAN UNTUK WSM (deltas konkret)

1. **God mode HERO = `CharacterDamagedBehaviour.Damage` (VounderS-proven).** Kita selama ini
   nembak lapisan bawah (`FOS.ApplyDamage`) + fitur option resmi. Tiga kandidat kini bersaing:
   - **(A) Fitur legacy `god` kita** (AddCharacterStatsOption Immortal+Invincible) — paling murah,
     NOL hook, jalur resmi game (`CoopCheatInvincibleCommand` = dev command memakai konsep sama). UJI DULU.
   - **(B) Hook `CharacterDamagedBehaviour.Damage`** @ `0x40002B85E7F0` (+ opsi iface-impl
     `0x40002B85E588`) dengan skip kondisional `this.character == hero` (via `get_DamagedBehaviour`
     yang sudah kita resolve!). Mekanisme hook absolut kita sudah terbukti.
   - **(C) Hook `FOS.ApplyDamage`** @ `0x400028FB64B4` skip — setelah fix poison v28.
2. **ATURAN BARU WAJIB: validasi hasil `cgm` vs dump.** Resolver il2cpp (cgm) TERBUKTI bisa
   mengembalikan alamat salah/folded (kasus slot13 "A_auto" → crash 3×). Sekarang kita punya
   tabel Offset VALID dari dump — setiap target damage-family harus divalidasi
   (`resolved == bias + dump_offset`); kalau tidak cocok → **fallback langsung ke absolut dump**.
   Ini membunuh seluruh kelas bug "resolver nyasar" untuk selamanya.
3. **Hook presisi > hook buta:** Shaizuro menyelesaikan "offset auto-update" dari server; kita
   mampu hal yang setara offline karena **il2cpp non-ASLR + kita punya dump** (mode B di plan WSM).
4. **Pola `old_*` (VounderS) untuk restore** = selaras kontrak kita (reversible) — sudah kita pegang.
5. **Nifuji's Lua-inject** = kemampuan kelas lain (pakai XLua game) — tidak perlu untuk god, tapi
   catat sebagai opsi fitur "Lua generator" di plan (mode C).
6. Anti-cheat: tidak ada dari mereka yang menyentuh server — konsisten dengan scope WSM. `MultiPlayHackReporter`
   + `ERROR_AUTH_USER_BANNED_WITH_HACK` = alasan kita TIDAK menyentuh ranked/ekonomi (tetap).

## 4. KEPUTUSAN OPERASIONAL (usulan urutan tes berikutnya)

1. **[0 biaya]** Hidupkan feat legacy `god` (F1) → hero dihajar → HP tetap? → kalau YA: god mode = SELESAI.
2. **[v28]** Fix poison (restore-alamat-lama + larangan orig-kosong + godmode→slot bebas 18) +
   tambah log alamat + validasi cgm-vs-dump → build → tes ulang (C) skip `FOS.ApplyDamage`.
3. **[v29 kalau perlu]** Pasang hook (B) `CharacterDamagedBehaviour.Damage` + kondisi hero →
   skip damage di level karakter (paling elegan: menirukan VounderS yang proven).
4. Speed (get_WalkSpeed/get_DashSpeed) & ESP = fase fitur berikutnya (peta target sudah ada di atas).

— SELESAI STUDI. Semua angka = terverifikasi dari dump + aritmetika bias yang sama dengan A_real yang terbukti hidup. 🪱
