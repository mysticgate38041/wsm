# WSM — DESAIN KERJA v1.0 (Review Sebelum Eksekusi)
**untuk: He** · oleh: worm shadow 🪱 · 6 Okt 2026 · status: menunggu approval

Dokumen ini mengunci **aturan, algoritma, dan pseudocode** WSM sebelum lanjut build & test.
Setelah lu approve (atau koreksi), gw eksekusi persis desain ini.

---

## 0. RUANG LINGKUP & PRINSIP
- **Scope klien**: stat tempur, gerak, visual/ESP, otomasi opt-in.
- **Bukan scope**: hal server-side (gem/gold asli, ranked) — bukan wilayah klien; tidak akan diklaim.
- **Prinsip inti**: fail-safe (gagal = game normal) · reversible (semua bisa dibalik) · verify-before-write · stealth (tanpa jejak) · satu mod saat uji.

## 1. ARSITEKTUR — 3 LAPISAN (hasil bedah Nifuji)
```
┌─────────────────────────────────────────────┐
│ L1  LOADER  (x86_64, Zygisk)                │  ← masuk lewat ZN 1.5.0
│  • filter proses  • stage engine ke memfd   │
│  • System.load → ART menjembatani ABI       │
├─────────────────────────────────────────────┤
│ L2  ENGINE  (ARM64, dunia houdini)          │  ← JNI_OnLoad dipanggil runtime
│  • resolve il2cpp 3 lapis                   │
│  • PatchEngine: field/const/hook/Lua        │
│  • ESP loop  • safety engines               │
├─────────────────────────────────────────────┤
│ L3  UI (Java DEX tersembunyi + ESPView)     │  ← overlay di activity game
│  • menu per-index (toggle + slider)         │
│  • gambar ESP via Canvas                    │
└─────────────────────────────────────────────┘
```
Alasan: game = ARM64 ⇒ semua logika game harus jalan **di dunia ARM64**; sisi x86_64 hanya bootstrap + UI.
Engine disimpan sebagai **memfd** (`/proc/self/fd/N`) supaya terbaca lintas-sandbox & tanpa jejak file.

**Jalur A (utama)** : memfd → `System.load` → `JNI_OnLoad` (ART menjembatani — pola yang dipakai GT sendiri).
**Jalur B (cadangan)**: `dlopen` + `NativeBridgeGetTrampoline` → panggil `wsm_engine_main` langsung.
**Jalur C (asuransi)** : x86-only — patch memori arch-agnostic + offset dari dump kita (tanpa memanggil ARM64).

## 2. ATURAN (dikunci)
### A. Keselamatan
- **R1 Fail-safe** : resolve/patch/verify gagal ⇒ mod diam, game jalan normal. Tidak ada jalur crash.
- **R2 Reversible**: setiap write menyimpan byte asli; **panic** = restore semua + sembunyikan UI.
- **R3 Verify-before-write**: baca target → cocokkan pola harapan (per-versi) → mismatch ⇒ skip + catat.
- **R4 Hook minimal**: handler patch tanpa alokasi/throwing; default = perilaku asli.
- **R5 Stealth**: tanpa file mencurigakan; tidak menyentuh ACTk/MultiPlayHackReporter; log minimal (bisa OFF); nama memfd netral.
- **R6 Auto-disable mode berisiko**: masuk Arena/Coop ⇒ paksa preset MP-SAFE; kembali PvE ⇒ pulihkan preset user.
- **R7 Batas nilai**: slider dibatasi (DMG ≤100×, SPD ≤10×) supaya nilai tidak "mustahil" di layar.
- **R8 Satu mod saat uji**: Nifuji/VounderS OFF selama test WSM.
- **R9 Timing aman**: patch diterapkan **awal** (sebelum fungsi pertama dieksekusi) ⇒ bebas isu cache-translasi houdini; patch runtime wajib lewat uji-flush.
### B. Teknik & proses
- **R10 Resolver 3 lapis**: by-name → pattern-scan → offset per-versi.
- **R11 Cek versi dulu** (versionName+versionCode via PackageManager); mismatch ⇒ UI tanda "WSM versi lama".
- **R12 Atomik**: simpan asli → tulis → flush → verify balik; gagal ⇒ restore otomatis.
- **R13 Retry**: resolve ×3 (jeda 300ms); bring-up tunggu libil2cpp ≤30s.
- **R14 Log terstruktur**: tag `WSM`/`WSMEngine`; INFO default, DEBUG opt-in.
- **R15 Build reproduce**: NDK r27c + `build.ps1` (verifikasi ELF: arch, align 16K, symbol kunci). Rilis = zip + changelog.
- **R16 Bukti**: tiap fitur butuh (a) skenario uji, (b) bukti nyata, (c) regresi — sebelum fitur berikut.

## 3. ALGORITMA + PSEUDOCODE

### 3.1 Bootstrap (L1 → L2)
```
onLoad(api, env): cache api/env/JavaVM

preAppSpecialize(args):
    name = utf8(args.nice_name)
    if not starts_with(name, "com.kakaogames.gdts"): return
    fd   = api.getModuleDir()
    data = read(fd, "engine/arm64-v8a.so")      # saat masih ber-privilege zygote
    m    = memfd_create("mem_cache", CLOEXEC)
    write(m, data); api.exemptFd(m)             # bibir fd tetap hidup pasca-spesialisasi

postAppSpecialize():
    env = attach(cached JavaVM)
    System.load("/proc/self/fd/" + m)           # ART: load + bridge → engine JNI_OnLoad
    if gagal: dlopen + NativeBridgeGetTrampoline → wsm_engine_main()   # Jalur B
    log hasil; dua-duanya gagal ⇒ tandai mode C
```

### 3.2 Bring-up Engine (L2)
```
JNI_OnLoad(vm): g_vm = vm; spawn(engine_main); register natives; return 1.6

engine_main():
    wait_lib("libil2cpp.so", 30s)               # R13
    h = dlopen("libil2cpp.so")
    api = resolve exports: domain_get, assembly_open, class_from_name,
          class_get_method_from_name, method_get_pointer,
          field_from_name, field_get_offset     # by-name
    targets = resolve_all(FEATURE_TABLE)        # 3.3
    verify_all(targets)                         # R3 → gagal ⇒ fitur nonaktif + log
    install_patches(OFF state)                  # R9 awal
    loop: watchdog + esp + safety               # 3.5 / 3.6 / 3.8
```

### 3.3 Resolver 3 lapis (per target)
```
resolve(target):
    for layer in [BY_NAME, PATTERN, OFFSET]:
        p = layer.try(target)
        if p and verify(p, target.expected): return p
    return NONE        # fitur mati (aman)
```
- BY_NAME: il2cpp API (Scripts.dll / Assembly-CSharp, namespace+class+method).
- PATTERN : scan `.text` libil2cpp untuk signature byte+mask (dari binary di disk).
- OFFSET  : base + RVA dari tabel versi (dump 3.54.0).
- verify  : baca N byte di target vs pola harapan; mismatch = tolak.

### 3.4 Patch Engine — 4 teknik
| # | Teknik | Untuk | Inti |
|---|--------|-------|------|
| T1 | FIELD_WRITE | nilai runtime | tulis field objek (offset), watcher jaga nilai |
| T2 | CODE_CONST | getter kecil | ganti fungsi ⇒ return konstan |
| T3 | HOOK_MUL | fungsi hitung | stub 16-byte → handler kita (×N) → call asli |
| T4 | LUA_INJECT | efek jalur Lua | panggil `luaL_loadstring` di XLua game |

Contoh T2 (arm64):
```
asli :  ldr s0,[x0,#0x48] ; ret
patch:  mov w0,#0x41200000 ; fmov s0,w0 ; ret     ; return 10.0f
```
Contoh T3 (stub + handler):
```
@target:  ldr x16,#8      ; 58 00 00 58
          br  x16         ; 00 02 1f d6
          .quad handler   ; (8 byte literal)
@handler: v = call_orig(ctx); return feature.on ? v*N : v
```
Prosedur atomik (R12):
```
apply(t):
    if not verify(t): return FAIL
    orig = copy(target); mprotect(page, RWX)
    write(target, t.patch); clear_cache(target)
    mprotect(page, RX)
    if read(target) != t.patch: write(target, orig); return FAIL
    save_original(orig)

restore(t): write(target, orig); clear_cache(target)
```

### 3.5 Feature registry + loop (contoh MovementSpeed penuh)
```
FEATURES = [
  { id:0, name:"Movement Speed", type:SLIDER, min:1, max:10, def:1,
    target:("Oak","CharacterStatsBehaviour","get_WalkSpeed"), tech:T3,
    tick: v => ctx.mult = v },
  ... dst
]
loop():                          # 100ms + event
    for f in FEATURES: f.tick()
    esp_tick(); safety_tick()
```
Makna `tick`: T3 = set angka yang dibaca handler; T1 = tulis field; T2 = ON ⇒ patch / OFF ⇒ restore.

### 3.6 ESP pipeline
```
esp_tick():                                  # 50ms
    cam = resolve("Unity","Camera","main")->WorldToScreenPoint
    for e in world.scan(kelas: Star/Coin/Chest/Mimic):   # daftar dari dump
        sp = cam(e.pos)  →  buf[i] = (x,y,dist,type)
    bila berubah: sinyal ke Java

Java ESPView.onDraw:
    d = native.getEspData()
    for each: drawCircle/drawRect + teks jarak
```
- Buffer tetap (tanpa alokasi per-frame). Draw via Java Canvas (tidak menyentuh GL game).

### 3.7 UI bridge — model per-index
```
getInternalData(int i) -> String      # nama+meta fitur i
setCollectionEnabled(int i, bool)     # toggle (apply/restore)
updateMetricParams(int i, int v)      # slider
getEspData() -> float[]               # data ESP
getStatus() -> String                 # heartbeat utk watchdog UI
panic() -> void                       # R2
```
UI generik: nambah fitur = nambah entri; UI otomatis menampilkan.

### 3.8 Safety engines
```
safety_tick():                        # 500ms
    scene = current_scene_name()
    if scene in {Arena, Coop, Raid} and preset != MP_SAFE: apply_preset(MP_SAFE); auto_flag=1
    elif scene == PvE and auto_flag: apply_preset(user); auto_flag=0

panic_watch():  (3× tap pojok ATAU hotkey) → for f: restore(f); ui.hide()

watchdog():
    engine menulis heartbeat; Java cek: tertinggal 3s ⇒ alert + best-effort disable
```

### 3.9 Config & versi
```
persist : Java simpan JSON {feature on/off, slider, preset} di files dir app (engine tidak menyentuh disk)
version : engine baca versionName GT → cocokkan tabel dukungan; mismatch ⇒ fitur "UNAVAILABLE" + saran update
```

## 4. FITUR v1 (urutan = urutan gate-test)
| # | Fitur | Teknik | Catatan |
|---|-------|--------|---------|
| 1 | Movement Speed ×1–10 | T3 | pembuka — risiko paling kecil |
| 2 | No Skill Cooldown | T2 | getter konstan |
| 3 | God Mode | T2/T3 | target intake damage |
| 4 | Damage ×1–100 | T3 (+T4 fallback) | cek jalur native vs Lua dulu |
| 5 | ESP: Star Piece, Purple Coin, Gold Cube | 3.6 | kelas item dari dump |
| 6 | ESP: Chest/Mimic + jarak | 3.6 | |
| 7 | Param tampil (ATK/DEF dll) | UI | |
| 8 | Preset + panic + auto-MP-off | 3.8 | wajib sebelum rilis |
| 9 | Auto-* (farm/win, opt-in) | — | V1.1 paling akhir |
(yang tidak lulus uji → tidak masuk rilis.)

## 5. RISIKO → MITIGASI
| Risiko | Mitigasi |
|--------|----------|
| Cache-translasi houdini (SMC) | R9: patch sebelum eksekusi pertama; runtime = uji-flush dulu |
| Update GT ⇒ pointer bergeser | R10 3-lapis + R11 versioning |
| Deteksi anti-cheat | R5 stealth + R6 auto-off + R7 cap nilai |
| Crash di hook | R4 minimal + R12 rollback otomatis |
| Bridge gagal di device lain | Jalur B & C |

## 6. PERTANYAAN REVIEW (boleh dijawab ringkas)
1. Set fitur v1 (§4): cukup? tambah/kurang (mis. Teleport, Drone View)?
2. Menu overlay kecil (draggable, collapse) — setuju?
3. MP-policy: auto-OFF semua di Arena, atau hanya fitur bahaya?
4. Panic: 3× tap pojok kanan-atas (tersembunyi) — oke?
5. Bahasa & nama menu: "WSM" + subjudul? (Indonesia/English?)
6. Cap slider: DMG 100×, SPD 10× — cukup?

---
*Setelah approval: eksekusi F0 finish (build → install → bukti "WSMEngine ALIVE") ≈ 1 sesi kerja, lalu naik fitur satu-satu sesuai §4.*
