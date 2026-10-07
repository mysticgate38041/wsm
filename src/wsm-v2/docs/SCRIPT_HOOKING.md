# SCRIPT_HOOKING — Arsitektur Hook Internal & Lapisan Script Guardian Tales

> Disusun: 2026-10-06 · Basis: dump 2.2 juta baris + engine WSM v4.0 (terverifikasi)
> Prinsip: setiap teknik ditandai **[V]** terverifikasi · **[K]** dari dump · **[P]** rencana

---

## 0. Ringkasan Eksekutif

Game ini dibangun **tiga lapis** (lihat `API_MAP_FULL.md` §0). Untuk "menguasai" seluruh sistem,
ada **enam tingkatan intervensi** dari yang paling halus sampai paling dalam:

| # | Teknik | Kedalaman | Status WSM |
|---|---|---|---|
| 1 | Panggil API resmi via `il2cpp_runtime_invoke` | L2 managed | **[V] ENGINE SEKARANG** |
| 2 | Enqueue command internal (`PooledCommand`) | L2 managed | [P] |
| 3 | Event system (subscribe/emit) | L2 managed | [P] |
| 4 | Panggil/menimpa fungsi Lua terdaftar | L3 script | [P] |
| 5 | **methodPointer patching** (hook fungsi managed) | L2 binary | [P — dokumen ini] |
| 6 | Hook native ARM64 langsung (houdini-side) | L1/L2 native | [P — dokumen ini] |

---

## 1. Lapisan Script xLua — Cara Kerja Internal [K]

### 1.1 Arsitektur binding
```
Script Lua (asset .lua di bundle)
   │  panggil fungsi global/table
   ▼
xLua runtime (libxlua di dalam libil2cpp / libunity)
   │  memanggil fungsi wrapper C# terdaftar
   ▼
Wrapper statis:  _m_Xxx(IntPtr L) / _s_Xxx(IntPtr L) / _g_Xxx(IntPtr L)   ← 3.9k+ wrapper
   │  baca argumen dari Lua stack → konversi → panggil C# asli
   ▼
Method C# oak asli (Character, Battle, dst.)
```

### 1.2 Registrasi
- **27.151 entri IDMAP0** format `Oak-<Class>-<Member><n>` (file `lua_api_idmap.txt`).
- Konstanta contoh: `Oak-CharacterStatsBehaviour-OnDeadEvent0 = 19512`, `Oak-MonsterDeadCommand-Create0 = 20993`.
- `XLua_Gen_Initer_Register__` = titik inisialisasi seluruh wrapper generated.
- `__Gen_Wrap_<n>(...)` = 8.774 adapter runtime (dipakai untuk delegate/panggilan dinamis).

### 1.3 Implikasi untuk kita
1. **Peta API**: file `lua_api_idmap.txt` = katalog lengkap fungsi yang PENGEMBANG game sendiri
   pakai untuk scripting → prioritas tertinggi untuk dipanggil via invoke (teknik #1).
2. Untuk memanggil wrapper `_m_*` langsung (teknik #4) kita butuh pointer **lua_State**
   yang hidup: didapat dari `LuaEnv` (kelas `LuaEnv : IDisposable`, punya instance runtime).
   Alur riset: `LuaEnv` instance → field `L`/state → push argumen ke stack → panggil wrapper.
   **Kompleks tapi sangat kuat** (semua yang script bisa, kita bisa).

---

## 2. Teknik #1 — Invoke API Resmi (yang sudah jalan) [V]

Sudah jadi tulang punggung engine WSM:
- Resolver by-name: `il2cpp_class_from_name` + `il2cpp_class_get_method_from_name`.
- Panggil: `il2cpp_runtime_invoke(method, obj, args, exc)` — virtual-dispatch jalan.
- Semua dibungkus 3 lapis guard: `ptr_ok` + per-call jmp + per-beat jmp.

**Batas teknik ini:** hanya bisa memanggil method yang ADA & method-nya tidak butuh state
internal yang tak tersentuh. Tidak bisa men-*intercept* (melihat/mengubah argumen saat game
memanggilnya sendiri). Untuk itu → teknik #5.

---

## 3. Teknik #2 — Command Pipeline [P]

```csharp
class PooledCommand<T> { Execute(CommandTypes) ... }
MonsterDeadCommand.Create(DamageInfo)     // contoh sudah diketahui
EnqueueCommand(Command)                    // jalur masukan (ditemukan di controller)
```

**Rencana:** membuat instance command via `Create(...)` lalu `Execute(0)` langsung
(tanpa queue) — kandidat kill-path resmi kedua (setelah combo Damage+Die).

---

## 4. Teknik #3 — Event System [P]

- `IEventListener`, `PooledEvent<T>`, contoh: `MonsterDeadEvent`, `StageRewardsEvent`,
  `BattleEnterEvent`, `ActionStateChangeEvent`.
- Setiap event punya `Create(...)` + `Dispose()` di pool.
- **Rencana:** memicu event mati (`MonsterDeadEvent.Create`) atau reward stage
  (`StageRewardsEvent.Create(items, stageType, true)`) langsung ke handler karakter.

---

## 5. Teknik #5 — methodPointer Patching (TRUE HOOKING) [P]

### 5.1 Konsep
Setiap method managed punya struktur runtime:

```
struct MethodInfo {
    Il2CppClass* klass;          // +0x00
    const char*  name;           // +0x08
    void*        methodPointer;  // +0x10   ← TARGET PATCH
    void*        invoker_method; // +0x18
    ...
}
```
Mengganti `methodPointer` = semua pemanggilan berikutnya (termasuk dari Lua/script!)
lompat ke fungsi kita → **intercept penuh** (baca/ubah argumen, skip asli, panggil asli).

### 5.2 ⚠️ MASALAH BESAR DI SETUP KITA — Perang ISA (houdini)
- Proses GT = **x86_64** (LDPlayer), tapi game (libil2cpp) = **ARM64 via houdini**.
- Engine WSM kita = **x86_64** → **TIDAK BISA** menaruh pointer fungsi x86_64 ke
  `methodPointer` milik method ARM64: caller (ARM64 translated) melompat ke kode x86_64
  = crash keras. **Ini kenapa kita selama ini pakai invoke (bridge resmi houdini), bukan patch.**

### 5.3 Solusi arsitektur: **PAYLOAD ARM64 + JEMBATAN FLAG**
```
┌─ x86_64: ENGINE WSM ────────────────────────────────┐
│ komando via memori bersama/flags (tanpa cross-call) │
│  state ter-parsing, method info ter-resolve         │
└──────────────▲──────────────────────────────────────┘
               │ flag region (mmap bersama, atomik)
┌──────────────▼──────────────────────────────────────┐
│ ARM64 PAYLOAD (libhook_arm64.so)                    │
│  dimuat via NativeBridge (System.load → houdini)    │
│  • thread init sendiri (constructor)                │
│  • baca flag → patch methodPointer ARM64 → stub ARM64│
│  • stub: rekam argumen → panggil asli (trampoline)  │
└─────────────────────────────────────────────────────┘
```
**Kenapa jalan:** app ARM64 memuat library ARM64-nya lewat houdini sepanjang waktu
(itu cara libil2cpp sendiri dimuat). Kita menumpang jalur native-bridge yang sama:
`System.load("/data/.../libhook_arm64.so")` dari sisi Java, atau `android_dlopen_ext`
dengan namespace yang tepat.

### 5.4 Langkah verifikasi (POC)
1. `hookprobe` di engine x86_64: resolve method (mis. `get_IsDead`) → baca MethodInfo →
   scan 0x40 byte pertama untuk pointer yang jatuh di rentang eksekusi libil2cpp
   (`0x4000_22c0_0000+`) → laporkan offsetnya (harapan: `+0x10`).

   **[V — TERVERIFIKASI 2026-10-06]** hasil live `hookprobe` pada `get_ActiveState`:
   ```
   HOOKPROBE mi=0x7eedd2a5ec80 klass=0x40002b833838 name=''
     | code@+0x10=0x4000263d2a14     ← methodPointer CONFIRMED (+0x10)
     | code@+0x28=0x40002cad6610     (kandidat invoker/slot tambahan)
     | code@+0x58/+0x60=0x40002b833880  (kemungkinan VTable-ish)
     | code@+0x68=0x4000263a3e98
   ```
   Catatan: field `name`@+0x8 terbaca kosong pada build ini — untuk debugging nama,
   gunakan `il2cpp_class_get_name` (sudah ada di symbol table WSM), bukan baca +0x8.
2. Bangun `libhook_arm64.so` minimal (constructor + thread + log) → `System.load` →
   verifikasi log muncul di logcat (bukti jalur native-bridge hidup).
3. Uji patch pertama di method berisiko rendah (mis. `SendMonsterDyingState` atau getter)
   → validasi intercept sebelum menyentuh fungsi inti.

### 5.5 Risiko & mitigasi
- **Anti-tamper**: game sudah menunjukkan poison pointer (`0xdead....`) & `MultiPlayHackReporter`
  (mode multiplayer). Semua hook = **PvE/single-player only**; jangan sentuh mode online.
- **Crash**: stub wajib meneruskan ke trampoline asli (panggilan asli tetap jalan);
  patch harus atomik (tulis pointer 8-byte aligned).
- **houdini quirk**: instruksi cache ARM64 — flush jika menyentuh kode (kita hanya
  menulis DATA pointer, bukan kode → relatif aman).

---

## 6. Teknik #6 — Hook Native ARM64 [P]

Setelah payload ARM64 hidup (§5.3), bisa naik level:
- Hook fungsi native libil2cpp (bukan hanya methodPointer): mis. `il2cpp_runtime_invoke`
  internal, atau fungsi damage inti → intercept SEMUA damage game (termasuk serangan asli!)
  → fitur seperti "Damage Multiplier global", "semua serangan = instakill" jadi elegan.
- Menggunakan pola hook standar (branch instruction di prolog / inline hook ARM64).

---

## 7. Urutan Eksekusi yang Disarankan (roadmap teknis)

1. **[Sekarang]** Teknik #1 sudah produksi — engine v4.0.
2. **`hookprobe`** (POC #5.4.1) — rendah risiko, memvalidasi fondasi patching.
3. **Payload ARM64 loader** (POC #5.4.2) — memvalidasi jalur native-bridge.
4. **Patch fungsi rendah risiko** (POC #5.4.3).
5. Baru lompat ke hook yang berdampak (damage pipeline / state machine).
6. Paralel: eksploit permukaan 27k API (teleport, buff, command) — tanpa perlu hook sama sekali.

---

## 8. Peta Cepat: Data Penting di Mana

| Kebutuhan | Lokasi |
|---|---|
| Katalog API lengkap | `docs/lua_api_idmap.txt` (27.151) |
| Wrapper Lua | `docs/lua_wrappers_{m,s,g}.txt` |
| Sistem inti | `docs/sys_core_idmap.txt` (1.655) |
| Peta sistem + rantai | `docs/API_MAP_FULL.md` |
| Bukti verifikasi | `evidence/g13..g18` |
| Dump mentah | `Downloads/Mod/gt_dump/tools/dump.cs` |

---
*Dokumen ini hidup — update setiap teknik naik status.*
