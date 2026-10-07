# POC ARM64 PAYLOAD — Investigasi & Status (UPDATE 5 — 🏆 **POC-3 SELESAI! HOOKING PRIMITIVE TERBUKTI!**)

> ## 🏆🏆🏆 POC-3 COMPLETE (2026-10-07 00:56) — PATCH KODE ARM64 LIVE = DIEKSEKUSI!
> ```
> D:old=0x72a00c8052981bc0 ret1=0x77 nm=3 nv=3      ← patch 3-alias + call = NILAI PATCH!
> E:victim firstcall=0x77                            ← fungsi disk-patched = dieksekusi
> agent: v4 magic=0x77 victim=0x77                   ← dua fungsi, dua jalur
> ```
> **RANTAI HOOKING PRIMITIF (SEMUA TERVERIFIKASI):**
> 1. `mprotect(page, RWX)` → tulis instruksi arm64 baru (movz/ret) → `__builtin___clear_cache` → readback ✓.
> 2. **Enumerasi ALIAS dari /proc/self/maps** — file yang sama ke-map 2-3× (offset sama!); **patch SEMUA alias** (n=3 ✓) — per-mapping offset dihitung dari mapping yang mengandung target (bukan base!).
> 3. **Houdini translasi LAZY saat call pertama → BACA IMAGE SAAT ITU (alias-patched = dibaca!) → EKSEKUSI KODE PATCH ✓.**
> 4. **SYARAT: patch sebelum eksekusi pertama** (cache translasi = per-proses; repatch setelah cached = belum diuji — next test).
> 5. **⚠️ Jebakan termahal: `((fn)ptr)()` di -O2 = DEVIRTUALISASI+FOLD jadi konstanta!! — WAJIB `volatile` fn-ptr utk call eksperimen!! (2 jam teka-teki salah arah krn ini!)** — `int (*volatile f)() = ...; f();`
> 6. `NativeBridgeGetTrampoline` = CRASH houdini (jangan); agent-thread+bus = jalur komunikasi ✓.
> 7. Juga terbukti: agent baca/tulis memori x86 (READ64/WRITE64 ✓), dua dunia baca ELF base IDENTIK ✓.
> → **UNTUK methodPointer HOOK libil2cpp: teknik sama (patch alias semua + sebelum eksekusi) ATAU pointer-swap di MethodInfo (data, no translasi!).**

## 🧪 SEJARAH PANJANG (arsip — 10+ hipotesis + 1 jebakan compiler menuju resep di atas)

> ## 🏆🏆🏆 POC-2 COMPLETE (2026-10-07 00:20) — JEMBATAN X86↔ARM64 DUA ARAH TERBUKTI!
> ```
> ACK: POC2B busfile=1 load h=0x…401 e=0 thread=UP hb=2 tid=9224 mag=0xa864cafe ping=2 mul=42 hb2=5
> LOG: "WSM-H64: agent thread UP pid=9224" / "agent: ping served" / "agent: mul 6 x 7 = 42"
>      engine: "POC2B thread=UP hb=2 tmag=0x74ead001" → FINAL_PID=9224 (GT HIDUP, 0 crash!)
> ```
> **ARSITEKTUR FINAL (pengganti getTrampoline — yang CRASH di houdini!):**
> 1. **x86 (engine)**: tulis pointer bus ke `h64_bus.txt` → load payload arm64 via `NativeBridgeLoadLibraryExt(path,2,null,null)` (lib/arm64 dir!) → tunggu heartbeat → kirim cmd via bus → baca hasil.
> 2. **arm64 (payload ctor)**: baca pointer bus dari file → **`pthread_create` AGENT THREAD** (houdini translate thread arm64!) → loop: heartbeat `bus[10]++`, layani cmd: 1=ping, 2=mul a*b, 9=exit.
> 3. **Bus** = `uint64_t[64]` static di engine (satu address space — pointer valid di dua dunia!): [0]=cmd [1]=status [2]=pid [3]=magic 0xA864CAFE [4,5]=input [6]=hasil [10]=heartbeat [11]=thread-magic 0x74EAD001.
> 4. **TERLARANG**: `NativeBridgeGetTrampoline` = CRASH SIGSEGV DI DALAM libhoudini (tombstone: houdini+0x305e54 deref marker internal 0xdead1052) — JANGAN pakai!
> 5. Command: `nbpoc2` (auto fresh-load + autotest). Evidence: `g28-poc2-agent-*.log`.
> **=> FONDASI HOOK ENGINE ARM64 SIAP: agent thread = eksekutor patch/gate berikutnya via bus.**

## 🧪 SEJARAH PANJANG (arsip — 10+ hipotesis tereliminasi menuju resep di atas)

> ## 🏆🏆🏆 BREAKTHROUGH (2026-10-07 00:02) — ARM64 CODE EKSEKUSI TERBUKTI DI GT!
> ```
> NBDL ext=1 fext=1 init=1 | .../lib/arm64/libh64y.so fd=201 dl=no e22 ad=no e22 bA=no e0 bB=OK e0
> WITNESS: H64-ALIVE v2 arch=aarch64 base=0x400030380000 pid=5575
> LOG: "WSM-H64: ctor v2: H64 mapped + executing arch=aarch64 base=0x400030380000 pid=5575"
> ```
> **RESEP FINAL (7 langkah dari engine x86_64):**
> 1. `dlopen("libnativebridge.so")` → `dlsym("NativeBridgeLoadLibraryExt")` — **`init=1` = bridge ter-registrasi di proses** (libnativeloader sudah init via prop).
> 2. **Panggil `fext(path, 2 /*RTLD_NOW*/, nullptr, nullptr)` — PATH-based, TANPA fd/extinfo!!** (varian fd-based `bA` GAGAL; varian bionic `dl`/`ad` = EINVAL e22 = bionic polos gak kenal bridge!).
> 3. **Path WAJIB di app lib dir: `/data/app/~~<hash>/<pkg>==/lib/arm64/<file>.so`** (namespace classloader = satu-satunya yang jalan; file di /data/data atau /storage = EINVAL walau fd OK!).
> 4. **File .so WAJIB TANPA dep `libc++_shared.so`!!!** (hadir = houdini tolak DIAM-DIAM tanpa error!) — build: `-static-libstdc++ -fno-exceptions -fno-rtti` (atau C murni). Deps aman: liblog/libdl/libc/libm/libandroid.
> 5. **Constructor arm64 = LANGSUNG JALAN saat load** (witness file + logcat OK!) — bukti eksekusi ARM64 via houdini translation.
> 6. **NO REBOOT, NO ZN, NO zygisk companion** — engine live bisa load kapan saja (hot!) — `nbdl` command, ~instant.
> 7. `base=0x400030380000` = region arm64 di maps (cari `/memfd:` atau anon RWX di area 0x40003...).
>
> **Implikasi: POC-2 (arm64 trampoline hooking di methodPointer libil2cpp) = FULLY UNLOCKED.** Komunikasi x86↔arm64 = file channel/shared mem (engine x86 yang orkestrasi).

## 🧪 SEJARAH PANJANG (arsip — 10+ hipotesis tereliminasi menuju resep di atas)

> Tujuan: menjalankan kode **AArch64** di dalam proses GT (LDPlayer 14) sebagai fondasi
> trampoline hooking (patch `methodPointer` di MethodInfo). Semua temuan = terverifikasi empiris.

## 🔬 FAKTA STACK (terverifikasi)

| Fakta | Bukti |
|---|---|
| Proses GT = **x86_64 native** (`/proc/pid/exe -> app_process64`, e_machine=0x3e) | baca ELF header exe |
| Native bridge aktif: **`ro.dalvik.vm.native.bridge=libhoudini.so`**, `ro.enable.native.bridge.exec=1` | props |
| Lib game (`libil2cpp` dll) = **AArch64 asli** (e_machine=0xb700) di `lib/arm64/` | baca ELF on-device |
| `libnativebridge.so` = dari APEX ART; houdini 14.0.0_z.leidian, lisensi evaluasi | maps + logcat |
| **ZygiskNext v1.5 memuat companion ARM64 milik modul lain** (`guardiantales_nifuji/zygisk/arm64-v8a.so`) SETIAP SPAWN oleh zygote | avc audit + maps |

## 🧪 PERCOBAAN LOAD (semua gagal, terdokumentasi)

1. **Java `System.load(libdir/libh64.so)`** → `UnsatisfiedLinkError: is for EM_AARCH64 (183) instead of EM_X86_64 (62)` — linker menolak (namespace classloader).
2. **`NativeBridgeLoadLibrary` (v1)** dari engine → **houdini: "Shall not invoke deprecated interface in v3 implementation!"** → null.
3. **`NativeBridgeLoadLibraryExt` (v3)** (ns/extinfo=null) → **null diam-diam** (tidak ada log refusal).
4. **Reflection `Runtime.loadLibrary0(appClassLoader,"h64")`** (jalur ART!) → InvocationTargetException → linker refusal yang SAMA.

## 🧩 MATRIKS ELIMINASI — kenapa companion arm64 KITA tidak dimuat ZN
Semua diuji dengan **reboot penuh + GT relaunch**, target: `wsm_gt/zygisk/arm64-v8a.so` di `/proc/<pid>/maps`:

| # | Hipotesis | Hasil |
|---|---|---|
| 1 | Permissions (777) | ❌ |
| 2 | Scripts (post-fs-data/service/sepolicy) | ❌ |
| 3 | Simbol `zygisk_module_entry` (ada di semua) | ❌ |
| 4 | `skip_mount` | ❌ |
| 5 | Deps C++ (`libc++_shared` → rebuild bersih `liblog+libdl+libc`) | ❌ |
| 6 | **Isi file → FILE NIFUJI SENDIRI ditaruh di modul kita** | ❌ **tetap tak dimuat** → gerbang ≠ konten! |
| 7 | SELinux label (`adb_data_file` = sama nifuji, chcon) | ❌ |
| 8 | Cache/waktu daemon (banyak reboot segar) | ❌ |
| 9 | `connectCompanion()` sebagai pemicu | ❌ (root-IPC; pre-only; permission denied) |

**KESIMPULAN KUAT: gerbang = LEVEL IDENTITAS MODUL di ZN** (state internal per-modul, kemungkinan diset saat instalasi/manajemen ZN). **File/label/deps TERBUKTI bukan.**

## 🎯 KUNCI PENUTUP #2: API VERSION = PENENTU COMPANION ARM64!!
Dari RE biner nifuji (x86_64, 41k baris disasm):
- Entry mereka: `movq $0x2, (%rax)` → **module_abi.api_version = 2** + struct 0x30 byte = **{version, impl, preApp, postApp, preServer, postServer}** — LAYOUT v2-ERA era asli (TANPA slot onLoad!).
- Loader KITA: `movq $0x4, ...` → **api_version = 4** (layout v4, ada onLoad).
- **Tes:** binary-patch 04→02 di loader kita → **modul MATI TOTAL** (nol log) — ZN menolak struct v4 yang ngaku v2 (layout mismatch).
- ➜ **HIPOTESIS FINAL: ZN memuat companion arm64 (bridge-world) HANYA untuk modul ber-API LAMA (v2/v3) — mode kompatibilitas legacy-nya memuat SEMUA ABI modul.** (Sesuai juga dengan commit Magisk "Refactor zygisk to use native bridge to inject": `archs = [armeabi-v7a, x86, arm64-v8a, x86_64]` + `archs32map` — mekanisme per-ABI kompat!)
- **NEXT: port loader.cpp ke header zygisk ASLI versi v2/v3** (wrapper flow berbeda: tanpa onLoad — setup dipindah ke pre/postSpecialize) → rebuild → reboot → cek maps.

## 📖 RE SOURCE MAGISK (master) — MEKANISME RESMI TERBACA TUNTAS
1. **`module.rs` (z64/z32)**: `#[cfg(target_arch = "x86_64")]` = COMPILE-TIME: z64 = `zygisk/x86_64.so`, z32 = `zygisk/x86.so` — **Magisk asli TIDAK PERNAH buka arm64-v8a.so lewat jalur daemon!!** (Di ARM HP: z64=arm64-v8a.so.) → arm64 di device kita = BUKAN dari daemon ZN, tapi **dari mekanisme lain (nifuji benar: loader mereka sendiri / jalur bridge).**
2. **`daemon.rs get_module_fds`**: fd per modul (z64/z32) dikirim ke zygote → `run_modules_pre` → `android_dlopen_ext("/jit-cache", {USE_LIBRARY_FD: fd})` → **bionic MENYERAHKAN ELF asing ke native bridge (houdini)** = cara arm64 dimuat di proses x86!! (Ini desain "Refactor zygisk to use native bridge to inject".)
3. **`daemon.rs set_prop`**: trik prop: `ro.dalvik.vm.native.bridge = "libzygisk.so" + <bridge asli>` (gabungan string!) — zygote memuat libzygisk.so DULU, yang memuat bridge asli.
4. **`module.cpp`**: registerModule validasi `api_version > ZYGISK_API_VERSION` (magisk master = 5!) → reject; fill API bertingkat: ≥1 v1 slots, ≥2 (getModuleDir/getFlags), ≥4 (lsplt hooks + exemptFd); `valid()` cek 5 fn wajib; **v1/v2 modules dapat `AppSpecializeArgs_v1` LAYOUT LAMA** (tanpa rlimits, const ptrs) — v3+ dapat layout modern.
5. ➜ **REVISI HIPOTESIS: arm64 companion nifuji = dimuat oleh bionic→houdini dlopen dari FD yang diserahkan... oleh SIAPA?** Sesi depan: **command `nbdl`** di engine (android_dlopen_ext + USE_LIBRARY_FD) = tes langsung: (a) file arm64 nifuji, (b) libh64 kita — dari ENGINE (proses GT live). Kalau ext-load tembus = **POC arm64 GAK PERLU ZN SAMA SEKALI — engine kita load payload arm64 sendiri via bionic/houdini!!**

## 💡 HIPOTESIS UTAMA: MEKANISME NIFUJI SENDIRI
- Loader nifuji = **1.2MB framework proprietary**, jalan di fase zygote (avc: `scontext=zygote comm=main`, pid app) = MOMEN loader kita berjalan.
- **Sangat mungkin: NIFUJI x86 loader MEMUAT SENDIRI arm64-nya** (mekanisme privat x86→houdini; strings "no x86_64 float registers"/"unsupported x86_64 register" = jejak loader/emu bawaan mereka). Loader mereka tanpa log.

## 📌 INTEL DARI RISET (penting untuk fase hooking!)

- **Houdini alias-aware patching** (paper ae-pcd-stamp-tracer): di bawah houdini, SATU offset file ARM64 bisa muncul di **BEBERAPA alamat mapping sekaligus** (alias verifier vs alias eksekusi). Patch satu alias = "kelihatan sukses" tapi gameplay tetap pakai yang lama. **Hook layer wajib: temukan SEMUA mapping se-file-offset yang sama → patch set dalam satu transaksi → verifikasi per-alias.**
- Magisk zygisk protocol: daemon mengirim `archs = [armeabi-v7a,x86,arm64-v8a,x86_64]` per modul + `archs32map` (x86_64↔x86, arm64↔arm32).
- Referensi arsitektur: `arm64-houdini-lsposed-framework` (primitif native sendiri, bukan lib inline-hook umum).

## 🎯 NEXT STEPS (urut prioritas)
1. **RE loader nifuji x86_64.so (1.17MB)**: cari kode yang open+map `zygisk/arm64-v8a.so` (xref path string / syscall openat+mmap; cek import `dlopen`/houdini utils). Itu jalur resminya.
2. Kalau gagal: audit manajemen modul ZN (webroot UI / zygiskd64) untuk gerbang identitas; coba versi ZN lain.
3. Paralel: gali export `libhoudini.so` (mungkin ada API houdini untuk memuat image arm64 dari proses x86).

## 🛠️ ARTEFAK
- `payload/h64.cpp` + `libh64.so` (AArch64, ctor → log + tulis bukti) — **menunggu jalur load yang sah.**
- `engine` command: `payloadrun` (Ext/v1 bridge attempt), `feat_payloadrun` di engine.cpp.
- Deployed: `/data/adb/modules/wsm_gt/payload/libh64.so`, libdir app, files dir game.
