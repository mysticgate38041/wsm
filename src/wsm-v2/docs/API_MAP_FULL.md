# API_MAP_FULL — Peta Lengkap API Guardian Tales (internal)

> **ARSIP SESI LAMA — bukan katalog lengkap atau attestation WSM6.** Peta 3.54.0
> yang diindeks dan diverifikasi dari metadata berada di
> [laporan statis baru](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/analysis/gt354-api/REPORT.md).
> Label `[V]` di bawah mengacu sesi lama; gunakan RELEASE_VALIDATION.md untuk bukti WSM6.
> Koreksi wrapper: `_s_set_*` adalah setter, bukan static method. Static wrappers
> dapat bernama `_m_*_xlua_st_`. Keberadaan detector/API tidak membuktikan perilaku aktif atau invisibility.

> Disusun: 2026-10-06 · Basis: dump il2cpp 2.2 juta baris + sesi tembusan WSM v1→v4.0
> Status label: **[V]** = diverifikasi live di engine · **[K]** = terkonfirmasi dari dump · **[P]** = proposed/riset
> File pendukung: `lua_api_idmap.txt` (27.151 API ter-expose), `lua_wrappers_m.txt` (1.329), `lua_wrappers_s.txt` (1.069), `lua_wrappers_g.txt` (1.550), `sys_core_idmap.txt` (1.655)

---

## 0. Tiga Lapis Arsitektur Game

```
┌─ L3 SCRIPT (xLua / Lua) ────────────────────────────┐
│  LuaEnv, LuaScriptEngine, LuaScriptObject            │
│  Script game memanggil C# via wrapper _m_/_s_/_g_    │
│  Registrasi: 27.151 entri IDMAP0 "Oak-<Kls>-<Mbr><n>"│
├─ L2 MANAGED (C# / il2cpp) ───────────────────────────┤
│  Oak.* — seluruh logika game (Character, Battle, ...) │
│  libil2cpp.so (arm64, jalan via houdini translator)   │
├─ L1 NATIVE HOST ─────────────────────────────────────┤
│  x86_64 process (LDPlayer) + houdini (ARM translate)  │
│  Engines kita (WSM x86_64) menyuntik di lapisan ini   │
└───────────────────────────────────────────────────────┘
```

## 1. Rantai Runtime INTI (verified selama sesi)

```
Stage.Instance (static get_Instance)
 ├─ get_CharacterManager()        → CharacterManager        [V]
 │   ├─ GetAllPlayers()           → List<ICharacter>        [V]
 │   ├─ GetAllMonsters()          → List<ICharacter>        [V]
 │   └─ CheckAllMonsterDead()     → bool  (kondisi clear)   [K]
 └─ get_BattleManager()           → BattleManager           [V]

Character (per entity)
 ├─ get_Position()                → Vector3 (boxed +0x10)   [V]
 ├─ get_ActiveState()             → 0 Disabled/2 Visible/3 Enabled [V]
 ├─ get_CharacterStatsBehaviour() → stats                   [V]
 ├─ get_DamagedBehaviour()        → IDamagedBehaviour       [V]
 ├─ get_OverrideDamageBehaviour() → (fallback)              [V]
 └─ ChangeState(IState)           → state machine           [K]
```

**List layout**: `_items@+0x10`, `_size@+0x18` (int32); elemen array `@+0x20`.
**Boxed value**: payload `@+0x10` (bool 1B, int/float 4B, Vector3 12B).

## 2. Stats & Options (Kategori 01 menu) [V semua]

```
CharacterStatsBehaviour
 ├─ AddCharacterStatsOption(int)    opsi mask
 ├─ RemoveCharacterStatsOption(int)
 ├─ set_Stamina(float) / set_Mana(float)
 ├─ get_HP() / get_MaxHP() / get_IsDead() / get_IsDeadConfirmed()
 ├─ override Damage(DamageInfo)     → void
 └─ OnDeadEvent()                   → dispatch kematian
```

**Mask `CharacterStatsOptions`**: `Immortal=1 · Invincible=2 · NoKnockBack=4 · NoPushDown=8 · NoStun=16 · NoDown=32 · NoArial=64 · NoPoison=128 · All=255`
> Verified: `god=3` (Immortal|Invincible), `poise=52` (4|16|32), `immune=224` (32|64|128), reversible `opts 0xf7↔0xf4`.

## 3. Sistem Damage & Kematian [V — SEMUA terverifikasi]

```
DamageInfo (struct 0x2F8, banyak ObscuredInt — fragile!)
 ├─ set_modifier(Nullable<float>)   → pengganda damage
 ├─ set_critical(bool) / set_notMortal(bool)
 ├─ set_stunFactor(int) / set_stunResult(int) / set_stunDuration(float)
 ├─ set_knockBack* (dir/force/factor)
 └─ GenerateTrapDamage(IFieldObject target, int damage) → DamageInfo (boxed)
    GenerateDamageFromLua(15 arg)  → ⚠️ NULL-deref di build ini (DILARANG)

RANTAI PEMBUNUHAN RESMI (terbukti visual + stage clear):
 1. info = GenerateTrapDamage(mon, 1jt)          [factory resmi]
 2. set_notMortal(false)                          [trap = non-lethal by design]
 3. stats.Damage(info)                            [HP → 0]
 4. db = char.get_DamagedBehaviour()
 5. db.Damage(info)                               [reaction handler, return bool]
 6. db.Die(info)                                  [pelatuk kematian + despawn]

MonsterDamagedBehaviour (komponen di monster)
 ├─ Damage(DamageInfo) → bool   (dipanggil jalur damage asli)
 ├─ Die(DamageInfo)             (kematian)
 ├─ DieInternal / SendMonsterDyingState (di bawahnya)
 └─ OnDeadEvent(MonsterDeadEvent) (event kematian internal)

Peristiwa/command kematian (belum dipakai, kandidat riset):
 ├─ MonsterDeadCommand.Create(DamageInfo) → Execute(commandTypes)
 └─ MonsterDeadEvent.Create(character, attacker, dir, knockBackResult, force, dir)
```

**Aturan targeting WAJIB** (pelajaran mahal sesi ini):
- Entitas **dummy** parkir di `(999,0,999)` — gebuk = nol efek visual. **Skip |x|>900 atau |z|>900.**
- Monster di layar bisa ber-status **ActiveState=2 (Visible)** — filter `act==3` doang = "semua fitur gak jalan".
- Terima **act 2 atau 3** + filter posisi real + radius dari hero (`get_Position`).

## 4. Sistem Battle [K]

```
BattleManager
 ├─ GetBattleFor(IFieldObject, bool)   → BattleInstance | NULL
 ├─ GetBattleFor(Party, bool)          → overload ke-2 (hati-hati ambiguitas!)
 └─ GetBattleFor(IList<ICharacter>)    → overload ke-3
BattleInstance
 ├─ Allies / Enemies
 └─ ResetAggro(IFieldObject)
```

> **[V hasil tes]** Di stage field, `GetBattleFor(...)` = **NULL** untuk monster maupun hero (battle instance tidak eksis di luar pertempuran aktif) → fitur aggro = **tidak didukung jalur ini**.

## 5. State Machine & Teleport **[V — TELEPORT LIVE 2026-10-06]**

```
ChangeState(IState state)                  → method publik (instance tertentu, cek kelas target)
CharacterTeleportState.Create(character, Vector3 targetPos, float duration = 0)
CharacterTeleportState.Create(character, pos, duration, fx..., transitionType, crashOnExit, setInvincible, ...)
CharacterExtensions.MarioJump / RecoilType1 / CancelRecoilType1
IFieldObjectExtensions.MoveTo(...)         → pathfinding move
MagicPortalBehaviour.Teleport(...)
```

**[V] Teleport via `set_Position` (plain) TERBUKTI LIVE**: tulis posisi + read-back = target,
nol fault (`TP ... now=(16.8,14.2) ok=1`). Fitur menu v5: 4 arah ±15m (kategori 05).
Jalur state resmi (`CharacterTeleportState` + state machine) = riset lanjut (fx + invincible param).

## 6. Buff [K]

```
BuffManager
 ├─ AddBuff(sender, slot, target, buffSpecName, level, showFx, showTxt) → bool
 ├─ AddBuffAlly(Character sender, string buffName, int level, ...)
 ├─ AddShieldBuff / CureAllDebuffs
BuffClassName enum (contoh): Invincible=59 · HealOverTime=68 · StackableSpeedScale=47
                            · CriticalMultiplierScale=46 · FinalAttackDamageScale=57 · KnockBackDefense=84
```

**Implikasi:** `StackableSpeedScale` = kandidat fitur **Super Speed** (via buff resmi, bukan hook) [P].
Nama spec buff berupa string — perlu enumerasi dari asset/metadata [P].

## 7. Skill / Cooldown [K]

```
Exposed ke Lua: SetCoolTime / ResetCoolTime / get_CoolTime  (kelas skill spesifik)
set_stunFactor(int) / set_stunDuration(float) pada DamageInfo [K]
```

> Fiture "Skill Tanpa Cooldown" [P] — kandidat: panggil ResetCoolTime per skill action.

## 8. Pergerakan & Dunia [K]

```
get_Position/set_Position (IFieldObject eksplisit impl)
CharacterExtensions (MarioJump, Recoil, Custom animasi)
AutoNavigateState (!!) — auto-move/pathfinding state tinggalan dev — [GEM]
```

## 9. Event & Command Pipeline [K]

```
IEventListener / Event system (Create/Dispose/PooledEvent)
Command pipeline: PooledCommand<T> + EnqueueCommand(Command) + Execute(CommandTypes)
   contoh: MonsterDeadCommand (CommandName MonsterDead = 122)
StageRewardsEvent.Create(items, stageType, clear) — event reward/hasil stage
```

**Implikasi:** kita bisa **enqueue command resmi** (mis. membuat MonsterDeadCommand sendiri) — kandidat kill path alternatif [P].

## 10. API Script Layer (xLua) — 27.151 entri

**File**: `lua_api_idmap.txt` — format `Oak-<Class>-<Member><n>`, n = indeks overload.

**Konvensi wrapper (kode bridge C# di dump):**
| Prefix | Arti | Jumlah |
|---|---|---|
| `_m_Xxx(IntPtr L)` | method/instance call dari Lua | 1.329+ (1.995 total kemunculan) |
| `_s_Xxx(IntPtr L)` | static call | 1.069+ |
| `_g_Xxx(IntPtr L)` | getter global | 1.550+ |
| `__Gen_Wrap_*` | generated dynamic wrapper | 8.774 |

**Infra**: `LuaEnv`, `LuaScriptEngine`, `LuaScriptObject`, `XLua_Gen_Initer_Register__`, `LuaScriptUtil`, `XLuaUnityDefaultConfig`.

**Highlight dari permukaan API (hasil grep):**
- `Oak-CharacterExtensions-Teleport0` [GEM]
- `Oak-IFieldObjectExtensions-MoveTo0` [GEM]
- `Oak-ItemExtension-UnLock0` [GEM]
- `Oak-BattleAIChinaTesterState-*` [GEM — AI tester dev]
- `Oak-AutoNavigateState-*` [GEM — auto-walk]
- `Oak-CommonScreenplay-MoveTo` (sinematik)

## 11. Sistem Lain (index cepat)

| Sistem | IDMAP | Status |
|---|---|---|
| BattleInstance | 60 entri | [K] |
| BattleManager | 78 entri | [K] |
| DamageInfo | 70 entri | [V sebagian] |
| MonsterDead | 9 entri | [K] |
| BuffManager | 13 entri | [K] |
| Character/Monster core | 1.655 entri (lihat `sys_core_idmap.txt`) | campuran |

## 12. Peta Jadi — Rekomendasi Prioritas Riset Lanjutan

1. **Teleport** (`CharacterTeleportState.Create` + state machine) — fitur menu baru, risiko rendah, API resmi.
2. **Super Speed** via buff `StackableSpeedScale` — perlu enumerasi nama spec buff.
3. **Skill no-CD** via `ResetCoolTime` per skill.
4. **Kill path v2** via `MonsterDeadCommand` — lebih "resmi" dari combo sekarang.
5. **Command pipeline generik** — menjalankan command internal game (termasuk yang tak punya wrapper Lua).
6. **methodPointer patching** → lihat `SCRIPT_HOOKING.md`.

---
*Semua temuan [V] punya evidence di `evidence/` (g13–g18).*
