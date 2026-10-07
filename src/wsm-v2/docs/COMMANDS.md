> Daftar penelitian lama; banyak command sengaja tidak tersedia di dispatcher WSM 6. Daftar produksi terbaru: `COMMANDS_V6.md`.

# COMMANDS — WSM v5.0 Toolkit Reference

> Semua command ditulis ke file control-plane & dieksekusi engine (deferral berlaku via menu).
> Channel: `/storage/emulated/0/Android/data/com.kakaogames.gdts/files/wsm_cmd` → ack di `wsm_ack`.
> **Catatan dedupe**: content identik tidak dieksekusi ulang — tambah spasi untuk re-run.

## Kontrol Umum
| Command | Fungsi |
|---|---|
| `status` | ringkasan (instance, modsN, feats aktif) |
| `featdiag` | rantai resolve + stats player (opts/faults) |
| `panic` | kill-switch: semua fitur OFF + reset |
| `feat <id> <val>` | toggle/set fitur (lihat tabel di bawah) |

## Fitur (feat)
| id | val | Fungsi |
|---|---|---|
| `god` | 0/1 | Immortal+Invincible (mask 3) |
| `hp` | 0/1 | Immortal (mask 1) |
| `stam` / `mana` | 0/1 | top-up stamina/mana kontinu |
| `poise` | 0/1 | super armor (mask 52) |
| `immune` | 0/1 | kebal ailment (mask 224) |
| `timescale` | float | kecepatan game (0.1–5) |
| `ohk` | 0/1 | auto-kill sapuan (combo penuh, radius aura) |
| `aura` | m | radius sapuan 5–40 |
| `dmg` | ×100rb | kekuatan kill (default 10 = 1jt) |
| `onehp` | 0/1 | musuh drop ke 1 HP (standalone) |
| `crit` | 0/1 | flag kritikal di info kill |
| `stunall` | 0/1 | stun animation resmi (gemetar; gerak tidak terkunci) · [SEBAGIAN] |

## Aksi
| Command | Fungsi |
|---|---|
| `sweep` | sapu SEMUA musuh asli di stage (combo kill) |
| `tpr <dx> <dz>` | teleport relatif (meter) — read-back verifikasi |
| `tp <x> <z>` | teleport absolut (koordinat dunia) |

## Diagnostik Musuh (x-ray toolkit)
| Command | Fungsi |
|---|---|
| `mlist` | seluruh list musuh: `idx:ptr:act:dead:hp` + live count |
| `mpos` | posisi hero + semua musuh (deteksi dummy 999) |
| `mread <idx>` | detail satu musuh |
| `mdmg <idx> [5/6/9]` | gebuk 1 musuh: 5=stats+behaviour, 6=+Die, 9=combo penuh |
| `mnear [mode]` | gebuk musuh TERDEKAT dari hero (GPS targeting) |
| `msweep [mode]` | versi raw sweep (semua target, no radius) |
| `kill1..kill8` | matriks uji jalur damage (riwayat riset) |
| `pulse1` | x-ray pipeline pulse (per-tahap fault) |
| `aggro1` | x-ray battle resolution (aggro) |
| `hookprobe` | baca MethodInfo live — verifikasi offset methodPointer |
| `kcmd` | uji MonsterDeadCommand (command kill resmi) — **[F] fault, riset** |

## ⚠️ ALAT RISET CRASH-SENSITIF (jangan dipakai tanpa pengawasan)
| Command | Fungsi |
|---|---|
| `stunvec <1\|2\|3>` | pilih vektor pulse stun (default **3**=aman; 1=state-inject, 2=command) |
| `stunprobe` | x-ray rantai stun (state-inject di dalamnya = **CRASH**) |
| `stunui` | state-inject dari UI thread via menu (**CRASH** di kedua thread) |

## Kontrol Time Mods (lama)
| Command | Fungsi |
|---|---|
| `mod <x> wsm` / `unmod wsm` / `clearmods` | pengali waktu via GlobalTimeManager |

## Status Label (menurut bukti)
- **[LIVE]** lolos uji + bukti visual/data · **[BARU]** baru, siap uji
- **[UJI]** siap diuji · **[SEBAGIAN]** efek parsial · **[BELUM]** tidak didukung jalur saat ini
- **[SERVER]** otoritas server, bukan wilayah klien · **[RISET]** butuh riset lanjut
