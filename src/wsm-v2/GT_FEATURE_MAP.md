> Arsip framework sebelum WSM 6. Klaim LIVE/VERIFIED dan PANIC/Clear di bawah bukan kontrak versi 6. Lihat `README.md` serta `docs/RELEASE_VALIDATION.md` untuk implementasi dan bukti terkini.

# GT_FEATURE_MAP — Adaptasi Mod Menu → Guardian Tales

> Status: **v3.0 (framework live)** · Diperbarui: 2026-10-06
> Prinsip: tiap fitur ditargetkan ke **API resmi game** (il2cpp by-name) — bukan hook buta.
> Teknik utama: `StatsOption flag` (God/super armor/immune), `setter` (stam/mana), `GlobalTimeManager mods` (timescale).
> Semua panggilan **fault-guarded** (crash = call di-skip, game selamat) & **reversible** (PANIC = nol semua).

## Chain runtime (tervalidasi via dump + siap di engine)

```
Stage.Instance (static, get_Instance)
 └─ characterManager          [field 0x80]
     └─ GetAllPlayers()       → List<ICharacter>  (items@0x10, size@0x18)
         └─ Character.characterStatsBehaviour  [field 0x130]
             ├─ AddCharacterStatsOption(op)      — flag IMMORTAL/INVINCIBLE/dll
             ├─ RemoveCharacterStatsOption(op)   — revert
             ├─ set_Stamina(float)               — stamina penuh terus
             └─ set_Mana(float)                  — mana penuh terus
```

`CharacterStatsOptions`: `Immortal=1 · Invincible=2 · NoKnockBackByDamage=4 · NoGetPushedWhenDown=8 · NoStun=16 · NoDown=32 · NoArial=64 · NoPoison=128 · NoAilment=224 · All=255`

## Kategori 01 — Statistik & Karakter

| Fitur | Target GT | Teknik | Status |
|---|---|---|---|
| God Mode | `CharacterStatsBehaviour` | `AddCharacterStatsOption(Immortal|Invincible=3)` | **LIVE — VERIFIED (HP aman digempur)** |
| HP Selalu Penuh | idem | `Immortal(1)` — tercakup god | LIVE (subset) |
| Stamina Tanpa Batas | idem | `set_Stamina(100)` top-up 0.9s | **LIVE — VERIFIED ("aman")** |
| Mana Tanpa Batas | idem | `set_Mana(100)` top-up 0.9s | **LIVE — VERIFIED ("aman")** |
| Super Armor | idem | `AddCharacterStatsOption(4|16|32=52)` | **LIVE — survive v3.4 one-shot** |
| Kebal Efek Status | idem | `AddCharacterStatsOption(NoAilment=224)` | **LIVE — survive v3.4 one-shot** |
| Skill Tanpa Cooldown | `Skill/*CoolTime*` (dump 327228 `CoolTime{get;set;}`) | telusuri kelas skill → setter/field | RISET |
| Ultimate Instan | gauge ultimate (cari kelas) | field/method scan | RISET |

## Kategori 02 — Pertempuran & Damage

| Fitur | Rencana teknik | Status |
|---|---|---|
| One-Hit Kill / Damage Multiplier | `CharacterStatsBehaviour.Damage(DamageInfo)` ke musuh — DamageInfo = struct besar (0x2F8, banyak `ObscuredInt`) → **butuh konstruksi hati-hati** (wave 2) | RISET |
| Kritikal 100% / CritDmg | getter read-only + ObscuredFloat → butuh buff `CriticalMultiplierScale(46)` / hook | RISET |
| Stun Semua Musuh | `CharacterStatsOptions.NoStun` baca-balik? — arah: `BattleInstance`/event | RISET |
| Attack Speed / Jangkauan | read-only getter → butuh hook method | RISET |

Catatan: jalur damage via `DamageInfo` sengaja tidak dipaksakan di v3.0 — struct penuh nilai ter-obfuscate (ACTk); salah isi = risiko korupsi state. Pendekatan aman direncanakan: pakai **buff resmi** (`BuffManager.AddBuff(...)`) begitu nama spec valid ditemukan, atau **skin DamageInfo minimal** (field `_damage` + `sender/target` + type) dengan uji terkontrol.

## Kategori 03/04 — Ekonomi & Progresi → **SERVER-SIDE**

`gold/gem/items/craft/unlock/exp/sp/skills/mastery/rep` — otoritas di server GT; klien hanya menampilkan. Perubahan klien akan **disinkron-balik** server (dan `Oak.MultiPlayHackReporter` mencatat anomali di mode multiplay). **Jujur: tidak tersedia sebagai fitur klien** — ditandai `[SERVER]` di menu.

## Kategori 05 — Pergerakan

| Fitur | Rencana | Status |
|---|---|---|
| Super Speed | field kecepatan `CharacterStatsBehaviour` = getter read-only → butuh hook `GetModifiedSpeed`/buff `StackableSpeedScale` | RISET |
| No-Clip / Lompat Tinggi | tidak ada API aman — butuh riset collision | RISET |
| Tanpa Aggro | `BattleInstance.ResetAggro(IFieldObject)` periodik (via `BattleManager.GetBattleForMyParty`) | RISET (API tersedia!) |

## Kategori 06/07 — Visual & Musuh

| Fitur | Rencana | Status |
|---|---|---|
| Penanda Item/Musuh (ESP) | top-down game; riset utility UI | RISET |
| Freecam / FOV | `StageCamera` — riset komponen | RISET |
| Freeze Musuh | kandidat: `ChangeCharacterActiveStates(Enemy, ActiveState)` — **risiko soft-lock**; alternatif aman: timescale mendekati 0 | RISET |
| Musuh Pasif / 1 HP | AI class + jalur damage | RISET |

## Kategori 08 — Sistem

| Fitur | Target | Status |
|---|---|---|
| Time Scale | `GlobalTimeManager.Mod(x,"wsm")` / `Clear()` | **LIVE sejak G11** |
| PANIC (kill-switch) | semua fitur off + `RemoveCharacterStatsOption(0xFF)` + `Clear()` | **LIVE v3.0** |

## Model keamanan (R8–R11)

- **Satu mod aktif saat uji** (R8) — nol overlap efek.
- **Fault-guard**: setiap panggilan il2cpp di dalam `GUARDED_BEGIN/END`; fault tertangkap → skip + log, game tidak pernah mati.
- **Reversibility**: toggle off = `RemoveCharacterStatsOption(managed&~want)`; PANIC = strip 0xFF + `Clear()`.
- **Scope**: single-player/PvE. Mode multiplayer (co-op/PvP) **tidak jadi target** (MultiPlayHackReporter aktif di sana).
- **Timing**: fitur stats hanya apply saat `Stage.Instance` + `CharacterManager` ada (masuk stage). Di title screen ack = `MISS (stage belum siap)` — perilaku benar.
