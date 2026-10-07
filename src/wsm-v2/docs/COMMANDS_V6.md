# Command produksi WSM 6

Kandidat terbaru RC2 mempertahankan command RC1 dan menambahkan dukungan relokasi ADR/ADRP untuk wrapped getter speed/critical damage. Prologue dengan branch/literal load yang tidak didukung tetap ditolak. Bukti dump dan uji ARM64 sintetis berada di `ORIGINAL_DUMP_AUDIT.md`; command applied belum menjadi bukti efek damage game.

Pembaruan kandidat 6.1.0 RC1: `catalog` / `catalog 0..11` membaca seluruh 47 fitur desain dan statusnya; `critdmg 0` / `critdmg 1..5` mengendalikan getter float hero-only eksperimental pada slot 26. `epoch E command` mewajibkan epoch E cocok ketika enqueue; UI memakai bentuk ini untuk mutasi. PANIC menaikkan epoch di bawah queue lock sehingga producer lama ditolak. Identity kini memerlukan package, versionName 3.54.0 dan versionCode 423. Semua command yang belum diimplementasikan tetap ditolak; katalog bukan toggle. Lihat `FINALIZATION_47.md` untuk cakupan dan bukti kandidat.

Native UI dan file transport masuk dispatcher yang sama; parser lama di `engine.cpp` merupakan artefak penelitian dan tidak diekspos langsung.

| Command | Rentang / efek |
|---|---|
| `status`, `telemetry` | Snapshot JSON; hanya membaca salinan state |
| `result ID` | Completion ring 64 entri; `expired` bila ID tidak tersimpan |
| `panic` | Batalkan command belum dijalankan; hentikan pulse/refill/loot; coba restore semua slot dan opsi milik WSM |
| `selftest` | Muat helper; 6 × 7 = 42; eksekusi branch/getter wrapper/hero predicate/GOD/neighbor/restore pada kode sintetis; efek game tetap diuji terpisah |
| `feat god/hp/stam/mana/poise/immune/ohk/onehp/crit 0\|1` | Toggle konfigurasi fitur |
| `feat aura 0` atau `feat aura 5..40` | OFF / radius pulse m |
| `feat dmg 0` atau `feat dmg 1..99` | OFF / pulse power ×100.000 |
| `feat timescale 0` atau `feat timescale 0.1..5` | OFF idempotent / modifier waktu bernama wsm, ON membutuhkan instance game yang hidup |
| `speed 0` atau `speed 1..5` | Restore / skala tiga getter gerak hero |
| `godmode 0\|1` | Restore / damage guard khusus hero, eksperimental |
| `nocd 0\|1` | Restore / tiga cooldown gates, eksperimental |
| `stunall 0\|1` | Restore / freeze AI hook; tidak menjalankan StunCommand lama |
| `loot 0\|1` | Stop / permintaan auto-loot; hasil reward tidak diasumsikan |
| `tpr DX DZ` | Relatif, masing-masing -100..100, readback posisi |
| `sweep` | Sekali damage sweep dengan diagnostik target/fault |

Setiap command mutasi selain PANIC membutuhkan identity benar, foreground, hero dalam scene, serta session tanpa fault. Parser menolak parameter ekstra, NaN/Infinity, rentang invalid, arbitrary peek/poke, watchpoint, dump dan hookabs. Antrean 32 command, history 64 result. PANIC tidak menghentikan command yang sudah berada dalam panggilan native; ia mengambil slot eksekusi berikutnya dan membatalkan antrean sisanya.

State result: `accepted` (belum dieksekusi), `applied`, `rejected`, `stale` (epoch/PANIC), `fault`; request invalid/full ditolak. UI timeout tidak membuktikan pembatalan request; snapshot/result menjadi sumber status berikutnya. Session fault dan timeout bus tidak di-reset otomatis, memerlukan restart proses.

File transport: `/data/user/0/com.kakaogames.gdts/files/wsm_cmd` → `wsm_ack`, ACK mode 0600 dan tanpa mengikuti symlink. Frame `@REQUEST_ID command` menghasilkan JSON dengan `request` yang sama; frame unik memungkinkan command identik diulang. `scripts/wsmctl.py` mengurus korelasi dan completion. Native menu tidak membutuhkan permission overlay maupun file transport.

Hotkey saat menu berfokus: End = PANIC, Insert = collapse, F1–F6 = enam kontrol karakter pertama. Keyboard global Android/game tidak di-hook.
