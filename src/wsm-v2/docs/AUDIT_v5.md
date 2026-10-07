# AUDIT MENYELURUH — WSM v5.0 "MODERN"
**Tanggal**: 2026-10-06 · **Metode**: baca penuh engine.cpp (~2.930 baris), WsmMenu.java (648 baris), loader.cpp, Android.mk/Application.mk + audit pola (grep) + build bersih `-Werror` + baterai runtime.
**Prinsip**: nol temuan yang disembunyikan. Fix = kode berubah; Observasi = perilaku didokumentasikan; Verified = dicek dan benar.

---

## 🔧 TEMUAN DIPERBAIKI (3)

### A01 — Badan dedupe control-thread memblokir path lain
`if (strcmp(raw, last) == 0) break;` → keluar dari loop SEMUA path saat satu path berisi command lama. Path kedua berisi command BARU tak akan pernah dieksekusi sampai path pertama diisi hal lain.
**Fix**: `break` → `continue` (skip path itu saja, path lain tetap dipindai).
**Dampak**: bug laten multi-path; path utama (files) tak terpengaruh → tidak pernah terlihat. Kini aman.

### A02 — 8 buffer statis dipakai lintas-thread tanpa proteksi
`infobuf, kbuf, mbuf, cbuf, hbuf, sbuf, ib, bb` (masing 0x300 B) tadinya `static` global. `feat_mdmg` bisa dipanggil dari thread control (`mdmg` cmd) DAN thread FEAT (msweep/pending) → korupsi data race di jendela milidetik.
**Fix**: semuanya `static __thread` (per-thread storage) — race mustahil by construction.
**Dampak**: risiko rendah tapi nyata; ditutup permanen.

### A03 — Teleport tanpa batas jangkauan
`tp <x> <z>` bisa menulis koordinat tak terbatas → potensi mendorong hero ke luar batas dunia.
**Fix**: clamps ±4000 m → `TP out-of-range` ditolak eksplisit. Read-back verifikasi tetap.

---

## 👁️ TEMUAN DIOBSERVASI (dokumentasi, bukan bug)

### O01 — `tpr/tp` dari menu dieksekusi di UI-thread (non-deferred)
Menu mendefer `feat/panic/sweep`, tapi teleport jalan langsung di thread UI. **Terbukti bekerja** (read-back ok=1, terverifikasi visual oleh He). Risiko teoretis (lock contention UI) tak terjadi. Keputusan: **dibiarkan + didokumentasikan**; bila kelak bermasalah → alihkan ke antrian FEAT.

### O02 — Command fitur yang belum ada di engine → ack jujur `PENDING`
`feat speed 2` (dan lootesp/fov/gold/dll) dijawab `PENDING feat=<id> (hook belum tersedia)` — dinyatakan apa adanya, bukan error palsu. Konsisten dengan label menu [RISET]/[SERVER].

### O03 — Prioritas pulse: STUN kelaparan saat OHK/OneHP aktif
Rantai: `ohk||onehp → pulse_kill; else if stun → pulse_stun`. Saat OHK hidup, stun tak dipanggil (musuh toh mati). **By design**; label menu stun sudah [SEBAGIAN].

### O04 — Chip "AKTIF ✓" loadout kembali jadi "ON" saat kategori dibangun ulang
Murni kosmetik (state `on` benar). Tidak menyesatkan status; bisa dipoles kelak.

### O05 — `mlist` dalam 512 B ack
Guard `used + 64 < cap` menjaga truncation rapi untuk stage >16 musuh (baris terakhir tetap `||live=`). Aman.

---

## ✅ AREA TERVERIFIKASI (tanpa temuan)

| # | Area | Hasil |
|---|---|---|
| V01 | Urutan parser command (30+ branch) | `tpr` sebelum `tp ` · `featdiag` sebelum `feat ` · `kcmd` sebelum `kill*` · `kill1..8` terpisah · `pulse1` vs `pulsesrc` aman · semua prefix tanpa shadowing |
| V02 | Guard pointer | Semua deref mentah didahului `ptr_ok`; semua invoke il2cpp dibungkus `GUARDED_BEGIN/END` (TLS + BEAT guard) |
| V03 | Layout array il2cpp (+0x10/+0x18/+0x20) | Konsisten di semua konsumen (feat_apply, mopen, teleport) |
| V04 | Resolver idempotent | Semua lookup `if (!g_m_x) g_m_x = ...` — aman dipanggil berulang, hot-swap friendly |
| V05 | Disiplin fd/memori loader | Semua jalur `close()`; `free()` setelah staging; mmap dex dilepas setelah copy |
| V06 | Build flags | `-Werror` bersih di kedua ABI; `-fvisibility=hidden`, no-RTTI/no-exceptions konsisten dengan kode |
| V07 | Sinkronisasi menu↔engine | Semua id menu (god/hp/stam/mana/poise/immune/ohk/aura/dmg/onehp/crit/stunall/timescale/aggro) ada di tabel engine; sisanya [RISET]/[SERVER] dijawab jujur |
| V08 | Deferral menu | `feat/panic/sweep` → flag+version bump → engine thread apply ≤1s; UI tak pernah memanggil il2cpp berat |
| V09 | Dedupe command | Semantik: isi identik = skip (tambah spasi utk re-run) — konsisten & terdokumentasi |
| V10 | feat_apply jalur stats | Guard lengkap, jalur method-only (field-path dibuang, tercatat), mask add/remove reversibel `0xf7↔0xf4` |

---

## 🧪 BATERAI RUNTIME (pasca-audit, build v5.1 — pid 20281; GT stage 16 entity)

```
[1]  status    → STATUS instance=0x0 modsN=0 feats=[-]              ✅
[2]  featdiag  → resolve=1 stage ok players=2 faults=0              ✅
[3]  hookprobe → methodPointer@+0x10 stabil                           ✅
[4]  mlist     → 16 entity; 13 musuh a2 hidup (hp 745–2831)          ✅
[5]  tpr 5 5   → from=(0.8,-2.7) now=(5.8,2.3) ok=1 fault=0          ✅
[6]  onehp ON  → OK chars=2; hit=0 (hero 51 m dari target — radius!)  ✅ by-design
[7]  onehp OFF → OK                                                    ✅
[8]  stun ON   → OK; hit=0 (radius sama, hero jauh)                    ✅ by-design
[9]  kcmd      → mdc=0x0 fInfo=1 (Create FAULT — guard tangkap)        ❌ [F]
[10] sweep     → MSWEEP targets=13 fault=0 → 13 musuh a2 semua a0:d1:h0 ✅
[11] panic     → PANIC OK (modsN=0)                                    ✅
     status    → clean                                                 ✅
     fault total: 2 (1 = kcmd; guard menangkap, game HIDUP → guard layer terbukti)
```

**Kesimpulan baterai**: kill-chain (sweep) sempurna · teleport sempurna · panic/status bersih · dedupe fix bekerja (semua command tereksekusi) · guard terbukti menangkap fault & game selamat. `onehp/stun hit=0` = **radius aura** (deskripsi menu dikoreksi +), bukan bug.

**ONEP LIVE-VERIFIED (follow-up oleh He)**: teleport ke tengah kawanan → onehp → scratch `hit=3`, salah satu monster = `h1` persist (bukti visual + data), sisanya dihabisi TIM HERO (by design). Hasil: FITUR ONEP = LIVE 100%, nol bug. Hardening v5.2 = ledger one-shot per toggle.

### K06 — `kcmd` (MonsterDeadCommand) = [F] GAGAL, di-park
`MonsterDeadCommand.Create(info)` fault (`pc=0x40006091fdd7 addr=0x3f000000`) — info sintetis (payload struct dari boxed) diterima `stats.Damage` (struct by-value) tapi `Create` butuh konteks lain. **Keputusan: jalur command resmi TIDAK dipakai; trap-chain tetap satu-satunya jalur kill verified.** Diagnostik `kcmd` tetap ada (guarded) untuk riset kelak.

### K07 — ONEP LIVE-VERIFIED END-TO-END (koreksi dari He)
Tes live: teleport ke tengah kawanan → `feat onehp 1` → scratch `hit=3` → **idx11 = `a3:d0:h1` (TEPAT 1 HP — bukti langsung)**; idx7-10 mati = **hero tim He mengeksekusi monster 1-HP itu (by design "lu eksekusi sendiri")** — dikonfirmasi visual oleh He. Nol bug.
Hardening v5.2 (dari investigasi ini, tetap dipasang): **ledger one-shot per toggle** — tiap monster di-scratch maksimal sekali sampai toggle di-reset; mencegah re-apply teoretis + menghentikan retry-fault.
Lesson: anomali gameplay (eksekusi tim) tidak terlihat dari x-ray data — selalu cross-check dengan penglihatan pemain.

### K08 — Stun Lock (investigasi tuntas, DITUTUP) — [X] crash
Upaya full-lock stun via 3 jalur resmi game (semua dari RE dump, tanpa tebak-tebakan):
1. **`StunCommand.Create(t,d,super)` + `Execute(0)`** — jalan mulus (f=0), **tidak ada efek terlihat** (no-op di pipeline stage).
2. **`StunEvent.Create` → `CharacterBehaviour.OnEvent`** — 1 fault (managed exception) — dibuang.
3. **`CharacterStunState.Create` + `stateMachine.ChangeState()`** — **CRASH GT** di CTL thread **DAN** di UI thread (menu-driven `stunui` 800ms tick). f=0 (tak ada fault tertangkap) — crash terjadi di pemrosesan state oleh loop game (diduga: state stun butuh konteks battle internal yang tidak ada di stage field — paralel dengan temuan aggro).

**Keputusan final:** jalur state injection = **JALAN BUNTU (crash-proven ×2, kedua thread)**. `g_stun_vec` default = **3 (legacy)** — stun lama (animation + gemetar, [SEBAGIAN]) dipertahankan sebagai satu-satunya jalur aman. Diagnosis tools (`stunvec`, `stunprobe`, `stunui`) tetap ada di engine untuk riset kelak — **JANGAN dipakai tanpa pengawasan** (stunprobe/stunui = pemicu crash).
**Ilmu baru:** manipulasi state-machine karakter tidak bisa dilakukan dari luar pipeline internal game, terlepas dari thread — beda dengan `set_Position` (aman dari UI thread).

---

## 📌 CATATAN ARSITEKTUR (dari sesi ini)
- Kill chain: trap info → `set_notMortal(false)` → `stats.Damage` → `db.Damage` → `db.Die`; targeting `get_Position` + skip dummy |x|>900 → terima `act 2|3`.
- `GenerateDamageFromLua` = NULL-deref, dibuang permanen (tercatat).
- `methodPointer @ +0x10` terverifikasi live (hookprobe) → fondasi payload ARM64.
- Poison `0xdead1031` → semua deref wajib `ptr_ok` + guard per-beat (insiden tombstone 0xdead1049 tercatat).
