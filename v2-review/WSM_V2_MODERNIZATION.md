# WSM v2 — modernisasi arsitektur berbasis bukti

**Tanggal:** 6 Oktober 2026, Asia/Bangkok / UTC+07:00  
**Status:** rancangan modernisasi + audit source + utilitas preflight yang dijalankan. **Bukan modul game v2 yang sudah bekerja.**  
**Keputusan utama:** pertahankan pemisahan loader/engine/UI, tetapi ganti fallback spekulatif dengan kontrak kemampuan, lifecycle eksplisit, dan gate terukur.

## 1. Ringkasan keputusan

Fondasi v1 yang dipertahankan: pemisahan tanggung jawab, verifikasi sebelum perubahan, satu eksperimen per langkah, fitur yang tidak lolos tidak dirilis, dan batas terhadap state server. Yang diubah: klaim absolut, penyamaan handle host/guest, rollback seolah transaksi database, polling bebas thread, indeks UI sebagai identitas, serta asumsi bahwa versionName cukup untuk mengenali binary.

Modernisasi tidak berarti memasang lebih banyak hook atau framework. Sistem yang lebih cerdas adalah sistem yang:
- dapat menjelaskan mengapa suatu kemampuan tersedia atau tidak;
- tidak mengubah sesuatu saat identitas target, ownership, atau lifecycle meragukan;
- memisahkan keinginan operator dari efek yang benar-benar teramati;
- tidak mencoba mekanisme yang lebih agresif ketika mekanisme sebelumnya gagal;
- menyimpan bukti sehingga kegagalan tidak diulangi dengan tebakan yang sama.

Tidak ada klaim tanpa jejak, bebas deteksi, bebas crash, ataupun jaminan keberhasilan semua fitur. Batas multiplier bukan bukti keamanan atau kompatibilitas. Dokumen ini tidak menambahkan teknik penyembunyian atau penonaktifan anti-cheat.

### Apa yang benar-benar dikerjakan

- Membaca seluruh lampiran, source loader/engine, konfigurasi build, installer, dan catatan proyek lama.
- Memeriksa rujukan primer Android, AOSP, Unity, Linux, Arm, dan Zygisk.
- Membuat `tools/wsm_preflight.py` untuk inventaris host, fingerprint source, pemeriksaan header ELF64/PT_LOAD, dan daftar perangkat ADB.
- Menjalankan 36 tes utilitas; satu tes melakukan 1.000 mutasi fixture deterministik. Ini bukti ketahanan parser terhadap sampel tersebut, bukan bukti keamanan menyeluruh.
- Menjalankan preflight pada proyek asli: `BLOCKED`, `runtime_status=NOT_TESTED`.
- Tidak mengedit `src/wsm`, memasang ZIP, menyalakan emulator, mengubah proteksi host, atau menulis ke proses game.

### Yang belum ada

Engine fitur, adapter IL2CPP v2, UI v2, session gate runtime, pengujian bridge di LDPlayer, dan bukti fitur gameplay belum diimplementasikan atau dijalankan dalam paket ini. Semua spesifikasi komponen berikut berstatus **usulan**, bukan daftar kemampuan yang sudah tersedia.

## 2. Bukti dan tingkat keyakinan

Gunakan empat label pada setiap keputusan:

| Label | Arti | Contoh |
|---|---|---|
| OBSERVED_LOCAL | Dibaca atau dijalankan pada mesin ini | `loader.cpp` memanggil dua jalur berurutan |
| SOURCE_VERIFIED | Didukung dokumentasi/source primer | `JNIEnv` bersifat per-thread [S1] |
| PROPOSED | Pilihan desain yang belum diimplementasikan | command ID stabil dan deadline |
| UNVERIFIED_TARGET | Membutuhkan bukti dari build/perangkat target | vendor bridge menerima library melalui memfd |

Catatan lama bahwa Nifuji bekerja merupakan konteks historis, bukan pengujian ulang pada sesi ini. String hasil RE bukan bukti bahwa suatu fungsi dipanggil, suatu backend dipakai, atau seluruh fitur bekerja. Dump dengan method body kosong hanya memberi metadata, bukan alur eksekusi.

Bukti lokal utama: `evidence/preflight-2026-10-06.json`. Snapshot ini menyimpan timestamp, hash source, stdout alat, dan alasan penghalang. Nama file laporan tidak boleh ditimpa; setiap pengujian menghasilkan snapshot baru.

## 3. Audit source yang ditemukan

Lokasi relatif pada tabel berikut merujuk `../src/wsm/` dari direktori paket modernisasi. Nomor baris mengikuti source yang dibaca sebelum perubahan apa pun.

| ID | Prioritas | Bukti | Temuan dan tindakan usulan |
|---|---|---|---|
| A01 | P0 | `jni/loader.cpp:152–157` | Jalur System.load dan bridge selalu dipanggil berurutan. Tidak ada status sukses yang mengendalikan fallback. Gunakan satu keputusan load dan handshake exactly-once per proses. |
| A02 | P0 | `jni/loader.cpp:225–249` | Handle berasal dari `dlopen` host, lalu diperlakukan sebagai handle bridge. Kontrak AOSP membedakan pemuatan guest dan host [S2,S3]. Hentikan asumsi ini; adapter wajib membuktikan provenance handle. |
| A03 | P0 | `jni/loader.cpp:243–249`, `jni/engine.cpp:35–39` | `shorty="vJJ"` memakai `v` kecil yang bukan kode shorty standar; void harus `V` [S13]. Jika maksudnya void, itu pun tidak cocok dengan pointer fungsi ber-return `long`. Fungsi C juga belum membuktikan kontrak JNI/bridge. Mengganti satu huruf tidak cukup: audit seluruh calling convention. |
| A04 | P0 | `jni/engine.cpp:23–39` | Implementasi ARM64 hanya mencetak proof-of-life. Tidak ada resolver, registry, feature engine, UI, atau panic. Jangan menyebutnya engine fitur yang selesai. |
| A05 | P1 | `jni/loader.cpp:99` | Prefix matching dapat memilih nama yang bukan proses utama dan semua proses tambahan. Gunakan allowlist proses eksplisit; package/process/UID harus konsisten. |
| A06 | P1 | `jni/loader.cpp:141–142` | Return `exemptFd` diabaikan tetapi log menyebut selesai. API mengembalikan bool [S4]. Kegagalan harus menutup jalur load, bukan hanya dicatat. |
| A07 | P1 | `jni/loader.cpp:108–143` | Ownership fd modul, memfd, dan mapping tidak terdokumentasi; tidak ada cleanup lengkap semua cabang. Pakai RAII sesuai kontrak provider, bukan menebak semua fd milik caller. |
| A08 | P1 | `jni/loader.cpp:44–78` | Tidak ada batas ukuran payload; EINTR diperlakukan sebagai kegagalan pembacaan. Tambahkan batas, penanganan pembacaan parsial/interupsi, dan hasil bertipe. |
| A09 | P1 | `jni/loader.cpp:129–139` | Hasil munmap tidak diperiksa dan payload belum diverifikasi identitas/arsitekturnya sebelum load. Validasi header, ukuran, dan hash payload. |
| A10 | P1 | `jni/loader.cpp:212–213` | NewStringUTF tidak diperiksa sebelum pemanggilan JNI berikutnya. Periksa null/exception dan batasi lifetime reference [S1]. |
| A11 | P1 | `jni/loader.cpp:202–221,253–267` | Pengelolaan local reference dan exception helper belum disiplin. Terapkan local-frame scope; error pelaporan tidak boleh menimpa penyebab awal [S1]. |
| A12 | P1 | `jni/loader.cpp:218` | Tidak ada exception pada System.load belum membuktikan engine siap. Butuh handshake dengan build ID, PID, protokol, kemampuan, dan nonce sesi. |
| A13 | P1 | `jni/loader.cpp:167–181` | Load native langsung memanggil entry sendiri, bukan jalur inisialisasi yang terbukti identik dengan JNI_OnLoad. Satukan bootstrap internal yang idempotent. |
| A14 | P1 | `jni/engine.cpp:10–18` | Proof file diarahkan ke `/data/local/tmp` dari konteks app; source sendiri mengakui bisa ditolak. Gunakan kanal diagnostik yang memang dimiliki, bukan keberhasilan file ini sebagai prasyarat. |
| A15 | P1 | `scripts/build.ps1:61–84` | Segment alignment, RELRO, dan dynamic dependency diperiksa pada loader, tidak pada engine. Validasi setiap library yang dikirim. |
| A16 | P1 | `scripts/build.ps1:31–33` | Menerima NDK mayor >=27, bukan pin exact revision. Ini repeatable recipe, belum bukti reproducible build. |
| A17 | P1 | `scripts/build.ps1:95–106` | ZIP menggunakan metadata file saat ini tanpa normalisasi atau perbandingan dua clean build. Klaim reproducible belum terbukti. |
| A18 | P1 | `jni/Application.mk:2`, rancangan DEX | min API 23 bertentangan dengan jalur InMemoryDexClassLoader yang mulai API 26; overload array API 27, librarySearchPath API 29 [S10]. |
| A19 | P2 | `jni/loader.cpp:87–88` | Logging onLoad terjadi sebelum filter target. Kurangi observasi proses yang tidak relevan; diagnostik tetap jelas, bukan disamarkan. |
| A20 | P2 | `module-template/customize.sh:15–23`, `Application.mk:1` | Installer mengenali empat ABI, build hanya dua. Error akhirnya ada, tetapi dukungan harus dinyatakan konsisten di manifest dan UI. |

**P0**: menghalangi pembuktian mekanisme inti. **P1**: harus ditutup sebelum runtime qualification/rilis. **P2**: perbaikan kejelasan, efisiensi, dan maintenance. Ini penilaian audit, bukan severity keamanan tersertifikasi.

## 4. Koreksi rancangan v1

### 4.1 Loader dan perbedaan ABI

AOSP menjelaskan bahwa guest library memakai linker tersendiri. Native bridge bukan fitur universal semua Android dan keberadaannya tidak membuktikan kompatibilitas payload tertentu [S2]. System.load dipengaruhi class loader dan namespace [S3]. Maka memfd -> System.load -> JNI_OnLoad tetap **hipotesis per lingkungan**, bukan kebenaran universal.

- Jalur A menjadi adapter eksperimental dengan handshake dan gate sendiri.
- Jalur B tidak aktif otomatis; deklarasi fungsi, versi interface, provenance handle, serta thread context wajib teruji lebih dulu.
- Jalur C tidak disebut fallback penuh. Membaca/menulis byte tidak membuktikan layout objek, sinkronisasi, semantik fungsi, atau invalidasi translator.
- Jika tidak ada adapter yang qualified, hasilnya `UNSUPPORTED`, bukan mencoba mekanisme lain tanpa kontrak.

Tidak menyimpulkan seluruh logika wajib ARM64: logika murni dan UI bisa berada di host. Yang harus tepat ialah ABI setiap panggilan, ownership data, dan konteks eksekusi.

### 4.2 Memfd, lifecycle, dan kompatibilitas

Memfd adalah objek file berbasis memori yang namanya terlihat melalui proc fd; bukan jaminan tanpa jejak atau bypass sandbox [S5]. Kebutuhan retensi fd/mapping harus dibuktikan per adapter, bukan ditutup terlalu cepat ataupun dibiarkan bocor.

`JavaVM` dan `JNIEnv` tidak boleh diperlakukan identik. Gunakan VM untuk memperoleh environment thread; reference lintas callback harus memiliki lifetime yang sesuai [S1]. Callback loader tidak menunggu loop readiness panjang. Worker dan callback memiliki owner serta titik penghentian yang jelas.

### 4.3 Resolver

Tiga lapisan bukan tiga bukti independen jika semuanya memakai asumsi signature yang salah.

- Nama target di dump = kandidat, belum alamat executable tervalidasi.
- `il2cpp_method_get_pointer` pada dokumen lama belum terbukti export dalam binary target. Tidak boleh dijadikan API wajib hanya karena tercantum pada catatan RE.
- Identitas method meliputi assembly, namespace, class, return type, parameter lengkap, static/instance, generic/virtual shape, serta ABI adapter.
- Pattern ambigu menghasilkan `AMBIGUOUS`, bukan memilih hasil pertama.
- Offset profile hanya berlaku untuk identitas build tepat. Tidak ada scan otomatis yang kemudian mempromosikan target menjadi boleh ditulis.
- Cache membawa module generation dan identity; reload/library replacement membatalkannya.

### 4.4 Perubahan memori dan rollback

Pseudocode v1 tidak atomik: verify dan write terpisah, thread lain bisa berjalan, dan write bisa parsial. Setelah proteksi dikembalikan RX, cabang gagal langsung menulis original lagi tanpa memperoleh izin tulis. Restore juga tidak mengelola proteksi. mprotect memiliki persyaratan alignment dan dapat gagal [S6].

Menyimpan byte asli tidak memutar ulang state permainan, event, transaksi jaringan, atau damage yang sudah terjadi. Restorasi byte berbeda dari pemulihan state aplikasi. Tidak ada jaminan rollback setelah segfault, kill, atau state proses sudah tidak sehat.

Stub branch saja bukan implementasi hook lengkap. Calling convention, parameter floating-point, callee-saved register, dan stack harus sesuai ABI [S7]. Relokasi instruksi serta lifetime trampoline tetap masalah terpisah. Paket ini tidak menyediakan patch game atau trampoline operasional.

### 4.5 Timing translator

Memuat engine awal tidak membuktikan target belum pernah dieksekusi. Flush instruction cache CPU juga tidak otomatis membuktikan invalidasi translation cache vendor. Klaim `bebas isu Houdini` dicabut. Qualification backend harus memuat eksperimen terkontrol pada library fixture milik sendiri, sebelum library target.

### 4.6 Unity, proyeksi, dan UI

Mayoritas API Unity tidak thread-safe dan membutuhkan main thread Unity [S8]. Attach JNI tidak membuat thread menjadi main thread Unity. Pengumpulan objek dan state kamera dilakukan pada konteks game yang tervalidasi; worker menerima salinan data, bukan pointer Unity mentah.

WorldToScreenPoint mengembalikan pixel dengan origin kiri-bawah, z berupa jarak dari kamera, dan dapat menghasilkan posisi di luar viewport [S9]. Pipeline harus menangani pemetaan ke overlay, viewport, rotasi, insets, depth, kamera kosong, dan generasi scene.

Float array baru pada tiap frame tidak sama dengan tanpa alokasi. Android menganjurkan menghindari alokasi di onDraw dan invalidation yang tak perlu [S11]. Kunci: publication snapshot yang konsisten dan renderer tanpa panggilan ke objek game.

## 5. Arsitektur v2 yang diusulkan

Tiga boundary deployment dipertahankan; di dalamnya ada komponen berikut. Menambah komponen tidak berarti menambah proses atau thread.

```text
Host tooling (read-only preflight, manifest, build evidence)
                         |
L1 Bootstrap adapter ----+---- Environment/identity probe
                         |     exact match + capability results
                         v
L2 Engine core: state machine + session gate + command dispatcher
   |             |                  |
   |       Runtime adapter          +-- Feature registry
   |       (same ABI / qualified         dependencies, conflicts,
   |        bridge, no guessed calls)    reversibility, evidence
   |             |
   |       Unity-context executor
   |             |
   +------- immutable snapshots + bounded event channel
                         |
L3 Presentation: lifecycle controller + JNI boundary + Canvas renderer
                         |
                  diagnostics/configuration
```

### Komponen dan kontrak

| Komponen | Input -> output | Kegagalan yang harus terlihat |
|---|---|---|
| EnvironmentProbe | package/OS/module facts -> capability report | missing, mismatched, unsupported |
| IdentityVerifier | binary fingerprints + profile -> exact match | stale profile, unreadable artifact |
| BootstrapCoordinator | adapter result -> session handshake | timeout, duplicate init, protocol mismatch |
| RuntimeAdapter | explicit contract -> typed runtime service | missing export, wrong signature, dead generation |
| LifecycleController | app/activity/game events -> generation | detached view, scene transition, shutdown |
| SessionGate | trusted session facts -> grant/revocation | unknown, stale, connected mode |
| CommandDispatcher | typed commands -> acknowledgements | invalid value, queue full, expired, stale epoch |
| FeatureRegistry | stable descriptors -> derived availability | unmet dependency, conflict, unqualified capability |
| SnapshotChannel | bounded POD records -> consistent immutable view | overflow, expired snapshot, epoch mismatch |
| Diagnostics | bounded structured events -> local evidence | dropped-event counter, incomplete report |

Tidak ada command yang memuat alamat arbitrer atau source Lua bebas. Antarmuka berbicara dalam capability/feature ID bertipe, bukan `write(address, bytes)` dari UI.

## 6. State machine dan lifecycle

### State proses

```text
CREATED -> DISCOVERING -> IDENTIFIED -> ADAPTER_READY -> READ_ONLY_READY
                  \            \           \                  |
                   +------------+-----------+--> UNSUPPORTED   |
                                                              v
                                                  ACTIVE (qualified only)
                                                              |
                                                QUIESCING -> DISABLED
                                                   |
                                              RESTART_REQUIRED
```

- `ACTIVE` hanya sesudah grant sesi eksplisit, profile cocok, dan backend terverifikasi.
- Error sebelum ada perubahan berujung DISABLED/UNSUPPORTED dengan game dibiarkan tidak disentuh.
- Error sesudah perubahan mungkin memerlukan RESTART_REQUIRED; jangan menampilkan `restored` jika hanya best-effort.
- Panic menutup penerimaan command, membatalkan pekerjaan tertunda, mengosongkan publikasi, lalu meminta penonaktifan pada owner context.
- Tidak ada janji memaksa menghentikan fungsi native yang sedang berjalan dengan aman.
- Panic bersifat latched: harus ada tindakan re-arm eksplisit. Kembali dari mode lain tidak otomatis mengaktifkan semua fitur.

### Empat jenis generasi

`process_epoch`, `activity_epoch`, `scene_epoch`, `module_epoch` dicatat terpisah. Command membawa generasi relevan. Respon terlambat dari activity atau scene lama ditolak, meskipun alamat memori kebetulan dipakai ulang.

### Urutan teardown yang diusulkan

1. Revoke grant dan tolak command baru.
2. Cancel timer/producer yang dimiliki modul.
3. Tunggu acknowledgement pekerjaan milik modul dengan deadline; jangan menunggu di UI thread.
4. Lepas view/listener pada UI thread.
5. Lepas resource runtime hanya ketika tidak ada callback yang menggunakannya.
6. Jika quiescence tidak terbukti, jangan dlclose code yang mungkin sedang berjalan; tandai restart diperlukan.

Invariant yang diuji: tidak ada callback setelah owner mati, tidak ada view ganda, dan tidak ada restore terhadap objek generasi baru.

## 7. Identitas, kemampuan, dan resolver bertipe

### IdentityKey usulan

- package name dan signing certificate digest bila tersedia;
- versionCode/versionName untuk UX, bukan satu-satunya kunci;
- ABI proses, ABI engine, bitness, API Android, runtime page size;
- hash artifact library dan metadata yang dipakai profile;
- ELF build ID jika ada, tidak diasumsikan selalu ada;
- provider/API loader serta identitas vendor bridge yang dapat diobservasi;
- versi protocol/schema WSM.

Hash artifact bukan hash sembarang rentang memori yang sudah direlokasi. Profil harus menentukan asal setiap fingerprint dan cara membandingkannya. Hash lokal tanpa trust anchor bukan autentikasi paket; rilis memerlukan distribusi/penandatanganan manifest yang tervalidasi.

### ResolveResult usulan

```text
status: FOUND | NOT_FOUND | AMBIGUOUS | BUILD_MISMATCH | ABI_MISMATCH
identity_key
method_signature
module_generation
candidate_count
capabilities_proven
validation_evidence_ids
reason_code
```

`FOUND` tidak sama dengan `WRITABLE` ataupun `BEHAVIOR_VERIFIED`. Target hanya dapat dipakai fitur setelah seluruh persyaratan fitur terpenuhi. Caching harus menyimpan hasil negatif secara terbatas agar tidak memindai tanpa henti.

### Adaptasi yang memang berguna

- Pilih adapter hanya dari daftar yang telah qualified pada tuple lingkungan tersebut.
- Circuit breaker per capability, bukan restart semua subsistem setiap timeout.
- Retry hanya error transient: runtime belum siap, bukan ABI/build mismatch.
- Gunakan deadline monotonic dan cancellation; interval tetap v1 bukan kontrak universal.
- Diagnostics mengelompokkan kegagalan serupa dan menunjukkan satu penyebab utama.
- Penurunan kualitas visual dapat otomatis; peningkatan hak/kemampuan write tidak otomatis.
- Tidak ada LLM yang memutuskan alamat atau patch saat runtime. Analisis offline boleh membantu review, tetapi tidak menggantikan pembuktian.

## 8. Registry fitur dan protocol UI

### FeatureDescriptor usulan

```text
feature_id (string stabil), schema_version, label_key, category
value_type, default_value, tested_min, tested_max, step
required_capabilities, dependencies, conflicts
allowed_session_kinds, activation_context
reversibility: NONE | CONFIG_ONLY | OBJECT_SCOPED | VERIFIED_RESTORABLE
availability, reason_code, evidence_ids
```

ID bukan nomor posisi menu. ID lama tidak boleh dipakai kembali untuk arti baru. Slider tidak aktif jika rentang belum diuji. NaN, infinity, tipe salah, overflow, dan nilai di luar rentang ditolak. Batas usulan SPD 10x/DMG 100x adalah permintaan produk, belum menjadi rentang tersertifikasi.

### Command/ack usulan

```text
Command: protocol, command_id, feature_id, desired_value,
         expected_revision, process_epoch, scene_epoch, deadline
Ack: command_id, accepted/rejected, applied_revision,
     effective_value, outcome, reason_code
```

`desired`, `accepted`, `applied`, dan `observed` ditampilkan berbeda. Toggle tidak boleh langsung terlihat sukses sebelum acknowledgement; ketika hasil belum teramati, tampilkan PENDING, bukan ON.

- Command duplikat idempotent.
- Slider drag dicoalesce, hanya nilai terbaru yang masih relevan.
- Queue bounded; error overload tidak memblokir UI.
- Snapshot config immutable; worker tidak membaca JSON yang sedang ditulis.
- Persistence menyimpan schema + desired settings, bukan pointer, fd, atau grant sesi.
- Startup sesudah crash memulai dengan fitur perubahan keadaan OFF.
- Migrasi konfigurasi eksplisit; versi schema asing tidak diterima secara diam-diam.

UI usulan: overlay kecil draggable/collapsible, tab Status/Fitur/Visual/Diagnostik, bahasa Indonesia dengan label teknis konsisten. Panic harus mudah ditemukan; tiga ketukan boleh menjadi shortcut tambahan, bukan satu-satunya kontrol.

## 9. Visual snapshot pipeline

### Kontrak publication

Producer di konteks Unity tervalidasi -> salinan data sederhana -> publication -> consumer UI. Tidak ada pointer objek engine keluar lewat JNI.

Header snapshot: versi, byte size, record count, frame sequence, timestamp monotonic, process/scene/camera epoch, viewport, flags truncation/staleness. Tiap record memiliki ID generasi, kategori, posisi layar terverifikasi, depth, jarak, dan status validitas.

Pilih implementasi mudah dibuktikan lebih dulu: bounded copy dengan mutex pendek di boundary non-hot, atau ownership slot yang jelas. **Dua buffer tanpa aturan ownership bukan otomatis bebas race.** Seqlock pada payload C++ non-atomic yang dibaca saat ditulis dapat tetap menjadi data race; jangan mengandalkan retry angka sequence sebagai pembenaran.

Aturan renderer yang diusulkan:
- hanya konsumsi snapshot lengkap dengan schema/epoch cocok;
- validasi count/stride/capacity sebelum mengakses record;
- buang NaN, depth tidak valid, atau snapshot kedaluwarsa;
- gunakan transform viewport eksplisit, bukan asumsi tinggi layar penuh;
- cache Paint, layout label, dan aset; jangan resolve runtime di onDraw;
- permintaan gambar mengikuti lifecycle dan frame callback Android;
- hentikan producer visual saat activity background;
- jika overload: buang snapshot lama, turunkan detail/label; jangan menghambat game.

Budget awal adalah **target pengujian**, bukan hasil: queue command 128 entri, cap snapshot 512 marker, snapshot TTL 250 ms, dan visual sampling maksimum 20 Hz. Sesuaikan hanya sesudah profil baseline menunjukkan kebutuhan. Scene change membatalkan snapshot segera, tanpa menunggu TTL.

## 10. Session gate, fail-closed, dan batas pemulihan

Nama scene tunggal tidak cukup menentukan keadaan sesi. Gunakan sinyal eksplisit dari adapter yang tervalidasi; ketidakpastian menghasilkan UNKNOWN dan perubahan keadaan tetap nonaktif.

| Jenis sesi | Keputusan usulan |
|---|---|
| UNKNOWN / TRANSITIONING | Tidak ada perubahan keadaan |
| LOCAL_TEST / OFFLINE_FIXTURE dengan grant | Hanya capability yang qualified |
| CONNECTED_PVE | Default nonaktif sampai kontrak dan dampak benar-benar diketahui |
| ARENA / COOP / RAID / RANKED | Tidak ada perubahan gameplay; tidak auto-resume |

PvE tidak otomatis berarti offline. Mematikan toggle setelah 500 ms tidak membatalkan efek yang sudah terjadi sebelumnya. Karena itu pencabutan grant ditempatkan pada transisi dan sebelum command diterapkan; polling hanya deteksi cadangan.

Watchdog memakai last-progress timestamp per subsistem, bukan satu heartbeat global. Pause/background bukan otomatis hang. Jika thread mati atau proses crash, watchdog in-process tidak dapat dijanjikan menyelamatkan proses; laporkan batasnya.

Restorasi harus membawa ownership token dan generasi. Jika keadaan berubah di luar owner, jangan menulis snapshot lama secara buta. Hasil penonaktifan dapat `DISABLED`, `PARTIAL`, `CONFLICT`, atau `RESTART_REQUIRED`—tidak dipaksa selalu sukses.

## 11. Matriks seluruh fitur dari lampiran

Semua entri masih **UNVERIFIED_TARGET**. Tidak ada fitur yang otomatis disetujui karena masuk tabel.

| Fitur v1 | Prasyarat yang harus dibuktikan | Bukti lulus | Bukti OFF/regresi |
|---|---|---|---|
| Movement Speed | Nilai yang benar mengendalikan gerak, ABI/owner tepat | Lintasan dan durasi konsisten terhadap baseline; animasi/kamera tidak rusak | Nilai asli pulih, scene/respawn tidak mewarisi state lama |
| No Skill Cooldown | Sumber cooldown aktual, bukan hanya tampilan | Perilaku skill dan timer teramati pada fixture lokal | Reset skill, respawn, karakter berganti |
| God Mode | Jalur kerusakan dan efek turunan dipahami | Kasus damage langsung/berkala/lifecycle diuji terpisah | Perilaku damage asli setelah OFF; tidak mengklaim undo damage |
| Damage multiplier | Jalur kalkulasi aktual, bukan parameter UI | Pengukuran input/output dan rounding pada kondisi terkontrol | Baseline pulih; tidak memodifikasi hasil server |
| Visual Star Piece/Purple Coin/Gold Cube | Sumber entitas dan lifetime per kategori | Marker cocok dengan entitas nyata, depth/viewport benar | Tidak ada marker stale setelah collect/despawn |
| Chest/Mimic + jarak | Identitas tipe dan definisi satuan jarak | Tipe tidak tertukar; posisi/jarak benar | Pergantian kamera/scene dan entity reuse |
| Parameter ATK/DEF | Read-only provenance, unit, interval pembaruan | Angka dibandingkan dengan sumber runtime terverifikasi | Missing data ditampilkan unavailable, bukan nol palsu |
| Preset/panic/session-off | State machine dan ack sudah lulus | Transisi, queue cancel, duplikasi command, panic latch | Tidak auto-resume; failure ditampilkan jujur |
| Auto-farm/win opt-in v1.1 | Lifecycle, cancellation, local fixture, bounded steps | Selesai/timeout/cancel dapat direproduksi | Tidak berjalan di sesi terhubung/kompetitif; tidak background diam-diam |

Tidak menambah Teleport/Drone View pada baseline v2: menambah ruang pengujian sebelum fondasi selesai. Bisa dievaluasi sebagai capability terpisah sesudah gate inti. Tidak ada auto-win ranked dalam scope.

Urutan baru: identitas/handshake -> lifecycle/panic/session gate -> telemetry read-only -> UI visual -> satu fitur perubahan keadaan dalam fixture lokal -> regresi -> kandidat target. Movement speed tidak otomatis berisiko paling kecil hanya karena tampak sederhana.

## 12. Build, kompatibilitas, dan release engineering

Usulan modernisasi toolchain:
- Reproduksi baseline dulu dengan NDK lokal tepat `27.2.12479018`.
- Uji migrasi terpisah ke NDK r28+ yang dipin exact revision; dokumentasi Android menyatakan alignment 16 KiB default pada r28+, bukan berarti r28 adalah versi terbaru [S12].
- Periksa setiap ELF: class/endian/machine, PT_LOAD bounds/alignment, RELRO, permission, dynamic dependency, exported/undefined symbols, serta ketepatan entry.
- Periksa runtime page size; jangan hardcode 4096 pada logika memory [S12].
- ZIP modul dan APK app fixture adalah artifact berbeda. Aturan APK zipalign tidak otomatis diterapkan ke ZIP modul yang mengekstrak file.
- Pin header Zygisk dan provider compatibility; jangan mengganti API hanya karena upstream lebih baru [S4].
- Pin compiler, linker, Java/D8, min API, flags, dependency hashes, dan module protocol.
- Native boundary memakai C ABI bertipe fixed-width. C++20 dapat dipilih untuk core baru setelah toolchain baseline lulus; tidak perlu upgrade bahasa seluruh loader hanya demi label modern.
- Tidak berbagi ownership STL, exception, atau allocator lintas DSO/ABI. Java/Kotlin tetap bytecode ART, bukan "kode x86".

Definition of reproducible: dua clean build dengan toolchain/input sama menghasilkan hash library yang sama dan package yang sama setelah metadata ZIP dinormalisasi. Jika gagal, dokumentasikan sumber variasi; jangan menyebut reproducible hanya karena script dapat dijalankan dua kali.

Release bundle yang disyaratkan: source revision, manifest exact versions, hashes, symbol files terpisah, changelog, test report, compatibility matrix, known limitations, serta rollback/uninstall procedure yang diuji. Paket modernisasi ini bukan release module.

## 13. Gate penerimaan dan rencana migrasi

| Gate | Hasil wajib | Status saat paket ditulis |
|---|---|---|
| G0 audit | Rancangan dibaca; temuan source dan bukti tersimpan | PASS untuk review ini |
| G1 preflight host | Toolchain merespons, source teridentifikasi | PASS parsial; binary project belum ada |
| G2 build | Semua artifact target dibangun dan diperiksa | NOT_RUN |
| G3 fixture native | JNI handshake/lifecycle pada app milik sendiri | NOT_RUN |
| G4 fixture bridge | Guest call/return, duplicate init, teardown | NOT_RUN |
| G5 read-only integration | Identitas target tepat, tidak ada write | NOT_RUN; perangkat tidak siap |
| G6 control plane | Registry, config, panic, session transitions | SPECIFIED_NOT_IMPLEMENTED |
| G7 satu fitur | Baseline -> ON -> OFF -> ulang, bukti perilaku | NOT_RUN |
| G8 regression/perf | Semua fitur kandidat, scene changes, lifecycle | NOT_RUN |
| G9 release | Manifest, reproducibility, qualified matrix | BLOCKED |

Setiap gate memiliki tiga kemungkinan: PASS, FAIL, atau NOT_RUN/BLOCKED. NOT_RUN tidak dapat dikonversi menjadi PASS karena gate sebelumnya lulus.

### Fault injection yang wajib pada implementasi berikutnya

- payload hilang/truncated/ABI salah/hash tidak cocok;
- VM/environment/class/method tidak tersedia;
- duplicate init dan respons handshake terlambat;
- no device, unauthorized, timeout, app update;
- registry feature ID asing, schema berubah, dependensi siklik;
- queue penuh, deadline habis, command duplikat;
- NaN/infinity/overflow pada input visual dan slider;
- scene/activity berganti ketika command pending;
- panic saat init/active/teardown; panic berulang;
- runtime berhenti memberi progress; app background tidak salah dianggap hang;
- object reuse/stale reference; dependency hilang;
- hasil penonaktifan parsial dan kebutuhan restart.

### Target performa dan stabilitas

Target awal usulan: p95 frame time naik tidak lebih dari 5% dari baseline yang diambil pada scene/workload sama; bandingkan pula p99, CPU, RSS, dropped frames, dan latency command. Jangan membandingkan sesi berbeda dengan workload tak terkendali. Warm-up dan urutan A/B bergantian harus dicatat.

Soak 60 menit tanpa crash hanyalah satu data point, bukan pembuktian bebas crash. Sertakan cold starts, background/resume, pergantian orientasi bila didukung, scene churn, dan leak trend. Sanitizer serta pengujian concurrency core dijalankan pada fixture, bukan disamakan dengan keseluruhan perilaku vendor translator.

### Langkah implementasi setelah review

1. Tutup cacat bootstrap P0 dalam branch terpisah; jangan menumpuk fitur pada source POC.
2. Bangun app fixture minimal milik sendiri dan payload identitas tanpa memodifikasi game.
3. Jalankan kontrak JNI/ABI/lifecycle pada native ABI dahulu, kemudian bridge yang qualified.
4. Implementasikan registry, dispatcher, snapshot channel, config, dan session gate dengan tests.
5. Integrasi read-only target hanya setelah perangkat siap dan fingerprint terambil.
6. Evaluasi fitur satu per satu berdasarkan matriks di atas. Jika target tidak memenuhi kontrak, tandai unavailable.

## 14. Pengoperasian paket yang benar-benar tersedia

Dari folder `modernization-v2`, jalankan:

```bash
python -m unittest discover -s tests -v
python tools/wsm_preflight.py --project "C:/Users/Administrator/Downloads/GT_cheat_analysis/WSMenu/src/wsm" --ndk "D:/Android/ndk/android-ndk-r27c" --adb "D:/_migration/profile/Downloads/platform-tools-latest-windows/platform-tools/adb.exe" --output evidence/preflight-next.json
```

Exit code utilitas: `0` berarti **host preflight saja** lulus; `2` berarti laporan tersimpan dengan blocker; `1` berarti gagal menulis laporan. Laporan existing tidak ditimpa.

Utilitas tidak melakukan root, install, attach proses, atau perubahan game. ADB bisa menyalakan daemon host sebagai efek discovery. Parser ELF dibatasi header ELF64 little-endian dan PT_LOAD: bukan dynamic linker, tidak memeriksa semua relocation, simbol, RELRO atau runtime behavior. Keterbatasan ini juga tertulis di JSON.

Fixture test adalah data buatan berlabel jelas. Laporan preflight menggunakan file dan stdout nyata; tidak mengganti data yang hilang dengan fixture.

## 15. Jawaban default atas pertanyaan review v1

1. Set fitur: cukup sebagai backlog; jangan menambah fitur sebelum lifecycle/session gate.
2. Menu: kecil, draggable, collapse, status selalu jelas; tidak perlu framework besar.
3. Multiplayer: semua perubahan gameplay OFF, UNKNOWN juga OFF, tanpa auto-resume.
4. Panic: tombol jelas + shortcut opsional; tiga ketukan tidak menjadi satu-satunya akses.
5. Nama: WSM; bahasa Indonesia default dengan istilah teknis stabil.
6. Cap: belum disertifikasi. Rentang slider mengikuti bukti per fitur/build, bukan angka global 100x/10x.

## 16. Sumber primer dan keterbatasan penelitian

Rujukan diambil pada 6 Oktober 2026. Sumber current upstream tidak membuktikan vendor LDPlayer memakai implementasi yang sama. Referensi Unity 6 dipakai untuk kontrak umum, bukan klaim versi Unity game target. Tidak ditemukan bukti sesi ini untuk invalidasi cache Houdini, daftar export target, atau alur internal Nifuji yang dihipotesiskan.

- [S1] Android Developers — JNI tips. Thread-local environment, references, exception discipline, registration/class-loader context.
  `https://developer.android.com/ndk/guides/jni-tips`
- [S2] AOSP ART — Native Bridge README, current main. Guest linker terpisah; AOSP tidak memasok translator aktual.
  `https://android.googlesource.com/platform/art/+/refs/heads/main/libnativebridge/`
- [S3] AOSP ART — native_loader.cpp, current main; serta kontrak header historis Android 6.0.1_r50. Referensi historis bukan klaim interface vendor saat ini.
  `https://android.googlesource.com/platform/art/+/refs/heads/main/libnativeloader/native_loader.cpp`
  `https://android.googlesource.com/platform/system/core/+/android-6.0.1_r50/include/nativebridge/native_bridge.h`
- [S4] topjohnwu — official Zygisk module sample/header. Kontrak callback dan fd; source lokal tetap dipin terpisah.
  `https://github.com/topjohnwu/zygisk-module-sample/blob/master/module/jni/zygisk.hpp`
- [S5] Linux man-pages — memfd_create(2).
  `https://man7.org/linux/man-pages/man2/memfd_create.2.html`
- [S6] Linux man-pages — mprotect(2).
  `https://man7.org/linux/man-pages/man2/mprotect.2.html`
- [S7] Arm — AAPCS64.
  `https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst`
- [S8] Unity — Awaitable completion and continuation. Kontrak thread Unity.
  `https://docs.unity3d.com/6000.0/Documentation/Manual/async-awaitable-continuations.html`
- [S9] Unity — Camera.WorldToScreenPoint.
  `https://docs.unity3d.com/6000.0/Documentation/ScriptReference/Camera.WorldToScreenPoint.html`
- [S10] Android Developers — InMemoryDexClassLoader API reference.
  `https://developer.android.com/reference/dalvik/system/InMemoryDexClassLoader`
- [S11] Android Developers — Optimize a custom view.
  `https://developer.android.com/develop/ui/views/layout/custom-views/optimizing-view`
- [S12] Android Developers — Support 16 KB page sizes, halaman diperbarui 16 September 2026.
  `https://developer.android.com/guide/practices/page-sizes`

- [S13] AOSP — Dalvik executable format, ShortyDescriptor grammar; `V` adalah void, bukan `v`.
  `https://source.android.com/docs/core/runtime/dex-format`

**Kesimpulan:** review dan alat bantu lokal selesai sebagai deliverable tersendiri; implementasi runtime WSM v2 belum selesai. Permintaan "wajib bekerja" diterjemahkan menjadi acceptance gate yang dapat menggagalkan rilis, bukan janji yang menghapus ketidakpastian.
