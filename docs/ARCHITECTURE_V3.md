# WSM v3 yang dikoreksi — 6.2.0 RC1

> Catatan migrasi v6.2. Perubahan runtime/menu berikutnya dijelaskan dalam [modernisasi v6.3](MODERNIZATION_V6_3.md); [indeks dokumentasi](README.md) menunjukkan panduan terkini.

Dokumen ini menjelaskan implementasi migrasi WSM 6.2.0 RC1 (`wsm-v6.2.0-rc1`, module versionCode `60201`). Target tetap exact: package `com.kakaogames.gdts`, versionName `3.54.0`, versionCode `423`, minimum Android API 26, ABI `x86_64` dan `arm64-v8a`. Proposal “WSM v3 — Full Architecture” dipakai sebagai masukan desain; klaim dan potongan kode di dalamnya bukan hasil pengujian implementasi ini.

## Peta source aktif

| Modul | Tanggung jawab dan batas |
|---|---|
| `src/wsm-v2/jni/module.cpp` | Entry Zygisk, allowlist, pembacaan komponen sebelum specialization, bootstrap engine/DEX |
| `jni/loader.cpp` | Shim source untuk kompatibilitas; build aktif memakai module.cpp |
| `jni/engine.cpp`, `jni/modern_control.inc` | Integrasi lifecycle, backend RC3/legacy yang dipertahankan, owner reset, publikasi dan callback UnityMain |
| `jni/wsm_runtime.h`, `jni/futex_bus.h` | Antrean bounded, result ring, epoch, notification generation dan condition variable |
| `jni/dispatcher.h/.cpp` | Allowlist/range command dan escaping JSON; bukan executor fitur game |
| `jni/payload_worker.h/.cpp` | Pemilihan deadline worker dari foreground, active, fault dan main pending |
| `jni/feature_flags.h/.cpp` | Cache observed-state koheren untuk 18 kontrol dan lifecycle epoch/revision |
| `jni/il2cpp_api.h`, `jni/il2cpp_resolver.h/.cpp` | API metadata, kontrak signature, identity dan tipe MethodInfo/pointer native yang dibedakan |
| `jni/aob_scanner.h/.cpp`, `jni/hybrid_resolver.h/.cpp` | Discovery diagnostik pada span eksplisit; qualified binding tetap memerlukan identity/signature metadata |
| `jni/wsm_binding.h` | Include facade kompatibilitas untuk binding lama |
| `jni/wsm_bus.h`, `jni/shared_bus_event.h` | Layout mailbox helper dan ordinary futex notification melalui libc |
| `jni/trampoline_pool.h/.cpp` | Reservasi bounded halaman owned, validasi reachable/alignment, cache flush dan seal RX |
| `jni/wsm_patch.h`, `jni/wsm_arm64_reloc.h` | Patch alias/readback/rollback serta relokasi prologue yang didukung |
| `payload/h64.cpp`, `jni/wsm_sweep.h` | Helper ARM64/native bridge, owned slot dispatch, guarded UnityMain damage sweep dan GC lifetime |
| `menu/WsmMenu.java`, `menu/ControlState.java` | UI Android dan correlation token/epoch; state tidak dipaksakan ON dari submit |
| `jni/Android.mk`, `jni/Application.mk`, `jni/CMakeLists.txt` | Dua backend build dengan ABI, API, STL, export dan alignment yang sama |

Nama path pada tabel relatif terhadap `src/wsm-v2` setelah baris pertama. Pemecahan modul memisahkan layanan generik dan kontraknya; engine legacy yang masih dipakai tidak diport seluruhnya ke offset baru atau dibuang tanpa bukti.

## Alur command, result dan observed state

Menu atau transport app-private mengirim command tervalidasi. Runtime menerima paling banyak 32 pending command, command <192 byte, dan menyimpan 64 result. ID result, expected epoch dan snapshot epoch dipakai untuk correlation; consumer tidak boleh menyamakan accepted dengan efek game. Worker memproses paling banyak empat command setiap beat agar producer tidak menahan maintenance.

Condition variable menggunakan generation yang diubah di bawah mutex Runtime. Submit/notify sebelum worker masuk wait tetap teramati; shutdown juga membangunkan waiter. Queue bukan diganti oleh satu variabel command. Terminal result tidak dapat ditimpa oleh completion duplikat, dan completion dari epoch lama tidak berubah menjadi sukses.

`FeatureFlags` mengamati state yang diterbitkan backend dan mengikatnya pada epoch/revision. Snapshot multi-field koheren; reset menonaktifkan 18 kontrol dan mengembalikan default yang sesuai engine. Enabled observation membutuhkan lifecycle ready dan epoch yang cocok. Cache ini tidak memanggil API game, tidak menggantikan restoration dan tidak membuat applied ACK.

Ada 18 ID kontrol: god, hp, stam, mana, poise, immune, godmode, ohk, onehp, aura, dmg, crit, critdmg, nocd, stunall, speed, loot, timescale. Pulse Power/dmg mengisi Int32 damage factory sebesar nilai bulat 1–99 ×100.000. Critical Pulse/crit mengatur `critical=true` serta `noCritical=false` pada DamageInfo yang dipin. Konfigurasi ON diperbolehkan setelah binding signature tepat; snapshot policy tetap immutable sampai ticket selesai. ONEHP menekan keduanya, OHK berprioritas, dan Clear Stage tetap damage 1.000.000. Konfigurasi armed belum membuktikan efek gameplay. Katalog 47 fitur mempertahankan `completed=0` dan qualification `UNVERIFIED`; tidak ada klaim seluruh 18 kontrol atau 47 semantik desain selesai.

## Worker dan shared bus

| Keadaan | Batas deadline maintenance |
|---|---:|
| OFF tanpa fitur aktif | 1.000 ms |
| Background atau fault | 1.000 ms |
| Fitur aktif | 250 ms, dapat diperpendek oleh feature deadline |
| Callback UnityMain pending | 25 ms |
| Command/lifecycle notification | Worker dibangunkan; latency eksekusi tetap bergantung pekerjaan yang sedang berjalan |

Deadline ini mengurangi polling rutin dibanding interval tetap 250 ms saat OFF. Worker tetap perlu memeriksa lifecycle/maintenance; tidak ada target atau bukti “zero CPU”. Konfigurasi deadline bukan pengukuran latency aktual.

Mailbox helper tetap 160 word, 32 slot dan token ACK. Generation word milik WSM membangunkan waiters lewat futex biasa melalui libc. Urutan acquire/release dan pemeriksaan generation sebelum wait menutup race notify-before-wait; helper idle tetap mempunyai timeout untuk maintenance. Notification tidak memperluas otoritas mailbox: bus identity, nonce, token dan timeout quarantine masih diperlukan.

## Lifecycle, PANIC dan restoration

Epoch berubah pada invalidasi scene/stage/hero dan aktivitas. PANIC menaikkan epoch serta menandai pending command stale sebelum owner worker melakukan reset fisik state. Engine mempertahankan `g_control_epoch`: bila owner epoch belum sama dengan Runtime epoch, snapshot menunjukkan `reset_pending`, ready false dan fitur kosong. Publikasi final juga memakai commit gate epoch agar producer yang mengubah epoch tidak menerima snapshot lama.

Reset melepas opsi/patch/modifier yang dimiliki WSM melalui backend restoration. Time Scale melepas modifier bernama `wsm`, dan ledger opsi tidak membersihkan state pihak lain. Native call yang sudah berjalan tidak dapat dibatalkan seketika. UnityMain sweep yang pending dibatalkan melalui jalur owned dan dipoll sampai completion sebelum reset yang tertunda diselesaikan. Channel/restoration yang tidak pasti membuat sesi fault; restart proses tetap diperlukan.

Sweep RC3 mempertahankan stage/membership checks, pinned GC handles dan pemanggilan pipeline damage game melalui callback yang memverifikasi UnityMain. Found MethodInfo tidak membuktikan object receiver, lifetime, thread, ownership atau efek gameplay. Legacy experiment tidak dibuka melalui dispatcher produksi.

## Resolver dan scanner

Binding wajib memenuhi identity target exact dan signature lengkap, termasuk method name, parameter/return type dan staticness. Ambiguitas, enumerasi terpotong, kontrak invalid dan native pointer yang tidak executable ditolak. MethodInfoRef dan NativeMethodPointer adalah tipe berbeda; metadata tidak diperlakukan sebagai kode atau handle dlopen sebagai base library.

AOB menerima span byte eksplisit, masked pattern, batas pencarian/iterasi dan alignment. Scanner tidak melakukan pencarian proses tanpa batas. Hasil unique AOB tetap hanya discovery diagnostik; ia tidak menyediakan MethodInfo, executable code, offset fallback atau qualification pada game versi berbeda. Metode yang tidak terikat dengan metadata tetap tidak boleh dijalankan berdasarkan kandidat AOB. Binding GlobalTimeManager yang belum tersedia saat bootstrap di-retry secara lazy oleh owner setelah identity diverifikasi, sehingga identity yang datang sesudah bootstrap tidak meninggalkan binding null secara permanen.

## Pool dan patch owned

Pool berkapasitas 512 halaman dengan stride 16 KiB, memakai arena statis aligned atau allocator near yang lolos reachability. Arena 8 MiB adalah kapasitas ruang virtual, bukan hasil ukur committed RAM. Halaman dipersiapkan RW, instruction cache di-flush dan seal RX harus berhasil sebelum branch target dipublikasikan. Jangkauan branch ±128 MiB, alignment, prologue relocation dan rollback tetap diperiksa.

Halaman yang telah dipublikasikan dipertahankan sampai proses berakhir. OFF/restore tidak otomatis membuktikan semua eksekutor lama sudah quiescent, sehingga pool tidak mendaur ulang halaman published menjadi slot 16 byte yang dapat ditulis ulang. Tail branch menjaga semantik return/prologue; pola BL+RET dari proposal tidak diadopsi.

## Bagian proposal yang tidak diterapkan

Ghost threads, menghentikan atau memarkir thread XShield/XIgncode, intersepsi monitoring, stealth shadow remap dan raw syscall untuk menghindari monitoring tidak termasuk implementasi. Tidak ada perubahan game data/account, pembuktian 47 efek gameplay, angka RAM/CPU/latency hasil ukur atau klaim kompatibel dengan game versi lain.

## Build dan verifikasi

Build default memakai ndk-build; CMake ≥3.22/Ninja merupakan backend kedua. NDK exact `27.2.12479018` (r27c), JDK 17, Python ≥3.10, SDK platform 34/build-tools 34.0.0, native API 26 dan STL statis tetap menjadi kontrak. Output dua loader, dua engine, satu helper ARM64 dan satu DEX diperiksa oleh script package. Receipt mencatat source modular dan hash binary; source yang berubah setelah checkpoint atau receipt yang tidak lengkap ditolak.

Pada migrasi awal, sebelas fixture native mencakup runtime, patch, binding, reloc, sweep, flags, resolver, dispatcher, worker, pool dan bus_event. Build Android mengompilasi semuanya untuk kedua ABI. CMake host/CTest disiapkan untuk mengeksekusi fixture pada POSIX/Linux; Windows lokal melakukan cross-compile executable Android; seluruh sebelas fixture per ABI kemudian dieksekusi pada LDPlayer dan 22/22 lulus. ARM64 memakai translation di kernel x86_64, bukan hardware ARM64 fisik. Kernel perangkat fixture memakai page size 4 KiB; runtime perangkat 16 KiB belum diuji. Package/catalog/Java checks menguji kontrak lokal dan tidak membuktikan gameplay.

Build migrasi awal backend ndk-build dan CMake lulus gate lima ELF + DEX serta Java state, 35 package regression tests dan sembilan catalog tests. Installer harness terisolasi lulus 8/8 tanpa memasang modul. Fixture patch di ARM64 translation memeriksa restoration terhadap permission aktual sebelum perubahan: requested RX dapat teramati sebagai R pada lingkungan ini. Hasil tersebut tidak membuktikan instruction execution atau permission behavior hardware ARM64 fisik. Hasil final build, eksekusi per ABI dan digest dicatat dalam laporan lokal `.publish/V3_MIGRATION_REPORT_20261008.md`. Pada tahap migrasi awal tersebut, remote CI, instalasi modul aktif dan efek target game belum diuji. Pengujian lanjutan dicatat terpisah di bawah. CPU/RAM/latency, ARM64 fisik dan runtime perangkat 16 KiB masih belum dikualifikasi. Bukti RC3/RC2 lama berada di dokumen historis dan tidak dipakai sebagai hasil eksekusi RC1. [Prosedur build dan CI/CD](CI_CD.md) serta [release notes RC1](RELEASE_v6.2.0_rc1.md) menjelaskan acceptance yang tersedia.

Pengujian lanjutan 18 kontrol dicatat di [laporan runtime lokal](RUNTIME_REPORT_20261008.md). Perbaikan pulse membangun ulang engine/helper/DEX bersama dan mengeksekusi dua fixture pada LDPlayer (x86_64 serta ARM64 melalui Houdini); source/fixture acceptance tetap terpisah dari acceptance gameplay.

## Pembaruan verifikasi runtime 8 Oktober 2026

Paket lokal yang diuji pada 8 Oktober 2026 adalah `ae0dcb92d5147133d69e3351dbc5032999dbb15f831814052b4e355ca0684041`. Build terakhir mengompilasi 12 fixture per ABI, termasuk `restore`. Angka 11 fixture/22 eksekusi di atas adalah riwayat migrasi awal. Build terakhir lulus 52 Python checks; fixture restoration 20 kasus dieksekusi pada x86_64 dan ARM64 translation.

Ledger `wsm_restore.h` memakai pinned GC handle sebelum perubahan dan verifikasi identitas sebelum membaca, menghapus atau melepaskan opsi. Hilangnya baseline, handle tidak valid dan pelepasan tidak pasti menjadi kondisi sticky; PANIC tidak melaporkan selesai bila masih ada ketidakpastian. Uji live satu pemain mengonfirmasi overlap opsi, ON/OFF dan PANIC berulang dengan residual nol. Ini tidak membuktikan semua kegagalan GC/scene pada game.

Ekspor runtime C++ statis disembunyikan pada kedua backend; gate memeriksa ekspor GLOBAL dan WEAK yang didefinisikan. Perbaikan ini belum terbukti menyelesaikan SIGKILL startup. Paket terbaru mencapai gameplay pada pembukaan kedua.

Semua 18 kontrol menerima ON/OFF, tetapi efek pulse belum terbukti meskipun callback melaporkan target nonzero. `observed:null` masih menunjukkan tidak adanya pembacaan HP musuh sebelum/sesudah. Percobaan perbaikan selanjutnya terhenti oleh pemeriksaan otomatis worker; edit parsial tidak masuk paket. Seluruh kontrol sudah OFF dan game dikembalikan ke lobby. Detail ada dalam [laporan runtime lokal](RUNTIME_REPORT_20261008.md).
