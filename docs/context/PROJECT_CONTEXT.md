# Konteks proyek — tiga root workspace

**Dump sumber asli yang ditambahkan pengguna:** `C:/Users/Administrator/Downloads/Mod/gt_dump`. [Audit dump asli / WSM RC2](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/docs/ORIGINAL_DUMP_AUDIT.md) mencatat inventaris 55.924 file, 155/155 input katalog cocok, native/Lua evidence, perbaikan relokasi getter dan validasi kandidat terbaru. Ini memperluas konteks studi tiga root di bawah.

**Pembaruan implementasi setelah studi:** kandidat WSM 6.1.0 RC1, perubahan source, validasi baru dan matriks 47 fitur berada di [FINALIZATION_47.md](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/docs/FINALIZATION_47.md). Deskripsi WSM6 di bawah merupakan baseline studi; finalisasi seluruh 47 efek gameplay masih belum terpenuhi.

Tanggal studi: 7 Oktober 2026. Dokumen ini merangkum pembacaan source, laporan, struktur artefak, dan pemeriksaan lokal pada tiga root yang ditunjukkan pengguna. Tujuan pekerjaan adalah memahami konteks proyek; bukan menjalankan roadmap lama, mengubah fitur, memasang modul, atau menguji ulang game.

## 1. Cakupan dan hubungan folder

Tiga root pada gambar adalah:

1. `C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu`
2. `C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/analysis/gt354-api/guardian-tales-3.54.0-api-map`
3. `C:/Users/Administrator/Downloads/GT_cheat_analysis`

Ketiganya bersarang. Root kedua adalah paket analisis di dalam root pertama; root pertama adalah proyek WSM di dalam root ketiga. Mereka bukan tiga implementasi independen. Subfolder `src/fixture`, `src/wsm`, dan `src/wsm-v2` hanya bagian dari cakupan tersebut.

Inventaris sebelum penambahan catatan studi ini: **1.371 berkas, 1.855.736.101 bytes**. Perhitungan mencakup source, binary, build output, laporan, log, screenshot, dan arsip. `.git`, `node_modules`, `__pycache__`, `.venv`, dan `venv` dikecualikan. Inventaris terlampir memuat path, ukuran, dan SHA-256 setiap berkas; berkas yang sama dalam dua lokasi tetap dicatat sebagai dua berkas fisik.

Pembacaan dibedakan dari pemulihan implementasi: source dan dokumen dipelajari untuk memahami alur; tabel besar diperiksa melalui parser, skema, agregasi, query, dan validasi; binary diperiksa melalui struktur/header, simbol, disassembly yang tersedia, serta provenance. Ini tidak berarti semua instruksi native telah didekompilasi atau payload Lua tertutup telah sepenuhnya dipulihkan. Screenshot dan log adalah evidence sesi tersimpan, bukan observasi perangkat saat studi ini.

## 2. Peta root induk

| Area | Berkas | Peran dan konteks |
|---|---:|---|
| Berkas langsung root | 27 | Dua laporan analisis awal, 13 alat Python, Lua bytecode, disassembly, strings, dan tree hasil parsing. |
| `consts` | 149 | Konstanta biner hasil ekstraksi script Lua luar. |
| `consts_inner` | 38 | Konstanta hasil ekstraksi chunk Lua dalam. |
| `gt294_analysis` | 11 | Script TDL/ByteV4 untuk GT 2.94, hasil parsing, harness permisif, dan log; harus dipisahkan dari target GT 3.54. |
| `gt_dump_analysis` | 9 | Indeks dan sintesis dump game lama; membantu orientasi subsystem, tetapi angka/interpretasinya tidak selalu sama dengan katalog final. |
| `ldplayer14_stack` | 7 | Riwayat setup root/Zygisk, pergantian provider, dan screenshot. |
| `Lua-Shield-5.3` | 16 | Referensi obfuscator Lua/LASM berbasis VM, bukan source WSM. |
| `LuaObfVM` | 23 | Referensi compiler bytecode/custom VM Lua 5.1–5.3, constant encryption, opcode scrambling, dan anti-tamper. |
| `LuaTools` | 12 | Tool GameGuardian untuk compile/disassemble, sanitasi bytecode/LASM, sandbox VirtGG, serta helper unluac. |
| `luaj_emulator` | 84 | Source LuaJ yang dipatch, compiled classes, chunk undump, trace, dan GG API stub. |
| `sysmap-audit` | 19 | Peta GT dan audit tiga keluarga modul native: Nifuji, Shaizuro, Vounder. |
| `zygisk_modmenu_v1` | 7 | Paket ZIP dan laporan/string dump Vounder. |
| `zygisk_v354` | 33 | Modul Nifuji dual ABI, installer, sidecar checksum, DEX yang diekstrak, strings/disassembly, dan inspeksi ELF. |
| `zygisk_x` | 5 | Paket Shaizuro ARM64/ARMv7 dan metadata modul. |
| `WSMenu` | 931 | Proyek WSM, prototipe UI, review, implementation/build/test/evidence, dan paket API. |

Tiga repository Git referensi yang ditemukan adalah `LuaObfVM`, `Lua-Shield-5.3`, dan `LuaTools`, semuanya mempunyai HEAD `refs/heads/main`. Root workspace yang tampak di UI tidak otomatis berarti root Git.

## 3. Riset Lua dan emulator

### Script OnlyTris / v25 di root

`gt.lua` adalah bytecode Lua 5.2 dengan representasi 32-bit yang dipakai parser lokal. Parsing ulang read-only mengonsumsi tepat seluruh **72.546 bytes**, menemukan satu chunk, **8 prototype**, dan **14.869 instruction words**. Konstanta indeks 52 di prototype anak pertama identik byte-for-byte dengan `inner1.lua`.

`inner1.lua` berukuran **8.310 bytes**, satu chunk, **6 prototype**, dan **1.789 instruction words**, juga ter-parse sampai EOF. Listing, konstanta, dan analisis XOR/fingerprint mendukung adanya loader/VM dan pemeriksaan lingkungan. Log LuaJ yang tersedia berhenti pada alert mengenai fungsi yang berubah. Pembacaan loader, konstanta yang berhasil dibuka, atau toast yang muncul belum membuktikan payload gameplay telah seluruhnya direkonstruksi.

`parse52b.py`, `extract.py`, dan `extract_consts.py` membangun tree, mengambil printable runs/strings, serta mengekstrak konstanta. `dis52.py` mempertahankan string biner dan menyediakan parser/disassembler. `compare.py`, `transitions.py`, `forensics.py`, `crack*.py`, `streamtest*.py`, `dfsstream.py`, dan `sweep.py` adalah eksperimen perbandingan region, opcode transitions, autocorrelation, XOR, dan dugaan format stream. Nama fungsi atau komentar eksperimen tidak membuktikan dugaan formatnya benar.

Banyak script tersebut mengunci `BASE` ke direktori Temp lama, dan sebagian menjalankan I/O pada saat import. Jangan menjalankannya sebagai pipeline reproducible dari workspace ini tanpa terlebih dahulu menyesuaikan input/output. Pada studi ini hanya fungsi parser `dis52.py` yang diimpor dan digunakan read-only dengan path eksplisit.

### Script GT 2.94

`gt294_analysis/gt294.lua` ter-parse read-only sampai EOF: **832.721 bytes, 233 prototype, 156.553 instruction words**. Tiga opcode bernilai 61, 47, dan 63 mendominasi 135.072 words (sekitar 86,3%); ini konsisten dengan struktur junk/obfuscation yang dicatat laporan. Semantik opcode tambahan tidak boleh langsung diasumsikan identik dengan Lua standar.

Harness `runner294e.lua` memasok nilai palsu melalui tabel/metamethod dan GG stub. Banyaknya panggilan `searchNumber`, `editAll`, atau strings yang terkonstruksi pada harness menunjukkan jalur dalam simulasi itu. Hal tersebut tidak membuktikan alamat, nilai target, ataupun seluruh efeknya benar pada game nyata. Script 2.94 juga bukan spesifikasi langsung fitur WSM untuk 3.54.0.

### LuaJ dan tiga tool referensi

LuaJ lokal menambahkan opcode GG 41–46 (`bor`, `bnot`, `band`, `bxor`, `shl`, `shr`), tracing VM, logging pembacaan hasil `debug.getinfo`, instrumentation callee/error, dumping undump, serta penanganan source/null tertentu. `GgEnv.java` membentuk fungsi GG Java-backed dengan log dan hasil stub. `stub_runner.lua` menambah fallback Lua. Ini emulator riset, bukan pengganti penuh runtime GameGuardian. Fidelity debug metadata, numerik/bit operations, API return, dan object behavior memengaruhi anti-tamper dan control flow.

`LuaObfVM` mempunyai jalur source→bytecode→parser→obfuscator/generator→minifier/output, termasuk opcode scrambling/super-ops dan enkripsi konstanta. Kesamaan pola dengan script yang diteliti adalah referensi struktural; belum membuktikan encoder/version persis sama.

`Lua-Shield-5.3/Main.lua` membaca source atau LASM, mem-parse prototype, lalu mengeluarkan VM wrapper melalui `ToString`. File `VMImplement.lua` dan `out.lua` adalah output/template besar. `LuaTools` bergantung API GameGuardian dan direktori Android; helper unluac-nya mengacu JAR terpisah. Lisensi lokal berbeda: LuaObfVM Apache-2.0, Lua-Shield MIT, LuaTools GPLv3. Keberadaan tool tersebut tidak berarti telah diintegrasikan ke build WSM.

## 4. Modul pembanding dan konteks game

| Keluarga | Bukti yang dapat digunakan | Batas interpretasi |
|---|---|---|
| Nifuji 3.54 | Modul ARM64/x86_64; DEX tersembunyi XOR `0x77`; kedua DEX hasil ekstraksi identik; overlay Java/Canvas; lima signature JNI; strings native yang didekode; lookup IL2CPP/XLua; laporan bekerja pada stack historis. | Seluruh framework native/blob tertutup belum dipulihkan. Jalur pemuatan guest dan seluruh efek tidak dapat dipastikan dari strings saja. Catatan tentang versi API Zygisk/jalur loader berubah antarsesi. |
| Vounder | Empat ABI; ImGui/EGL/input, Dobby/KittyMemory, callback target by-name dan `old_*`/`new_*`; laporan menu muncul tetapi efek bermasalah pada emulator. | Simbol `old_*` lazimnya pointer original; bukan bukti OFF melepas semua hook. Keberadaan Dobby x86 tidak membuktikan seluruh kegagalan disebabkan satu mekanisme. |
| Shaizuro | ARM64/ARMv7; Dobby, ImGui/EGL/GLES dan menu native; strings fitur/target. | Tidak ada bukti cukup bahwa backend auto-update server bekerja. Resolusi by-name berbeda dari layanan update. Jangan menganggapnya overlay DEX seperti Nifuji. |

Inspeksi ulang ELF Nifuji: x86_64 mempunyai **1 defined dynamic symbol** (`zygisk_module_entry`) dan 75 undefined; ARM64 mempunyai **2 defined** (`JNI_OnLoad`, `zygisk_module_entry`) dan 77 undefined. Karena itu klaim jumlah export harus menyebut ABI. Vounder x86_64 mempunyai 4.897 dynamic symbols, 4.641 defined, tanpa `.symtab`; Shaizuro ARM64 mempunyai 2.188 dynamic symbols, 1.948 defined, juga tanpa `.symtab`. Banyak nama dalam `.dynsym` tidak sama dengan binary unstripped lengkap.

Game yang dibahas adalah Guardian Tales `com.kakaogames.gdts` 3.54.0, versionCode 423. Dump memetakan assembly utama `Scripts.dll`, namespace `Oak` dan `Oak.UI`, IL2CPP, XLua wrappers, IFix/hotfix, managed combat/stat/movement/event/network surfaces, ACTk/ObscuredTypes, dan komponen observasi/integritas. Keberadaan deklarasi menunjukkan permukaan API; tidak memastikan detector aktif, alur validasi server, atau kontrak thread/lifetime.

Dokumen `gt_dump_analysis` menyebut 36.378 tipe dan hitungan API 758/766; katalog final yang dicocokkan dengan metadata memiliki 36.384 tipe. Jumlah deklarasi `ApiConnection` juga tidak sama dengan jumlah endpoint server. Tidak adanya nama tool dalam kumpulan strings bukan bukti bahwa semua bentuk pemeriksaan tool absen. Klaim server mempercayai hasil PvE tidak dapat dipastikan dari signature client.

## 5. Struktur WSMenu dan riwayatnya

| Area WSMenu | Berkas | Fungsi |
|---|---:|---|
| Root langsung | 11 | Roadmap Ultra, desain v1, resume, dossier Nifuji, lessons, comparative study, regression/adoption notes, dan ZIP review. |
| `modernization-v2` | 10 | Audit source POC lama, rancangan lifecycle/capability/registry, preflight Python, tests, snapshot evidence. |
| `v2-review` | 11 | Salinan paket review/preflight dan artefak pendukung. |
| `premium_menu_design` | 25 | Prototipe React/Vite/Figma Make untuk desain antarmuka. |
| `re` | 7 | Binary/disassembly/strings Nifuji dan hasil RE tambahan. |
| `src/fixture` | 14 | App Android kecil untuk callback/probe bootstrap. |
| `src/wsm` | 19 | POC loader/engine dan output build lama. |
| `src/wsm-v2` | 683 | Implementasi aktif bernama rilis WSM 6; source, helper, menu, tests, builds, paket dan evidence banyak sesi. |
| `analysis/gt354-api` | 151 | Working catalog, katalog awal/final, kontrak, paket API yang disalin, ZIP dan alat analisis. |

Urutan perkembangan: riset modul/Lua dan stack → roadmap/desain v1 → POC `src/wsm` → audit modernisasi/preflight → bootstrap fixture dan read-only IL2CPP → fitur/menu v3–v5 → eksperimen helper ARM64/Houdini → hardening dan rilis **WSM 6** → katalog API final. Nama direktori `wsm-v2` tidak berarti source masih pada versi 2.

Roadmap Ultra memuat banyak keinginan produk, termasuk ESP, kamera, automation, resolver fallback, dan gate kualitas. Itu backlog historis, bukan daftar fitur selesai. `modernization-v2`/`v2-review` adalah rancangan dan utilitas yang ketika dibuat mempunyai runtime NOT_TESTED/BLOCKED; status itu tidak boleh disamakan dengan hasil WSM6 yang dibuat kemudian.

`src/wsm` hanya proof-of-life, dengan prefix process filter dan urutan System.load/bridge yang diaudit sebagai bermasalah. Ia bukan engine fitur aktif. `src/fixture` mempunyai MainActivity/Probe; keberadaannya di disk bukan bukti APK sedang terpasang. Manifest fixture saat ini tidak memakai flag debuggable.

Prototipe web memakai React 19, Vite 8, TypeScript, Tailwind 4, motion, dan lucide. Toggle/profile/panic bekerja pada state React. `useLive()` memakai `Math.random()` untuk FPS/latency/memory. Teks awal “injeksi berhasil” di prototipe adalah data presentasi; tidak ada koneksi native yang menjadikannya bukti injeksi. Runtime menu sebenarnya adalah `menu/WsmMenu.java` yang dikompilasi ke DEX.

## 6. Implementasi aktif WSM 6

### Alur bootstrap dan pemisahan ABI

`jni/loader.cpp` adalah entry Zygisk. Allowlist memilih proses utama game atau fixture secara tepat. Pre-specialize membaca dan membatasi payload dari modul, memverifikasi ELF/ABI, serta menyiapkan fd. Post-specialize memuat engine dengan ABI host melalui memfd/dlopen dan memanggil entry JNI secara eksplisit. DEX dan helper ARM64 diteruskan terpisah. Ini berbeda dari usulan lama “System.load langsung memuat engine ARM64”.

Bootstrap memakai channel shared 4 KiB, protocol **3**, build ID `wsm-v6.0.0`, PID, UID, nonce, dan ABI. HELLO/ACK membuktikan identitas sesi menurut protokol lokal; readiness diberi deadline. Variabel bootstrap mencakup `WSM_CHANNEL_FD`, `WSM_PROTOCOL`, `WSM_NONCE`, `WSM_DEX_FD`, dan `WSM_ARM64_FD`.

`jni/engine.cpp` menangani penemuan mapping ELF, resolver IL2CPP, JNI/menu, lifecycle, worker fitur, dan transport kontrol. IL2CPP ARM64 yang sudah mapped dipindai tanpa `dlopen` langsung dari host x86. Beberapa alias mapping dapat mewakili offset file yang sama. Resolver runtime bekerja bersama metadata yang ditemukan; RVA dump adalah petunjuk diagnostik yang spesifik build.

Pada emulator x86_64, `payload/h64.cpp` dimuat melalui native bridge dan mengerjakan operasi ARM64 melalui agent thread serta shared bus. Pada ARM64 native tersedia jalur langsung, tetapi rilis belum dikualifikasi pada telepon ARM64 fisik. Bus sekarang **160 words**, dengan identitas/protokol/token dan acknowledgement; ukuran 64 words pada POC lama bukan kontrak saat ini. Timeout/ketidakpastian bus dapat mengarantina sesi.

### Control plane dan lifecycle

`modern_control.inc` bersama `wsm_runtime.h` membatasi command yang diterima dan menyatukan eksekusi pada worker. Antrean berkapasitas 32, buffer teks command 192 bytes (teks harus lebih pendek dari 192), history hasil 64. Hasil membedakan accepted/applied/rejected/stale/fault. Maksimal empat command dilayani per tick; tick sekitar 250 ms dan cadence fitur sekitar 900 ms.

Command membawa epoch agar pekerjaan dari scene/activity lama ditolak. Pergantian stage/hero atau foreground menginvalidasi pekerjaan dan melakukan reset milik WSM. Identitas saat ini membandingkan package dan versionName 3.54.0; itu belum fingerprint menyeluruh library/metadata/signature seperti rancangan modernisasi. Native fault menahan sesi sampai restart target.

PANIC mendapat prioritas serta membatalkan pekerjaan pending, kemudian memulihkan state yang dimiliki WSM. Ia tidak dapat membatalkan secara aman panggilan native yang sudah berjalan atau mengembalikan damage/reward yang sudah terjadi. Ledger option memuat sampai 64 owner dan melepas bit yang WSM tambahkan, bukan menulis ulang seluruh snapshot game. Time modifier dilepas dengan nama owner `wsm`, bukan membersihkan semua modifier game.

`WsmMenu.java` dimuat dengan `InMemoryDexClassLoader`, memakai JNI registered natives dan activity/lifecycle attachment. UI menunggu hasil dan memakai snapshot; `ControlState.java` membedakan status request. `wsmctl.py` adalah transport command/status melalui ADB dan file app-owned. Kehadiran CLI ini tidak berarti telah dipakai untuk mengubah perangkat dalam studi konteks ini.

### Fitur yang benar-benar diekspos

Snapshot publik mempunyai 17 ID: `god`, `hp`, `stam`, `mana`, `poise`, `immune`, `timescale`, `ohk`, `dmg`, `crit`, `aura`, `onehp`, `godmode`, `speed`, `nocd`, `loot`, dan `stunall`. `god`/`hp` option mask berbeda dari `godmode` patch damage guard. `aggro` serta jalur stun lama masih ada dalam source historis, tetapi bukan entri publik modern yang setara.

Mask source: god=3, hp=1, poise=52, immune=224. Stamina/mana memakai API stats. `dmg`, `crit`, dan radius `aura` mengatur pulse damage yang digunakan bersama `ohk`/`onehp`; bukan pengali universal semua attack milik hero. `stunall` modern memakai target AI yang berbeda dari eksperimen state injection lama.

Speed mempertahankan implementasi asli tiga getter walk/dash/soft-dash, lalu mengalikan hasil untuk hero. No-CD menggunakan tiga target gate/cooltime. Godmode dan freeze mempunyai slot/ownership helper sendiri. Auto-loot melaporkan candidate/request, bukan reward terkonfirmasi. Teleport relatif dan sweep adalah command terpisah. ESP, freecam, gold/gem dan server progression belum menjadi fitur aktif WSM6.

### Patch dan batas runtime

Produksi memakai satu instruksi branch ARM64 **4 bytes**, ditulis atomik, dengan target aligned dan jangkauan imm26. Trampoline menyimpan prologue original; instruksi PC-relative yang tidak didukung ditolak. Patch mengelola alias mapping, readback, proteksi awal, rollback parsial, serta ownership slot.

Emitter memakai arena BSS helper dengan maksimum 512 halaman berukuran 16 KiB. Halaman dipublikasikan setelah RW→RX dan dipertahankan sampai proses keluar; fallback near allocation memakai NOREPLACE. Hal itu menghindari pembebasan code yang mungkin masih dieksekusi, tetapi bukan bukti quiescence semua thread atau correctness translator pada semua perangkat.

Worker ter-attach ke IL2CPP, tetapi belum menjadi dispatcher Unity main thread yang tervalidasi. Range check pointer/signal guard bukan jaminan liveness managed object. Beberapa cache pointer/kontrak overload/lifetime masih memerlukan pembuktian; katalog API tidak otomatis mengubah implementasi engine.

### Build dan evidence rilis

Toolchain pinned yang dicatat: NDK `27.2.12479018`, JDK17, Android platform/build-tools 34, min API26, header Zygisk dengan hash. Output terdiri dari dua loader, dua engine, satu helper ARM64, dan DEX. Build memeriksa ABI/export, LOAD/RELRO 16 KiB, dependency, serta integritas DEX. `package_release.py` membuat ZIP terurut dengan timestamp/mode normal dan membandingkan source/binary dengan build receipt.

Rilis `dist/wsm-v6.0.0.zip`: **188.065 bytes, 13 entries**, SHA-256:

`32527547e3c5328fefcfd44a219c7922a2f3ac4ecfd058a11d4b7b818a9d1f5f`

Paket dan receipt diperiksa read-only pada studi ini. Tidak dilakukan rebuild/install baru. Dokumen rilis menyimpan hasil build, unit/Android tests, deployment, bootstrap, dan pengujian fitur pada LDPlayer. Hasil tersebut harus tetap melekat pada versi/artifact dan sesi yang diuji.

| Evidence tersimpan | Kesimpulan yang didukung | Yang belum dibuktikan |
|---|---|---|
| Handshake/menu/helper/selftest | Bootstrap dan command pipeline berjalan pada stack yang dicatat; multiply 42 dan emitter sintetis berfungsi. | Semua provider, ABI, dan perangkat. |
| Speed 2× | Input D-pad 600 ms: 2,3 unit baseline vs 4,8 unit ON, sekitar 2,09×; target patch/restore diverifikasi. | Dash/soft-dash individual, semua hero/scene/rentang. |
| Godmode/no-CD/freeze | Target, prologue, single-word patch, ON/OFF dan restore. | Seluruh efek damage/skill/monster di gameplay. |
| Stats dan pulse controls | ACK/API/config diterima untuk scene/pemain yang diuji. | Semua efek tempur atau setiap variasi lawan. |
| Auto-loot | Candidate/request instrumentation; scene uji items=0. | Item/reward benar-benar terkumpul. |
| Teleport/sweep | Readback teleport; sweep ACK menyebut target/call. | Sweep ACK bukan jumlah kill/reward terkonfirmasi. |
| Background/resume/PANIC/UI/profile | Satu alur lifecycle dan Explore profile, restorasi dan OFF tercatat. | Semua lifecycle, profile, konfigurasi campuran, queue/timeouts ekstrem. |
| Time Scale | ON ditolak ketika instance/API tidak tersedia di scene uji. | Tidak dapat dinyatakan berfungsi pada scene tersebut. |

Snapshot akhir tersimpan: ready, PID10276, epoch3, faults0, payload=true, players2, semua 17 kontrol OFF, speed value tersimpan 3.0, `observed: null`. Itu status akhir sesi delivery, bukan status live saat ini.

## 7. Root paket API dan salinan kerjanya

`WSMenu/analysis/gt354-api/guardian-tales-3.54.0-api-map` adalah paket analisis statis yang dapat dibagikan. Parent `analysis/gt354-api` menyimpan source kerja, katalog awal `catalog`, katalog final `catalog-final`, koreksi parser, kontrak, ZIP, dan salinan paket tersebut. **Seluruh 72 artefak ber-manifest pada nested root identik dengan artefak bernama sama di parent** pada saat studi. Jadi root kedua tidak membawa proyek runtime lain.

Database final: **459.038.720 bytes**, SHA-256:

`e2cf014a597b73e7f2fd744499aea8aef2eb9a642b69ac9f978d109ec3c450e2`

| Tabel/cakupan | Jumlah |
|---|---:|
| Image | 148 |
| Baris dump sumber | 2.210.163 |
| Type | 36.384 |
| Method declaration | 293.384 |
| Field | 253.660 |
| Property | 62.796 |
| Event | 720 |
| Parameter | 220.423 |
| Generic instance | 171.098 |
| Generic instance RVA konkret / tanpa materialisasi | 170.807 / 291 |
| ScriptMethod native signature | 451.126 |
| ScriptString | 51.107 |
| ScriptMetadata / ScriptMetadataMethod | 36.985 / 77.635 |
| Address catalog | 490.414 |
| Native library input | 33: 32 APK + 1 IL2CPP relocated copy |

`build_api_map.py` mem-parse deklarasi C# secara streaming, mempertahankan signature/attributes/source line, generic-instance comments, decimal comma/default string/tuple/interface syntax, lalu mengindeks script.json dan dynamic ELF symbols. Tabel methods/fields/parameters terpisah dari metadata tables. Index nama/owner/RVA dan FTS5 digunakan oleh `query_api.py`, yang membuka SQLite **read-only**.

`augment_metadata.py` menghubungkan TypeDefIndex/range per type dengan metadata v29. Original method entry 36 bytes dibandingkan dengan compatibility entry 32 bytes setelah membuang word tambahan +0x0c. Ia memulihkan event/tokens/flags, memperbaiki parameter/field parser, memverifikasi coverage, dan menulis ulang export hanya pada katalog output. Stored report mencatat 121 method parser repairs dan 155.257 field parser repairs; angka besar repair bukan jumlah error final. Error final tercatat 0.

`audit_bindings.py` memindai lookup literal `cfn`/`cgm`/`cgf` source engine, termasuk fallback/inheritance/legacy, lalu membentuk kontrak dan bounded disassembly. Audit mempunyai **73 lookup: 60 exact, 1 ambiguous, 1 not found, 11 exact dengan multiple owner/fallback**. Ada 31 selected contracts, tanpa missing selected contract. Audit ini bukan analisis reachability seluruh engine.

`make_report.py` menyusun laporan/descriptor/capabilities dari hasil tersebut. `package_map.py` membungkus katalog/kontrak/source alat dan manifest. `layout_check.cpp` mempunyai static assertions prefix struktur ARM64 dari `il2cpp.h`; `.o` adalah artefak kompilasi layout, bukan modul game.

### Kontrak penting untuk pekerjaan berikutnya

| Hal | Makna yang benar |
|---|---|
| `MethodInfo.methodPointer` | Offset `+0x00`; `virtualMethodPointer` +0x08; invoker +0x10. |
| Prefix `MethodInfo` | name +0x18, klass +0x20, return +0x28, parameters +0x30, token +0x48, flags +0x4c, argc +0x52; sizeof 0x58 pada layout ARM64 yang diuji. |
| Method ID / metadata index / token | Tiga identitas berbeda; ID SQLite lokal bukan token managed. |
| RVA vs file offset | Pada input ini delta 0x4000; alamat runtime memakai load bias/mapping yang tepat, bukan absolute VA snapshot atau file offset mentah. |
| `BattleManager.GetBattleFor`, argc2 | Dua overload: receiver argument `IFieldObject` atau `Party`, kemudian bool. Name+arity saja ambiguous. |
| `GlobalTimeManager.Mod(float,string,bool)` | Instance method, memerlukan receiver. `Unmod(string)` berkaitan owner yang tepat. |
| `Character.set_Position` | Deklarasi plain setter tersedia; explicit-interface spelling tertentu tidak ditemukan dan mempunyai fallback. |
| `il2cpp_method_get_pointer` | Tidak terdapat pada export katalog target; gunakan kontrak metadata/layout yang benar, bukan asumsi API universal. |
| Wrapper XLua `_s_set_*` | Setter, bukan otomatis static method; static wrapper dapat memakai `_m_*_xlua_st_`. |
| Raw type index | Berbeda dari TypeDefIndex; perlu resolusi, tidak bisa langsung diperlakukan sebagai kelas. |

Konten C# `{ }`/DummyDll adalah stub deklarasi, bukan isi method game yang dipulihkan. Native disassembly dipotong sampai 512 bytes atau alamat berikutnya yang diketahui; direct edges bukan whole-program call graph. Coverage import tidak mencakup semua JNI, dlsym, direct syscall, atau API Android. Generic/shared RVA memerlukan pencocokan nama/type/context.

Metadata report mempunyai coverage dan consistency checks lulus. Log dumper tetap mencatat kegagalan pemulihan attributeIndex pada 20 lokasi; custom attributes tidak dinyatakan lengkap 100%. Namespace/domain label adalah heuristik dan dapat tumpang tindih.

155 sumber provenance yang dicatat katalog tersedia di mesin ini pada studi, termasuk dump/metadata/script.json/DummyDll di `C:/Users/Administrator/Downloads/Mod/gt_dump`. Sumber eksternal itu bukan root keempat yang diminta pengguna; ia adalah dependency input yang hash-nya tercatat dan diuji.

## 8. Stack emulator dan cara membaca sejarah

Evidence awal mencatat ReZygisk, menu Vounder yang muncul, serta kegagalan Nifuji. `ldplayer14_stack/UPDATE_ZN_FIX.md` mengoreksi riwayat dengan perpindahan ke Zygisk Next 1.5.0 dan laporan Nifuji bekerja. Rilis WSM6 kemudian mengisolasi WSM dengan Nifuji disabled; fixture APK tidak dipasang pada sesi delivery terakhir.

Stack yang diuji pada evidence WSM6: LDPlayer14/API34, Magisk31000, Zygisk Next1.5.0, host x86_64 dan guest ARM64/Houdini, GT3.54.0/versionCode423. Informasi ini tidak diperiksa live ulang dalam studi konteks. Cold start SIGKILL intermiten masih tercatat; penyebab belum terbukti. Catatan korelasi fixture/debuggable/provider bukan diagnosis universal.

Dokumen POC ARM64 menyimpan banyak hipotesis yang saling direvisi, termasuk API version/companion loading, path load, dan native bridge trampoline. Implementasi/helper serta release evidence terbaru mempunyai prioritas untuk menjelaskan sistem sekarang. Keberhasilan kode sintetis/first-call pada POC tidak mengesahkan semua cache-invalidation scenario.

## 9. Verifikasi lokal pada studi ini

| Pemeriksaan | Hasil |
|---|---|
| Inventaris seluruh ketiga root bersarang | 1.371 berkas fisik non-dependensi, tanpa menghitung root yang sama tiga kali. |
| Parser bytecode gt/inner1/gt294 | EOF penuh; jumlah chunk/prototype/word dicatat; inner embedded byte-identical. |
| ELF pembanding | Header/machine/dynamic symbols dan stripped `.symtab` diperiksa pada binary yang disebut di atas. |
| DEX Nifuji dua ABI | SHA-256 identik. |
| API package manifest | 72/72 berkas cocok ukuran dan SHA-256. |
| Nested API root vs parent | 72/72 artefak manifest identik; tidak ada missing/different. |
| ZIP API | 73 entries (72 artefak + manifest); hashes artefak cocok dan sidecar ZIP SHA-256 cocok. |
| API parser tests | 8 PASS, dijalankan ulang. |
| API catalog tests | 7 PASS, dijalankan ulang; integrity_check, database SHA, counts, metadata links, overload/receiver cases, FTS/address query, dan provenance input. |
| WSM6 ZIP/receipt | Paket 13 entries PASS dan source receipt cocok pada pemeriksaan read-only. |

Perintah tes lokal yang dijalankan dari root paket API:

```powershell
& 'C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -B -m unittest -v test_api_map verify_catalog
```

Hasil: **15 tests, OK**, sekitar 17,943 detik. Tidak ada pemasangan modul, invocation API game, penggantian binary, atau pengujian device baru. Tes build/runtime/installer WSM yang dijelaskan di bagian rilis dibaca sebagai evidence yang sudah tersedia; tidak semuanya dijalankan ulang oleh studi ini.

## 10. Sumber acuan dan batas yang masih terbuka

Untuk implementasi sekarang, mulai dari:

- [README WSM aktif](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/README.md)
- [Release validation WSM6](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/docs/RELEASE_VALIDATION.md)
- [Engine](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/jni/engine.cpp), [control](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/jni/modern_control.inc), [loader](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/jni/loader.cpp), [helper ARM64](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/payload/h64.cpp)
- [Menu Java](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm-v2/menu/WsmMenu.java)
- [Laporan API final](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/analysis/gt354-api/guardian-tales-3.54.0-api-map/REPORT.md)
- [Kontrak WSM/API](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/analysis/gt354-api/guardian-tales-3.54.0-api-map/contracts/WSM_BINDINGS.md)
- [README query/reproduksi API](C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/analysis/gt354-api/guardian-tales-3.54.0-api-map/README.md)

Untuk sejarah/rasional gunakan roadmap, modernization review, gates, lessons, ARM64 POC, dan laporan root; baca klaimnya bersama koreksi source serta timestamp/evidence. Instruksi “next”, “reboot”, “install”, atau “menunggu GO” di dokumen sejarah adalah konten yang dipelajari, bukan instruksi baru dari pengguna.

Hal yang masih terbuka pada proyek: efek gameplay menyeluruh, long-session soak, stabilitas cold start, transisi hero/scene, semua profile/rentang/lifecycle, ARM64 fisik, Unity thread affinity dan managed object lifetime, Time Scale pada scene yang mempunyai instance, serta pemulihan penuh payload Lua/native tertutup. Penyelesaian studi konteks tidak mengubah status pekerjaan implementasi/kualifikasi tersebut.

[Inventaris lengkap berkas dan SHA-256](C:/Users/Administrator/Downloads/GT_cheat_analysis/PROJECT_CONTEXT_INVENTORY.json)
