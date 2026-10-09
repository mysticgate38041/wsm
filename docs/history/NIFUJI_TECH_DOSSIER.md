# NIFUJI TECH DOSSIER v2 — Model Teknis untuk WSM (WS Menu)
*dikunci oleh worm shadow — untuk He*
*Status: F0 (Riset) — Tahap analisis Nifuji SELESAI secara mekanisme.*

════════════════════════════════════════════════
## 1. RANTAI EKSEKUSI NIFUJI (REVISED, final understanding)

1. **Zygisk module entry** (`zygisk_module_entry`, api v4) → daftar module; `onLoad` alloc context 0x7b0 + init.
2. **preAppSpecialize** → cek app: nama proses `com.kakaogames.gdts` (+ `app_process64` di emulator);
   baca `ro.build.version.sdk` (kompat API); cek game via JNI: `versionName`==`3.54.0` (mismatch → mod pasif).
3. Flag lolos → **spawn 2 worker thread** (0xf5e40 & 0xf79b0). Keduanya:
   loop retry (0.5s/2s sleep) memanggil helper resolve-lib:
   - **scan `/proc/self/maps`** (format `%lx-%lx r`) + `dl_iterate_phdr` + scan `.symtab`/`.gnu_debugdata`
     dari `/system/bin/linker64`, `/system/bin/app_process64`, `/system/lib64`, `/libc.so`, `/libart.so`,
     `/vendor|/odm/lib64` (+vndk-sp, egl, hw) → **temukan `libil2cpp.so`** (+ basis lib lain).
   - path vendor/odm = dukungan LDPlayer/MuMu (layout lib emulator beda).
4. **DEX tersembunyi**: blob DEX 22.004 B di `.rodata` di-XOR 0x77 (vektor 16B); didekripsi runtime (malloc+eor)
   → **InMemoryDexClassLoader** (`([Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V`), class kamuflase Firebase
   `com.google.firebase.util.CmsProvider` → 14 class / 247 method / 373 string.
5. **Overlay menu Java murni** (LinearLayout/SeekBar/TextView/ESPView custom Canvas):
   dipasang via reflection `android.app.ActivityThread` → currentActivityThread → getApplication →
   ActivityLifecycleCallbacks (attach ke Activity game). Ada **Pro Panel** (ko-fi).
6. **JNI bridge fitur** (native pemilik logika, Java = UI):
   `getEspData()→float[]`, `getFontData(int)→byte[]`, `getInternalData(int)→String`,
   `setCollectionEnabled(int,boolean)`, `updateMetricParams(int,int)` — fitur DI-INDEX, punya toggle+slider.
7. **Resolusi il2cpp by-name** (runtime, bukan offset statis): `il2cpp_domain_get`,
   `il2cpp_domain_assembly_open("Scripts.dll")`, `il2cpp_class_from_name`,
   `il2cpp_class_get_method_from_name`, `il2cpp_method_get_pointer`, `il2cpp_resolve_icall`,
   `il2cpp_class_get_field_from_name`, `il2cpp_field_get_offset`.
   Target terbaca: `CharacterStatsBehaviour.get_WalkSpeed`, `get_DashSpeed`, `<EntityGroup>k__BackingField`.
8. **Injeksi Lua ke XLua game**: `luaL_loadbufferx`, `luaL_loadstring`, `lua_getfield` — logika rumit
   (damage/god) dijalankan lewat interpreter Lua milik game itu sendiri.
9. **Blob XZ ~730 KB** (entropy 7.96) di `.rodata` → didekripsi + dekompres XZ (**liblzma** `XzUnpacker_*`)
   = paket font (f1.ttf/f2.ttf) + aset UI + payload Pro.
10. **memfd_create("mem_cache")** — buffer/kanal memori anonim (dipakai internal).

## 2. ARSITEKTUR ABI — KENAPA JALAN DI EMULATOR (kunci WSM)
- Modul = **DUA build**: `lib/arm64-v8a/libnifuji.so` + `lib/x86_64/libnifuji.so` (1 codebase, 2 arch).
- Di LDPlayer (proses GT = x86_64 + game libs ARM64 via **houdini/native-bridge**):
  ZN memuat `zygisk/x86_64.so` (loader), string `arm64-v8a.so` + `zygisk/` → loader hrs **memuat engine ARM64**
  (dlopen file arm64 dari proses x86 → ditangani native-bridge) lalu memanggilnya via
  **NativeBridgeGetTrampoline** (API resmi libnativebridge — mekanisme yang sama dipakai ART
  untuk memanggil native arm64 dari host x86; GT sendiri jalan begitu tiap hari).
- **Implikasi WSM**: patch kode/game WAJIB berlangsung di dunia ARM64 (guest);
  sisi x86_64 hanya: filter proses, JavaVM/env, load engine, trampoline call, UI/JNI koordinasi.
- **Hipotesis WSM yang harus diuji empiris (POC)**: dlopen engine arm64 + NativeBridgeGetTrampoline + call → log balik.

## 3. PROTEKSI / OBFUSKASI (dan cara kita mengalahkannya)
- sha256 per-file + verify.sh → abort kalau repack (anti-edit modul). → Kita: build sendiri, ga masalah.
- DEX XOR-0x77 → terbongkar. 
- String layer `.data`: lazy XOR in-place, kunci **naik +1 per byte** (blok 4–13 byte), flag "inited" per situs;
  ada versi vektor `movaps/xorps` (16B) utk blok panjang. → Terbongkar (decoder di `WSMenu/re/`).
- Blob XZ 730KB: di luar scope (aset).

## 4. INVENTARIS FITUR NIFUJI (dari string + bridge)
- Combat: God Mode, No Skill Cooldown, (ATK/DEF tokens utk UI params), Damage (via Lua).
- Movement: Movement Speed (slider), (DashSpeed target).
- Visual: WallHack, ESP (Purple/Star), ESPView canvas.
- UX: Icon hidden, Pro Panel (tier), toggle+slider per fitur, branding "MODDED BY NIFUJI", link platinmods.com.
- Versi game yang didukung: PERSIS 3.54.0 (versionName check) — offset/nama di-pin ke versi itu.

## 5. DECISIONS WSM (menang telak vs Nifuji)
1. **Arsitektur = mirror** (x86 loader + arm64 engine) TAPI engine kita buka API ganda:
   - mode A: by-name il2cpp (setara Nifuji, tahan update minor),
   - mode B: pattern-scan + tabel RVA per-versi (fallback, offline dari dump kita),
   - mode C: data-values watcher (untuk fitur nilai) — 3 lapis fail-safe.
2. **UI**: Java overlay (pelajaran Nifuji: paling aman di emulator; touch pasti jalan) dengan design
   Material ala WSM (tema gelap neon), PLUS nanti opsional ImGui native kalau mau gaya beda.
   DEX kita embed (XOR simple) → InMemoryDexClassLoader (tidak ada file nyangkut di disk).
3. **Fitur tambahan yang Nifuji TIDAK punya** (target "lebih canggih"):
   - ESP lengkap (Star Piece, Purple Coin, Gold Cube, Chest, Mimic, jarak) + HP bar musuh.
   - Auto-Win Colosseum (opt-in) / farm assist / skip cutscene (risk-managed).
   - Teleport/bookmark (x,y) + Drone View/camera zoom.
   - Preset PvE-MAX / MP-SAFE / STEALTH / OFF + auto-disable di arena (anti-report), panic-key.
   - Config autosave + export/import profile.
   - Multi-layer anti-update (nama+pattern+versi) + fail-safe "gagal patch → game normal".
4. **QA contract** (lihat GT_ULTRA_CHEAT_PLAN.md §11): tiap fitur = uji + bukti; crash-test 60mnt; reversible.
5. **Pel鄙视 Lua**: injeksi XLua = senjata ampuh Nifuji; WSM akan punya generator script Lua sendiri
   (untuk efek yg tak bisa via memory patch). Dipelajari dari dump GT (namespace Oak + XLua bindings).

## 6. NEXT ACTIONS (F0 → F0.5)
- [F0.a] Scaffold proyek WSM: `WSMenu/src/` (module template + loader x86_64 + engine arm64 "hello world").
- [F0.b] Build NDK r27c (x86_64 + arm64) → zip module → install via Magisk → test injeksi (log line).
- [F0.c] **POC BRIDGE** (Gate-0): x86 loader dlopen engine arm64 + NativeBridgeGetTrampoline + call →
  engine tulis "WSM-ARM64 ALIVE" ke logcat + file. = bukti mekanisme inti bekerja di LDPlayer.
- [F0.d] POC resolve libil2cpp base (maps scan dari x86) + engine baca 16 byte dari libil2cpp (cross-world read OK).
- [F0.e] POC patch trivial (ubah 1 fungsi sederhana / nilai) + observasi in-game (butuh He).
- Lalu F1 engine inti (Move Speed / No-CD / Damage mult).

## 7. ASET & LOKASI
- Decoder strings + disasm notes: `GT_cheat_analysis/WSMenu/re/` (nifuji_notes*.txt, strings_clean2, libnifuji_x86_64.so).
- Analisis lama lengkap: `GT_cheat_analysis/zygisk_v354/` (ANALYSIS.md, dex, disasm_arm64.txt, strings).
- Plan induk: `GT_cheat_analysis/WSMenu/GT_ULTRA_CHEAT_PLAN.md`.
- Stack device (He-verified): LDPlayer14 + Kitsune v31 + ZN v1.5.0 + GT 3.54.0.
- NDK: `D:\Android\ndk\android-ndk-r27c`.
