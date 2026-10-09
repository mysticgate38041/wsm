# WS Menu (WSM) — Guardian Tales "Ultra" Cheat — RENCANA INDUK v1.0
Proyek: modul Zygisk sendiri untuk GT 3.54.0 di LDPlayer 14
Author: worm shadow (untuk He) — 2026-10-06

════════════════════════════════════════════════
## 0. TUJUAN & KRITERIA SUKSES
- Lebih canggih dari Nifuji v3.54 & VounderS: fitur lebih lengkap, stabil, bisa di-update mandiri.
- "Berfungsi total": SETIAP fitur wajib lolos uji + bukti (screenshot/log/nilai) sebelum dianggap selesai.
- "Aman": preset & guardrail; nilai wajar; auto-disable di mode berisiko; semua reversible.
- 100% kontrol sendiri: tanpa integrity pihak ketiga, tanpa target "auto update" yang bisa mati.

════════════════════════════════════════════════
## 1. ASET YANG SUDAH KITA MILIKI (fondasi nyata)
1. Device stack JALAN: LDPlayer 14 + Magisk Kitsune v31 (+fix manual) + ZygiskNext 1.5.0.
   - rootMode LDPlayer = OFF (no traces); su = u:r:magisk:s0; semua persisten.
2. Bukti konsep: modul Zygisk x86_64 BISA mengubah perilaku GT (via ARM-translation/houdini)
   → dibuktikan oleh Nifuji v3.54 yang WORKING (He-verified) di setup ini.
3. Dump il2cpp GT 3.54.0 lengkap: Downloads\Mod\gt_dump → nama class/method/field + offset.
   Contoh target: CharacterStatsBehaviour (Walk/DashSpeed, CritScale...), CharacterDamagedBehaviour.Damage,
   MonsterBattleAIState, StageCamera, ColosseumClient, CharacterOptionStats.
4. 2 modul referensi untuk dibongkar:
   - Nifuji (WORKING; minimal-import; string obfuscated) → sumber teknik "yang benar".
   - VounderS ModMenu (menu render tapi efek GAGAL; pakai Dobby x86 hooking) → contoh "yang salah".
5. Toolchain terbukti: NDK r27c + build pipeline Zygisk (kita sudah build+verify modul probe ELF).
6. Script Lua GG (OnlyTris v25, TDL v2.94) → referensi logika nilai & anti-tamper GT.
7. Skills terpasang: zygisk-module-re, zygisk-module-build, ghidra-headless-mass-decompile, gg-luaj-script-emulation, ldplayer-magisk-zygisk.

════════════════════════════════════════════════
## 2. TEMUAN MEKANISME (kenapa kami yakin bisa, & arah desain)
- ModMenu x86_64: imports DobbyHook/DobbyCodePatch + il2cpp by-name → hook x86 ke kode ARM
  = TIDAK EFEKTIF di houdini (menu jalan, efek mati). → JANGAN pakai jalur ini untuk hook.
- Nifuji x86_64: imports = syscall, mmap/munmap, dl_iterate_phdr, dlopen, getauxval,
  pthread_*, file I/O, rand, syslog, abort. TANPA Dobby, TANPA simbol il2cpp polos (string di-obfuscate).
  → pola modifikasi "in-guest": kemungkinan (a) resolve libil2cpp sendiri lalu tulis DATA field, dan/atau
  (b) menanam patch kode ARM64 ke halaman memori (eksekusi tetap lewat translator houdini).
- FASE 0 akan menetapkan mekanisme persis (live diff memori vs file; dump region exec anonim; disasm ARM64).

════════════════════════════════════════════════
## 3. ARSITEKTUR USULAN — "HYBRID ENGINE"
```
[ Zygisk module  libwsm.so  (x86_64) ]
 ├─ Core          : parser /proc/self/maps, base resolver (libil2cpp + libunity), ELF dynsym lookup
 ├─ Il2Cpp Bridge : resolusi class/method/field by-name (mapping dari dump; dlsym bila memungkinkan)
 ├─ Patch Engine  : tulis patch ARM64 (branch stub → cave), manajemen cave, cache-flush (uji houdini SMC)
 ├─ Data Engine   : watchdog thread (tulis field nilai; dipakai utk fitur value/ESP)
 ├─ Overlay Menu  : ImGui; hook eglSwapBuffers (LAYER INI SUDAH TERBUKTI JALAN di GT)
 ├─ Input         : hotkeys (F1-F12, toggle menu), via AInputEvent (proven di ModMenu)
 ├─ Config        : /data/adb/wsm/config.json (autosave, per-preset)
 ├─ Safety        : mode detector (PvE/MP/arena), caps, panic-key (OFF semua), auto-disable MP
 └─ Logging       : /data/adb/wsm/wsm.log (rolling)
```
Prinsip: PATCH hanya bila perlu; utamakan DATA bila cukup; SEMUA aksi reversible + tercatat.

════════════════════════════════════════════════
## 4. DAFTAR FITUR (lengkap + prioritas + risiko)
TIER A — COMBAT (prio 1; risiko sedang utk MP)
 A1 Damage Multiplier x1–x1000 (slider + hotkey +/-)
 A2 Defense Multiplier / Damage Taken %
 A3 God Mode (immunity toggle)
 A4 One-Hit Kill
 A5 No Skill Cooldown
 A6 Infinite Shield / Guardian (cek dump; opt)
TIER B — MOVEMENT
 B1 Move Speed x1–x5 (proven target: get_WalkSpeed/get_DashSpeed)
 B2 Dash No CD / stamina
 B3 Game Speed x0.5–x3 (opt; hati2 timing jaringan)
TIER C — VISUAL / ESP (beat Nifuji di kelengkapan)
 C1 Enemy Wallhack
 C2 ESP: Star Piece (jarak+label)
 C3 ESP: Purple Coin
 C4 ESP: Gold Cube
 C5 ESP Chest / Mimic / Collectible (filter)
 C6 Enemy HP Bar + Boss tracker
 C7 Camera Zoom-out / Drone View
TIER D — AUTOMATION (opt-in, guardrail ketat)
 D1 Auto-Win Colosseum (switch; otomatis OFF di ranked sesungguhnya? sesuai kebijakan He)
 D2 Auto-run / farm assist (basic)
 D3 Skip cutscene / dialog cepat
TIER E — SAFETY/UX
 E1 Preset: PvE-Max / MP-Safe / Stealth / OFF
 E2 Auto-disable saat deteksi arena/coop/ranked
 E3 Hotkeys lengkap + Panic key (semua OFF seketika)
 E4 Config autosave + backup + log viewer (menu)
 E5 Anti-update: pattern-scan (signature) selain offset + version table
 E6 (v2) dukungan arm64 phone (stretch)

════════════════════════════════════════════════
## 5. ROADMAP & GATE (tiap fase = hasil nyata + bukti)
FASE 0 — RISET & POC  [estimasi 1–2 sesi]
 0.1 Static RE libnifuji.so (Ghidra + capstone; deobfuscate string; peta fungsi utama)
 0.2 Live diff: libil2cpp di memori vs file → deteksi patch kode Nifuji & lokasinya
 0.3 Dump & disasm region exec ANONIM di proses GT (cari payload ARM64 tertanam)
 0.4 Eksperimen SMC houdini: patch kecil sendiri (getter uji) → efek + kebutuhan cache-flush
 0.5 Verifikasi resolusi alamat base+RVA (dump ↔ runtime)
 0.6 Kerangka modul WSM naik ke GT (menu ImGui muncul) — pakai pipeline NDK kita
 GATE 0: ≥1 fitur trivial JALAN dari modul KITA (mis. Move Speed atau Drone View)
FASE 1 — ENGINE INTI + MENU  [1–2 sesi]
 - Patch engine + framework fitur + config + safety skeleton
 GATE 1: Move Speed + No-CD + Damage x jalan (3 fitur; dengan bukti)
FASE 2 — COMBAT LENGKAP (Tier A) + test matrix  [2–3 sesi]
 GATE 2: God Mode & One-Hit terverifikasi; crash-test main 1 jam
FASE 3 — VISUAL/ESP (Tier C)  [2–4 sesi]
 GATE 3: Wallhack + ESP Star Piece terbukti tampil
FASE 4 — AUTOMATION (Tier D)  [1–2 sesi]
FASE 5 — HARDENING + RILIS WSM v1.0  [1–2 sesi]
 - Pattern-scan, perf, docs, zip rilis, repo, skill baru "wsm-gt-mod-dev"
Total jujur: ±8–15 sesi kerja bertahap (masing2 dengan output, bukan janji).

════════════════════════════════════════════════
## 6. RISIKO & MITIGASI
R1 [KRITIS] Houdini SMC: patch ARM64 mungkin tidak langsung invalidated translator.
   → Uji F0.4. Mitigasi: (a) flush via syscall/cacheflush/; (b) patch saat init (sebelum translate);
   (c) fallback penuh ke Data Engine.
R2 GT update → offset geser → pattern-scan + version table (fitur E5).
R3 Crash/instabilitas → semua patch reversible (simpan bytes asli), panic key, auto-restore,
   mod fail-safe: kalau mod GAGAL init → game jalan normal (tanpa efek).
R4 Deteksi (ACTk + MultiPlayHackReporter; behavioral) → preset MP-Safe, caps nilai,
   auto-disable di mode berisiko, hindari laporan absurd.
R5 Emulator "crashes sometimes" (quirk LDPlayer/houdini) → relaunch; mod tahan restart.

════════════════════════════════════════════════
## 7. PROTOKOL PENGUJIAN (per fitur)
- Skenario tulis: PvE Story, Dungeon, Rift, Arena(mode aman), Coop(guard).
- Metrik: angka damage, HP, timer cooldown, posisi speed, marker ESP.
- Bukti wajib: screenshot + log + (bila relevan) rekaman singkat.
- Regresi: sesudah tiap fitur baru, fitur lama dicek ulang (checklist).

════════════════════════════════════════════════
## 8. DELIVERABLES
- Source: Downloads\GT_cheat_analysis\WSMenu\{src, build, docs, releases}
- Zip modul per versi + CHANGELOG + panduan install
- Screenshot bukti per fitur (folder evidence\)
- Skill baru: wsm-gt-mod-dev (build/test/update workflow)

════════════════════════════════════════════════
## 9. DI LUAR SCOPE (jujur, biar ekspektasi akurat)
- Ekonomi server-side (gems/gold nyata), unlock skin/hero (server), manipulasi ranked →
  bukan wilayah klien; TIDAK akan diklaim bisa.
- "Berfungsi total" = di ranah yang memang bisa: combat/movement/visual/automation lokal.

════════════════════════════════════════════════
## 10. LANGKAH PERTAMA (menunggu GO dari He)
- Mulai FASE 0 (riset & POC). Target cepat: dalam 1–2 sesi ada bukti
  "modul WSM milik kita sendiri berhasil mengubah 1 perilaku game".

════════════════════════════════════════════════
## 11. KONTRAK KUALITAS — definisi "BERJALAN 100%" (dikunci)
"100%" berarti SEMUA poin ini terpenuhi & dibuktikan, bukan klaim:
1. SETIAP fitur punya (a) skenario uji tertulis, (b) bukti nyata (angka/screenshot/log), (c) regresi dicek ulang saat fitur lain ditambah.
2. Tidak ada offset hardcoded tunggal: setiap target pakai pattern-scan + fallback offset per-versi (update GT = tambah entri versi, bukan rusak).
3. Fail-safe: kalau mod gagal init / gagal patch → game JALAN NORMAL (tanpa efek), tidak nge-crash.
4. Semua patch REVERSIBLE: bytes asli disimpan; panic-key (satu tombol) mematikan semua efek seketika; auto-restore saat mati.
5. Crash-test: main ≥60 menit dengan semua fitur inti nyala, tanpa crash (atau crash teranalisa & tertutup).
6. Panic/stealth: preset MP-Safe & auto-disable di mode berisiko, diuji nyata (masuk arena/coop → fitur OFF).
7. Zero dependency eksternal: tanpa server, tanpa update pihak ketiga; build reproducible dari source kita (NDK pipeline terdokumentasi).
8. Performa: tidak ada frame-drop terukur >5% (profil sederhana sebelum/sesudah).
9. Dokumentasi: CHANGELOG + cara install + cara update versi GT baru, ditulis sebelum rilis.
10. Satu sumber kebenaran: source di WSMenu\src, build script, zip rilis per versi; skill wsm-gt-mod-dev diperbarui tiap fase.
