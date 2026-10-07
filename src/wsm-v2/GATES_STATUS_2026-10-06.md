# WSM v2 — STATUS GATE & MILESTONE
*worm shadow · 6 Okt 2026 · status terakhir ~11:25*

## Ringkasan
WSM v2 (WS Menu) — modul Zygisk dual-ABI yang berjalan di LDPlayer 14 (Android 14, x86_64 + houdini),
menggunakan arsitektur loader x86_64 + engine di-load via dlopen dari memfd. Fase saat ini: **read-only**
(tidak ada penulisan ke state game). Semua milestone diuji di fixture sendiri DAN di game asli (Guardian Tales 3.54).

## Gate
| Gate | Isi | Status | Bukti |
|---|---|---|---|
| G2 | Build bersih (loader x2 + engine x2, ELF checks, NDK pinned 27.2.12479018) | ✅ | `dist/wsm-v2.0.0-poc1.zip` |
| G3 | Fixture: staged → ENGINE_DLOPEN → JNI_OnLoad → ALIVE → HANDSHAKE_OK → callback 2-arah | ✅ | `evidence/g3-retest-v2c-*.log` |
| G4 | Fixture relaunch (pid baru, chain penuh lagi) | ✅ | `evidence/g4-relaunch-*.log` |
| G5 | GT asli: inject + handshake (uid 10075) + probe maps (libil2cpp terlihat) dengan Nifuji aktif coexist | ✅ | `evidence/g5c-gt-full-*.log`, `gt-running-with-wsm.png` |
| G6 | Resolusi simbol il2cpp via **ELF dynsym parse** (tanpa dlopen; aman utk lib ARM64 di proses x86_64) | ✅ 9/10 | `evidence/g6-gt-elf-*.log` — `dg`/`cfn` alamat asli; absen: `il2cpp_method_get_pointer` (tidak diexport GT) |
| G7 | **Query by-name live di game**: `il2cpp_domain_get` → `domain_assembly_open("Scripts")` → `assembly_get_image` → `class_from_name`; thread_attach dipanggil | ✅ **FULL — cls≠0** | `evidence/g7g-gt-class-*.log` — `cls=0x799f531be6c0` (GlobalTimeManager ditemukan by-name di GT live!) |
| G8 | **Method by-name**: `il2cpp_class_get_method_from_name(cls, "get_Instance", 0)` → MethodInfo* | ✅ | `evidence/g89c-gt-final-*.log` — `mth=0x79f8e3a40938` |
| G9 | **Field by-name + offset + static value LIVE**: `class_get_field_from_name` → `field_get_offset` → `field_static_get_value` + **watcher permanen v2n** | ✅ **FULL — nilai live non-null** | `evidence/g9-watch-live-125244.log` — `WATCH static: mods=0x795caae75330 instance=0x795c9a779a40 (t=96s)` (ketangkep otomatis pas He masuk gameplay!) |
| G10 | **Live object read + managed invoke**: `il2cpp_field_get_value` baca `storedMaximumDeltaTime` (Nullable&lt;float&gt;) dari instance + raw List size + **`il2cpp_runtime_invoke(get_Instance)`** | ✅ **FULL — match_static=1** | `evidence/g10-invoke-live-143332.log` — `INVOKE get_Instance -> 0x795c9a07ec80 match_static=1`; `smdt=0.0000(has=0) modsN=0` (semantik benar: belum ada override/entry) |
| G11 | **Control plane + fitur pertama**: channel file `wsm_cmd`/`wsm_ack` + command `mod/unmod/clearmods/setmax/resetmax/status` via `il2cpp_runtime_invoke` ke API resmi game (`GlobalTimeManager.Mod`/`Unmod`/`Clear`) | ✅ **LIVE** — He merasakan slow-mo (0.5/0.25) & fast (2.0); `modsN` verify 0↔1; `clearmods` = revert handal | `evidence/g11-ctl-live-*.log` |
| G12 | **Menu in-game (overlay)**: DEX di-load in-memory + **view di-attach ke Activity GT** (via JNI `mActivities` — tanpa izin overlay!) + tombol → native `exec` → `ctl_exec` + status feedback di panel | ✅ **MENU MUNCUL + TOMBOL RESPONSIF** (tap terverifikasi) | `evidence/menu-visible-proof.png`, `g12-menu-live-155437.log` |
| G13 | **Menu PREMIUM** (port penuh desain "Premium Mod Menu Desain.zip"): 8 kategori / 47 fitur, rail + kartu + toggle/slider, chip READY/risk live, PANIC, log aktivitas, pill minimize, status-poll 6s | ✅ **LIVE DI LAYAR** — tap → `feat god 1 → PENDING`; risk chip berubah `STEALTH→ELEVATED 1/47`; log update | `evidence/g13-premium-menu.png`, `g13-premium-menu-fixed.png`, `g13b-premium-*.log` |
| G14 | **Framework fitur GT (v3.0→v3.4)**: `Stage.Instance → CharacterManager.GetAllPlayers() → Character → CharacterStatsBehaviour → AddCharacterStatsOption/RemoveCharacterStatsOption/set_Stamina/set_Mana` — 5 fitur statistik LIVE + readback + reversibility + one-shot apply | ✅ **VERIFIED DI PLAYER (DUA ARAH)**: `opts=0x3` (getter game), **He: "HP aman"** digempur · stam/mana "aman" · poise+immune survive · **god OFF → "darah turun" (He), god ON → kebal lagi**; `opts 0xf7→0xf4→0xf7` bit-clean, faults=0 | `evidence/g14-feats-live-173716.log`, sesi 17:31–17:50 |

## G14 — pelajaran teknik kunci (WAJIB DIBACA SEBELUM NYENTUH LAGI)
1. **Array il2cpp: elemen mulai di +0x20** (klass@0, monitor@8, bounds@0x10, len@0x18). Baca di +0 = klass-ptr (sampah yang deterministik — ini nyamar jadi bug selama 2 ronde).
2. **Field-read (`characterStatsBehaviour@0x130`) FAULT di build ini** (`il2cpp_field_get_type` SIGSEGV) → pakai **method getter** (`get_CharacterStatsBehaviour`) — return valid; jangan balik ke field read kecuali diinvestigasi.
3. **Desain apply: ONE-SHOT per toggle** (version counter). Spam re-apply tiap 0.9s = **game self-quit** 100ms setelah apply massal (forensik: `poise` kill bukan bit-nya — desain spam-nya). v3.4 one-shot: semua aman.
4. **Game mati diam tanpa tombstone + "has died: fg TOP"** = app **keluar sendiri** (unhandled exception → Quit khas Unity), bukan native crash. Cek logcat `ActivityManager` + `houdini backtrace` untuk forensik.
5. `set_Stamina/set_Mana` top-up per ~0.9s = aman & terasa; biarkan di ticker, jangan di-spam paralel dari ctl.
6. readback bukti: `get_CharacterStatsOptions()` → boxed enum → unboxed di `+0x10`.
7. **JANGAN tulis ke game dari UI thread** (tap menu). Semua apply WAJIB dari thread engine. v3.5: `menu_exec` set `g_menu_ctx` → `ctl_exec` cuma ubah state + `g_feat_version++` → balas `QUEUED` → ticker apply ≤1s pada feat-thread (tervalidasi: 3 tap → 3 apply, game hidup terus). Timescale dari menu → `g_pending_ts` + `ctl_ts_apply` di ticker.
8. **Guard SIGSEGV: TLS + install sekali.** Satu `jmp_buf` global bisa cross-thread longjmp (fault di thread A melompat ke stack thread B) = crash. v3.5: `__thread` jmp_buf/flag + `guard_install_once()` (berhenti churn sigaction per window); fault tak-terlindungi → restore SIG_DFL + raise (biar tombstone normal).

## G13 — catatan fitur (jujur)
- **Wired (real):** `timescale` (slider → `Mod`/`Clear` via API game, perlu gameplay), `PANIC` (→ `Clear()`), `status` poll.
- **PENDING (UI lengkap, hook belum):** 45 fitur lain — tiap satu butuh RE kelas target di dump (`Oak.BattleManager`, dll) + hook/invoke + verifikasi per-fit. Backlog berurutan.

## G12 — teknik menu (tervalidasi)
- **Overlay `TYPE_APPLICATION_OVERLAY` DITOLAK** untuk GT (`BadTokenException: permission denied for window type 2038`) — GT tidak mendeklarasikan `SYSTEM_ALERT_WINDOW`, jadi tidak muncul di Settings overlay-permission dan appops set tidak nempel (LDPlayer).
- **Solusi**: attach view **ke dalam Activity game** — `ActivityThread.currentActivityThread()` → `mActivities` (ArrayMap) → record.`activity` → `findViewById(android.R.id.content).addView(panel)`. **Tanpa izin apapun.** (JNI ke hidden member justru LOLOS di device ini.)
- **Build DEX harus menyertakan SEMUA `.class`** (inner `$1/$2/$3`) — kalau tidak: `NoClassDefFoundError: wsm/WsmMenu$1` saat runtime.
- DEX + engine = **hot-swappable per-spawn** (loader baca file fresh tiap app start); hanya loader yang butuh reboot.

## Fitur engine v2p (G10)
- **Instance snapshot**: baca field instance dari objek `GlobalTimeManager` (`il2cpp_field_get_value`, Nullable&lt;float&gt; layout: has@+0, value@+4).
- **Raw List read**: `List<T>` internals dari pointer object (`_items`@0x10, `_size`@0x18 setelah header 0x10) — `modsN` = jumlah entri mods live.
- **Managed invoke**: `il2cpp_runtime_invoke(method, NULL, NULL, NULL)` untuk static getter — hasil = pointer objek, dibandingkan dengan static field (terbukti sama).
- **Watcher v2p**: log transisi + snapshot `smdt/modsN` tiap perubahan + heartbeat 4 menit (`WATCH alive`).

## Kilasan teknologi inti (tervalidasi)
- **Assembly utama GT = `Scripts.dll`** (Image 0 di dump.cs), BUKAN Assembly-CSharp.
- libil2cpp punya **dua set mapping** (view native `0x4000...` + alias houdini ~170MB `0x77...`); engine memilih mapping offset-0 dan **retry semua kandidat** sampai parse sukses.
- Resolusi simbol via **ELF dynsym parse murni** (tanpa dlopen) — aman untuk lib ARM64 di proses x86_64.
- **Fault-guard WAJIB** (sigsetjmp/SIGSEGV): mapping alias yang cuma sebagian ter-backing akan SIGSEGV saat parse — guard bikin kandidat itu ditolak tanpa pernah mematikan game (pelajaran dari 16 crash beruntun v2j — semua tombstone menunjuk `/memfd:wsm_payload`).
- **Fault-guard WAJIB di SEMUA panggilan il2cpp dari thread kita** (bukan cuma ELF parse): insiden 14:33 (tombstone_13) — watcher v2p yang TANPA guard memanggil API field il2cpp saat game sibuk → NULL-deref di `Thread-3` (thread engine kita, tid 6644) → GT crash saat He main. **v2q**: semua call (`fsgv/fgv/invoke`) dibungkus `GUARDED_BEGIN/END` + counter faults → fault = call di-skip + log `WATCH guard: N faulted call(s) skipped`, game **tidak pernah** mati karenanya.
- **BUKTI LIVE guard (14:48, sesi v2q pid 7679)**: pola glitch identik dengan crash 14:33 muncul lagi ≈8 detik setelah enter gameplay → `WATCH guard: 1 faulted call(s) skipped (total=1, t=960s)` → game **SELAMAT** (invoke + live values jalan normal di beat berikutnya; NOL tombstone baru). Evidence: `g10b-guard-proof-145314.log`.
- Query API dipanggil lewat pointer hasil parse + `il2cpp_thread_attach` — semua read-only.

## Next (G10+)
1. **G10 — Instance/live object**: `il2cpp_runtime_invoke(get_Instance)` atau `il2cpp_object_*` untuk baca object field (battle/player state) — fase lanjut.
2. **Control plane**: command/ack (desired≠applied≠observed), UI menu (Java DEX phase), toggles fitur.
3. **Fitur (fase tulis — hati-hati)**: sesuai roadmap WSM (speed/god-mode dsb) dengan fail-safe + reversible patch.

---

## G15/G16 — PEMBUNUHAN VISUAL + INTEGRASI MENU (2026-10-06 malam)

**Temuan kematian musuh (G15):** `stats.Damage(info)` sendirian = musuh "keranda beku" (HP 0, diam, stage tak selesai).
Rantai BENAR: `GenerateTrapDamage(mon,1jt)` → `set_notMortal(false)` → `stats.Damage(info)` → `get_DamagedBehaviour().Damage(info)` → `Die(info)`.
**Targeting wajib via `get_Position()`**: entitas dummy parkir di (999,0,999) — digebuk = tidak ada efek visual. Musuh di layar bisa ber-status **ActiveState 2 (Visible)** — filter `act==3` doang = "semua fitur gak jalan" (pelajaran v4.0d). Terima **act 2|3** + filter posisi.
**Verified:** musuh di samping hero mati visual (3→2→0 di layar), stage SELESAI, "udah work bro".

**Crash-proof v3.10+ (pelajaran mahal):** poison pointer `0xdead1031` di static game → watcher deref buta → game mati. Fix: `ptr_ok` di semua deref + **guard per-beat** (jmp_buf thread-local terpisah, nested GUARDED tetap jalan).

**v4.0 (menu+engine integrasi):** feat: ohk/aura(radius)/dmg(×100rb)/crit/onehp/stunall/aggro + cmd `sweep`; semua [LIVE]/[UJI]/[SERVER]/[RISET] di menu sinkron kenyataan. Fix v4.0e: onehp butuh gate `ohk||onehp` + ohk menang atas onehp.
