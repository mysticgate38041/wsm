# WSM — LESSONS LEARNED (KONSOLIDASI TOTAL)
*worm shadow untuk He · 2026-10-07 · hasil: saga debugging malam ini + audit 3 rival (Shaizuro/VounderS/Nifuji) + GT Full System Map + audit WSM sendiri*

---

## A. ARSITEKTUR & HOOK SYSTEM (pelajaran paling mahal)

**A1. SATU SLOT = SATU ALAMAT = SATU BLOK (INVARIAN MATI).**
Rebind slot ke alamat lain tanpa restore = alamat lama eksekusi blok baru → crash (terbukti 3×: "kena pukul" = poison A_auto). Aturan baru:
- restore alamat lama DULU (verifikasi byte pattern kita) → baru rebind
- alamat sudah ter-patch & orig belum tersimpan → **REFUSE** (jangan pernah bikin blok dari orig nol = UDF)
- default: pakai slot BARU, jangan pernah rebind sembarangan.

**A2. RESOLVER (cgm) = DISCOVERY, BUKAN KEBENARAN.**
Terbukti bisa mengembalikan alamat folded/salah (dua nama → satu alamat; nama → alamat bukan target). Aturan baru: **setiap alamat hasil resolver WAJIB divalidasi `== bias + dump_Offset`**; mismatch → pakai absolut dump. Ini membunuh seluruh kelas bug "alamat nyasar" selamanya.

**A3. VERIFIKASI BYTE SEBELUM PERCAYA.**
Setiap blok/patch baru = peek byte dulu (encoding adrp/ldr/cmp/b.eq = semua bisa divalidasi statis). Malam ini: god-block bytes = TERBUKTI benar via peek — bug-nya di bookkeeping, bukan encoding.

**A4. DUA DUNIA ABI — semua patch di dunia ARM64.**
x86 loader + engine; payload arm64 masuk via NativeBridgeLoadLibraryExt; houdini lazy-translate; absolute-jump patch (bukan trampoline); patch sebelum eksekusi pertama ATAU patch semua alias mapping (file double-mapped!). Payload arm64 wajib tanpa libc++_shared.

**A5. SEMUA SENTUHAN GAME LEWAT GUARDED_*.**
Fault di dalam guard = skip beat; fault di luar guard = mati by design (SIG_DFL+raise). Forensik crash = via logcat signal + "FEAT guard" logs — **tombstone TIDAK reliable** di konteks arm64-di-x86 bridge (jangan buang waktu cari tombstone).

**A6. HOOK = REVERSIBLE, SELALU.** (VounderS juga begitu: old_* per fitur — termasuk hook render!). Setiap install punya restore yang benar-benar jalan + tercatat.

## B. ENVIRONMENT (LDPlayer) — pelajaran operasional

**B1. Setiap game crash → watchdog LDPlayer (`com.android.ld.appstore`) mulai SIGKILL relaunch (signal 9) + render rusak (splash hitam). Obat: RESTART TOTAL HOST LDPlayer** (adb reboot tidak cukup — sudah kebukti 2×).
**B2. Jangan crash-loop**: setiap crash = biaya restart host + 5 menit. Berhenti, pahami, baru tes.
**B3. Launch pasca-boot = rapuh**: launch pertama sering mati; tunggu system settle (~2-3 menit post-boot) + launch ulang sabar.

## C. DARI 3 RIVAL (validasi eksternal)

**C1. VounderS membuktikan: target god/immortal hero = `CharacterDamagedBehaviour.Damage`** (bukan yang kita pakai). 3 opsi god kita sekarang: (A) opsi resmi Immortal|Invincible [paling murah] → (B) hook CharacterDamagedBehaviour.Damage [VounderS-proven] → (C) skip FOS.ApplyDamage [pasca-fix].
**C2. Nifuji membuktikan: XLua (271 wrap di GT!) = pintu kelas dewa** — inject Lua ke runtime game untuk fitur yang tak nyaman dari memori. Kandidat mode C kita.
**C3. Shaizuro membuktikan: by-name self-discovery (class_get_methods/nested_types)** = tahan update minor. Kita = by-name + dump absolute = **kombinasi terkuat** (mereka tak punya dump-verified absolute; kita punya).
**C4. NOL dari ketiganya punya auto-update runtime sungguhan** — klaim "auto update" = branding. Keunggulan kita nyata, bukan haluan.
**C5. Standar distribusi kelas atas (Nifuji): sha256 per-file anti-repack + dual-root + dual-ABI.** Kita bisa adopsi sha256 verify untuk modul kita.
**C6. Semua klien-side, tidak menyentuh server** = konfirmasi scope kita 100% benar (ranked/ekonomi = terlarang, bukan keterbatasan).

## D. DARI GT FULL SYSTEM MAP

**D1. Selalu mulai dari lapisan paling "resmi" game** (opsi Immortal|Invincible, dev-cheat classes, Command system) — baru hook jika perlu. Hook = opsi terakhir karena paling berisiko.
**D2. DamageInfo = 30 field obscured** — fitur damage jangan lewat asumsi nilai; pakai setter API (set_modifier/dll).
**D3. Peta serang lengkap sudah ada** (GT_FULL_SYSTEM_MAP.md §11): OHK→DamageCommand.OnExecute; Dumb→MonsterBattleAIState; Speed→get_WalkSpeed; NoCD→coolTime per BattleAction; Loot→DropItem; dst.
**D4. XLua surface (271 wrap) = roadmap fitur lanjutan** yang belum kita sentuh sama sekali.

## E. METODOLOGI (proses kerja)

**E1. BACA KODE DULU SEBELUM BUILD.** Tiga crash malam ini = akibat bookkeeping slot yang tidak dibaca ulang. Pelajaran: sebelum setiap build, audit jalur yang disentuh.
**E2. Bukti-sebelum-percaya**: setiap klaim = peek/log/counter/vision. Label jujur (LIVE/UJI/SEBAGIAN/RISET/SERVER).
**E3. Satu hipotesis per build**; jangan tembak-tembakan; kalau data belum cukup → BERHENTI & pahami (malam ini membuktikan).
**E4. Evidence discipline**: setiap deploy → log tersimpan; setiap fix → dokumen pelajaran.

## F. KONSEKUENSI LANGSUNG (yang berubah di WSM)

| # | Perubahan | Status |
|---|---|---|
| 1 | Payload: restore-alamat-lama + refuse-zero-orig + `g_hook_orig_addr[]` bookkeeping | **v28** |
| 2 | Engine: `godmode` → slot bebas (18) + parse arg diperbaiki + log alamat | **v28** |
| 3 | Resolver: validasi vs dump otomatis (fallback absolut) | **v28** |
| 4 | God mode urutan tes: A (opsi resmi) → C (skip ApplyDamage) → B (CharacterDamagedBehaviour) | tes berikut |
| 5 | Autohook log: sertakan ALAMAT tiap target | **v28** |
| 6 | Distribusi: sha256 verify (adopsi Nifuji-style) | roadmap |
| 7 | Fitur baru prioritas: Speed (get_WalkSpeed) → No-CD (coolTime) → ESP (DropItem) → Lua layer (271 wrap) | roadmap |

## G. EPILOG: GOD MODE JALAN — 2026-10-07 (v28b/v29c)

Rantai pembunuh bug yang akhirnya membuka god mode (setelah 3 generasi crash):
1. **Relokasi orig16**: target berprologue `adrp/br` (SEPERTI `stats.Damage` = stub) TIDAK BOLEH di-relokasi → crash houdini `0xdead1007` deterministik. Solusi: precheck dekode 4 kata pertama; skip target unsafe. (Fix: `[0] SKIP-UNSAFE` di v28b.)
2. **LDR unsigned-offset scaling**: `ldr x9,[x9,#imm12]` menskalakan ×8; offset mentah → baca alamat salah. Solusi: `adr x9,#+100; ldr x9,[x9]` untuk literal intra-blok.
3. **Skip-path out-params**: skip pada method multi-out WAJIB tulis nol ke x2/x3 sebelum ret (caller membaca outs; sampah = pointer crash).
4. **Skip-direct install**: mismatch resolver → langsung pasang di alamat dump (nol rebind = nol orphan-alias).
5. **Precheck patch-aware**: blok precheck harus kenali patch milik kita sendiri (`58000051 d61f0220`) agar tidak salah tolak.
6. **LDPlayer launch-2x**: launch pertama pasca-boot selalu dibunuh ~0.5s (cgroup kill watchdog); relaunch cepat = hidup.
7. **Verifikasi byte blok sebelum tes**: peek entry + adr/ldr/cmp/b.eq + orig16 + skip tail + literal.

**HASIL TERVERIFIKASI LIVE**: hero HP TIDAK turun saat dipukul, musuh tetap kena damage, NOL crash,
`OnDamageRecorder=68 / CheckWillDie=68 / mdb.Damage=58 / kills=5` di pipeline asli. God mode = `godmode` (slot 18 @ `0x400028fb64b4`).

— SELESAI. Ini buku pelajaran resmi WSM. 🪱
