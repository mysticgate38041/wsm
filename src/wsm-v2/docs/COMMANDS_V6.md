# Command produksi WSM 6.3

Referensi command untuk `v6.3.0-rc1`. Target identity tetap package `com.kakaogames.gdts`, versionName `3.54.0` dan versionCode `423`. `epoch E command` mewajibkan epoch cocok saat enqueue; PANIC menaikkan epoch dan membatalkan permintaan lama. Katalog adalah referensi cakupan, bukan 47 toggle aktif.

Arsitektur/state menu: [modernisasi v6.3](../../../docs/MODERNIZATION_V6_3.md). Bukti RC2 dan semantik desain lama tetap berada di [audit dump](ORIGINAL_DUMP_AUDIT.md) dan [finalisasi 47](FINALIZATION_47.md). ACK tidak membuktikan seluruh efek gameplay.

Native UI dan file transport masuk dispatcher yang sama; parser lama di `engine.cpp` merupakan artefak penelitian dan tidak diekspos langsung.

| Command | Rentang / efek |
|---|---|
| `status` | Snapshot JSON dengan epoch/revisi kontrol; fitur kosong ketika status belum siap/restoring |
| `telemetry` | Snapshot ditambah revision dan metrik antrean/completion; hanya diagnostik |
| `catalog` / `catalog 0..11` | Baca katalog 47 fitur beserta status cakupannya |
| `result ID` | Completion ring 64 entri; `expired` bila ID tidak tersimpan |
| `panic` | Batalkan command belum dijalankan; hentikan pulse/refill/loot; coba restore semua slot dan opsi milik WSM |
| `selftest` | Muat helper; 6 × 7 = 42; eksekusi branch/getter wrapper/hero predicate/GOD/neighbor/restore pada kode sintetis; efek game tetap diuji terpisah |
| `feat god/hp/stam/mana/poise/immune/ohk/onehp/crit 0\|1` | Toggle konfigurasi fitur |
| `feat aura 0` atau `feat aura 5..40` | OFF / radius pulse m |
| `feat dmg 0` atau `feat dmg 1..99` | OFF / pulse power ×100.000 |
| `feat timescale 0` atau `feat timescale 0.1..5` | OFF idempotent / modifier waktu bernama wsm, ON membutuhkan instance game yang hidup |
| `speed 0` atau `speed 1..5` | Restore / skala tiga getter gerak hero |
| `critdmg 0` atau `critdmg 1..5` | Restore / skala critical damage hero, eksperimental |
| `godmode 0\|1` | Restore / damage guard khusus hero, eksperimental |
| `nocd 0\|1` | Restore / tiga cooldown gates, eksperimental |
| `stunall 0\|1` | Restore / freeze AI hook; tidak menjalankan StunCommand lama |
| `loot 0\|1` | Stop / permintaan auto-loot; hasil reward tidak diasumsikan |
| `tpr DX DZ` | Relatif, masing-masing -100..100, readback posisi |
| `sweep` | Sekali damage sweep dengan diagnostik target/fault |

Setiap command mutasi selain PANIC membutuhkan identity benar, foreground, hero dalam scene, serta session tanpa fault. Parser menolak parameter ekstra, NaN/Infinity, rentang invalid, arbitrary peek/poke, watchpoint, dump dan hookabs. Antrean 32 command, history 64 result. PANIC tidak menghentikan command yang sudah berada dalam panggilan native; ia mengambil slot eksekusi berikutnya dan membatalkan antrean sisanya.

State result: `accepted` (belum dieksekusi), `applied`, `rejected`, `stale` (epoch/PANIC), `fault`; request invalid/full ditolak. UI timeout tidak membuktikan pembatalan request; snapshot/result menjadi sumber status berikutnya. Session fault dan timeout bus tidak di-reset otomatis, memerlukan restart proses.

File transport: `/data/user/0/com.kakaogames.gdts/files/.7d1b0c33aa94e6f28e5b10c4d9a2f607` → `.7d1b0c33aa94e6f28e5b10c4d9a2f608` (v6.4: nama netral bergaya file token milik app, menggantikan `wsm_cmd`/`wsm_ack`), ACK mode 0600 dan tanpa mengikuti symlink. Frame `@REQUEST_ID command` menghasilkan JSON dengan `request` yang sama; frame unik memungkinkan command identik diulang. `scripts/wsmctl.py` mengurus korelasi dan completion. Native menu tidak membutuhkan permission overlay maupun file transport.

Hotkey saat menu berfokus: End = PANIC, Insert = collapse, F1–F6 = enam kontrol karakter pertama. Keyboard global Android/game tidak di-hook.

## Grup GM (v6.4)

Komposisi di atas kontrol yang sudah terverifikasi — tanpa jalur engine baru: setiap operasi memakai handler produksi yang persis sama dengan command tunggalnya. Preset hidup di memori engine (ikut siklus proses), tidak menyentuh disk. Semua command GM melewati gerbang session yang sama (identity, foreground, hero dalam scene, tanpa fault); `gm reset` dikecualikan seperti PANIC.

| Command | Rentang / efek |
|---|---|
| `gm status` | Ringkasan satu baris: special (godmode/nocd/loot/stunall/speed/critdmg), 12 feat, dan preset tersimpan |
| `gm reset` | Setara PANIC: semua kontrol OFF + restore default + epoch baru |
| `gm max <id>` | Set ke maksimum: 9 toggle (`god hp stam mana poise immune ohk crit onehp`) → ON; `aura`→40; `dmg`→99; `timescale`→5; `speed`→5; `critdmg`→5 |
| `gm all 0\|1` | Bundle ON/OFF 12 operasi: godmode + 9 feat tetap + `dmg 99` + `nocd` |
| `gm preset save <nama>` | Simpan state lengkap (nama ≤16 char `[a-z0-9_]`, maks 8 slot, in-memory) |
| `gm preset load <nama>` | Terapkan ulang seluruh state preset lewat handler yang sama |
| `gm preset list` | Daftar preset tersimpan |

Batas kebijakan tetap: ekonomi server-authoritative (gem/gold/gacha/reward progression) tidak termasuk — di luar jangkauan klien; grup GM berada pada scope single-player dan tidak menyentuh mode ranked/co-op.
