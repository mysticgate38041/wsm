# WSM v2 — PETA SISTEM LENGKAP (AUDIT TOTAL)
*worm shadow · untuk He · 2026-10-07 · sumber: baca langsung seluruh kode (jni/engine.cpp 5154 baris, jni/loader.cpp, payload/h64.cpp, menu/WsmMenu.java 670 baris, wsm_protocol.h, build.ps1, build_menu.sh, Android.mk, module-template, layout device)*

---

## 0. IDENTITAS

| Item | Nilai |
|---|---|
| Nama | WS Menu (WSM) v2 — "wsm_gt" |
| Build stamp | `wsm-v5.0.0-modern` (banner di handshake) |
| Bentuk | Magisk module + Zygisk loader (x86_64 + arm64-v8a) + engine in-process + payload arm64 via native bridge |
| Target | `com.kakaogames.gdts` (+ fixture `com.wsm.fixture`) — exact match allowlist |
| Prinsip | Evidence-label (LIVE/UJI/SEBAGIAN/SERVER/RISET), fail-safe, reversible, stealth, single load decision |

## 1. RANTAI EKSEKUSI (boot → menu hidup)

```
Magisk boot → ZN (ZygiskNext) load zygisk/x86_64.so  [LOADER]
 └ preAppSpecialize : allowlist exact-match → baca engine/x86_64.so (validate ELF magic/machine, ≤32MB)
                      + baca dex/wsm_menu.dex (≤4MB) — TANPA fd tersisa
 └ postAppSpecialize: memfd_create ×3:
        "wsm_payload" ← engine bytes;  "wsm_dex" ← menu dex;  "wsm_chan" ← 4KB kanal
        env: WSM_CHANNEL_FD / WSM_PROTOCOL / WSM_NONCE / WSM_DEX_FD
        spawn worker + sinyal; lalu g_api->connectCompanion() (arm64 companion via ZN)
 └ worker : dlopen("/proc/self/fd/N") → JNI_OnLoad(engine) → poll HANDSHAKE (≤10s) → PROBE (≤150s)
     ↓ (chan memfd) HELLO {magic WSMH, proto 2, pid, uid, nonce, build, abi}
[ENGINE x86_64 di dalam game]
 └ JNI_OnLoad → duplicate-guard → engine_thread:
     • buka WSM_CHANNEL_FD → tulis ACK {WSMA, nonce_echo, pid/uid, state=READY, caps}
     • jni_probe (fixture, senyap di game)
     • tunggu libil2cpp ≤30s (scan /proc/self/maps) → safe_elf_parse (fault-guarded, retry semua
       kandidat base — alias houdini!) → resolve 16 symbol API il2cpp (kSymbols)
     • query-phase G7-G9 (read-only) → tulis PROBE {base, sym N/M, dom/asm/img/cls/mth/fld,
       inst, smdt, modsN}
     • spawn 4 thread: MENU / CTL / FEAT / AUTOHOOK  + watcher 8s-beat
[PAYLOAD arm64 — h64]
 └ engine load via native bridge: NativeBridgeLoadLibraryExt(libh64z.so, flags=2)
   (houdini MENOLAK API v1 deprecated; Ext = jalur resmi)
 └ ctor payload: baca h64_bus.txt (pointer bus dari engine) → spawn agent thread
 └ agent: bus CMD 1..15 (ping/mul/read64/write64/patchself/HOOKINSTALL v3/RESTORE/READ/
   SELFTEST/RAWPATCH/GUARD/GUARDREPORT/GODBLK/exit) → pasang hook arm64 absolut
```

## 2. TIGA JALUR KONTROL

| Jalur | Media | Pemakai | Isi |
|---|---|---|---|
| A. Chan memfd 4KB | memfd `wsm_chan` (POD struct) | loader ↔ engine | handshake + probe report |
| B. File channel G11 | `wsm_cmd`→`wsm_ack` (multi-path: folder game D2 + modules path), poll 1s + dedupe | He (adb) ↔ engine | **50 perintah ctl_exec** |
| C. Menu | native `exec(String)` (RegisterNatives) → langsung ctl_exec | WsmMenu.java | feat/panic/status/sweep/tpr/... |

**Daftar 50 perintah ctl_exec (inventaris penuh dari kode):**
`aggro1, clearmods, droff, dron, drread, feat, featdiag, godmode, guardhp, guardoff, guardread, hookabs, hookall, hookcount, hookdump, hookprobe, hookread, hookself, hookverify, kcmd, kill1..kill8, klassof, mdmg, mlist, mnear, mod, mpos, mread, msweep, nbdl, nbpoc2, nbpoc3, panic, payloadrun, peek, pulse1, pulsesrc, resetmax, setmax, status, stunprobe, stunui, stunvec, sweep, tp, tpr, unmod`

## 3. MENU (WsmMenu.java — Java overlay, BUKAN ImGui!)

- Engine = 0 ImGui (terverifikasi grep). Menu = Android View asli, dimuat dari DEX via **InMemoryDexClassLoader**, class `wsm.WsmMenu`, dipasang lewat reflection `ActivityThread.mActivities` → attach ke Activity game (fallback: overlay TYPE_APPLICATION_OVERLAY).
- 7 kategori / **37 item**: Statistik&Karakter (god/hp/stam/mana/poise/immune), Pertempuran (ohk/aura/dmg/onehp/crit/stunall/sweep/loadout), Ekonomi (6×SERVER), Progresi (5×SERVER), Pergerakan (tpE/W/N/S, aggro, speed), Visual (lootesp/enemyesp/freecam/fov), Sistem (timescale, paytest).
- Widget: toggle pill, SeekBar slider, PANIC (exec "panic" + reset UI), minimize pill "Ω WSM · N aktif", drag header, risk chip (STEALTH <8 ELEVATED else EXPOSED dari 37), log footer, auto status-poll 6s.
- Jalur klik: `exec(cmd)` → menu_exec() → ctl_exec() (flag g_menu_ctx utk defer ke ticker feat).

## 4. FITUR ENGINE → MEKANISME

| Kelompok | Fitur | Mekanisme (kode) |
|---|---|---|
| Options-mask | god(3=Immortal|Invincible), hp(1), stam, mana, poise(52), immune(224) | `AddCharacterStatsOption(stats, mask)` ke tiap player (via invoke) — jalur resmi game |
| Pulse tempur | ohk, onehp, dmg(×100k via set_modifier/GenerateDamageFromLua), crit(set_critical), stunall (state/event vector, default vec3), aura (radius sweep), aggro (BattleInstance.ResetAggro / sweep) | feat_thread beat: feat_kill_try / feat_pulse_kill / feat_pulse_stun / feat_aggro_sweep |
| Kill machinery | kill1..8 (mode), sweep/msweep (semua musuh stage), mdc.Execute + set_Target | invoke MonsterDeadCommand |
| Timescale | mod/unmod/clearmods/setmax/resetmax (kelas query + SetMaximumDeltaTime + Time.timeScale) | ctl_* machinery |
| Teleport/drone | tp x z (absolut), tpr dx dz (relatif), dron/dronew hero (kamera) | set_Position (E dulu, P fallback); drone via pos watcher |
| HOOK arm64 | autohook (18 slot begitu boot, marker-gated), hookabs <slot> <addr>, hookself, hookverify2, hookread, godmode | payload h64 (blok absolute-jump); bus[59/60/70+s/88/69] |
| Diagnostik | mlist/mpos/mnear/mdmg, klassof, peek, kcmd, pulse1, diag, stunprobe, hookprobe, hookdump, featdiag, mread | read-only invoke + memcpy |
| Watchpoint DR | dron/drread/droff | child ptrace async-safe (raw syscalls) + debug registers |
| Guard page | guardhp/guardread/guardoff | mprotect page-trap + SIGSEGV handler bersama |

## 5. PAYLOAD ARM64 (h64.cpp)

- `__attribute__((constructor))`: baca `/…/files/h64_bus.txt` (bus ptr), spawn agent, tulis h64_alive.txt.
- **NSLOT=24, blok 36 word**; `patch_aliases` = patch SEMUA alias mapping (file-offset match — file double-mapped!); patch = `ldr x17,#8; br x17; <block>` (range-free).
- `build_hook_block` = counter + orig16 + jump balik code+16 (TERBUKTI: 38→75).
- `build_god_block` = ldr literal hero_stats → cmp x0 → b.eq skip (mov w0,#0; ret) | jalur normal counter+orig16+jump.
- `install_god` + safety-check (`already`); `restore_hook` (hanya alamat terakhir — lubang yang ditemukan malam ini).
- CMD15 = GODBLK (slot/code/hero_stats via bus[59][60][88]).
- Di modul juga ada `payload/libh64.so` (copy lama) — yang aktif = `libh64z.so` di lib dir game.

## 6. MODUL & BUILD

- **module-template**: `customize.sh` (BOOTMODE, API≥26, Magisk≥26000, ABI x64/arm64, file check), module.prop (id=wsm_gt), skip_mount, META-INF installer.
- **Layout device** `/data/adb/modules/wsm_gt/`: `zygisk/{x86_64,arm64-v8a}.so` (loader), `engine/{x86_64,arm64-v8a}.so`, `dex/wsm_menu.dex`, `payload/libh64.so`, marker `autohook`, post-fs-data.sh/service.sh/sepolicy.rule (kosong).
- **build.ps1**: NDK pinned 27.2.12479018 + pin hash zygisk.hpp; ndk-build (loader+engine ×2 ABI, APP_STL=none, android-26, -fno-exceptions/rtti); ASSERT keras: ELF machine, LOAD align ≥16KiB (max-page-size 16384), GNU_RELRO 16KiB-align, symbol wajib (zygisk_module_entry / JNI_OnLoad), tolak libc++_shared+(TEXTREL); menu dex: javac(8)→d8(min26, android-34); rakit zip (template + loader×2 + engine×2 + dex).
- **build_menu.sh**: JDK17 target 8 + d8 → `menu/wsm_menu.dex` (±22.5KB).

## 7. KEAMANAN / STEALTH / KONTRAK

- Allowlist exact; non-match SENYAP; fd tidak disimpan; read bounded+EINTR; FNV64 hash engine; satu keputusan load (tanpa fallback otomatis).
- Semua sentuhan il2cpp lewat GUARDED_* (fault di-skip, bukan mematikan game); watcher read-only; hook/watchpoint semua reversible.
- Label menu jujur (SERVER utk ekonomi/leveling; RISET utk speed/ESP).
- Fault semantics (v21): fault di luar guard = `SIG_DFL + raise` → mati (by design, agar tercatat normal).
- Evidence: BRANCH_NOTES A01–A20 (closure table), evidence/g10–g18 logs.

## 8. STATUS & ISU TERBUKA (per malam 2026-10-07)

**Sehat & terbukti:** injeksi→handshake→menu→ctl→feat→hook; absolute-hook (counter 38→75); god-block terpasang; guard bekerja.
**Bug ditemukan malam ini (fix v28 disiapkan):**
1. **Slot-rebind poison** — 1 slot = 1 blok bersama; rebind tanpa restore alamat lama → alamat lama eksekusi blok baru → crash `kena pukul` (3×). Fix: restore-alamat-lama + larangan orig-kosong + godmode→slot bebas (18).
2. **Resolver nyasar (`cgm`)** — kembalikan alamat folded/salah utk beberapa method (slot13/A_auto ≠ A_real). Fix: validasi `== bias + dump_offset`, fallback absolut.
3. **Watchdog LDPlayer** pasca game-crash (signal 9/`appstore`) → cure = restart host LDPlayer; no-tombstone utk crash bridge (pakai logcat signal).
**Belum ada:** speed, ESP (RISET), beberapa fitur kategori SERVER (memang bukan wilayah klien).

## 9. BARIS BESAR YANG PERLU DIINGAT

- **Dua dunia ABI:** loader & engine punya build x86_64 + arm64; di LDPlayer proses = x86_64 → yang hidup = `zygisk/x86_64.so` + `engine/x86_64.so`; `payload/h64.cpp` (arm64) masuk via **native bridge** (jalan khusus yang membuat kode arm64 hidup di dalam proses x86).
- **Tiga kanal, tiga audiens:** memfd (loader↔engine), file G11 (He↔engine), menu exec (UI↔engine) — semuanya bertemu di `ctl_exec`.
- **Menu = Java** (bukan ImGui) → semua interaksi UI lewat `exec(String)` — termasuk yang kita tap malam ini.
- **Hook absolut + non-ASLR** = keunggulan unik kita vs Shaizuro/Nifuji/VounderS (mereka by-name/offset-server; kita dump-verified offline).
