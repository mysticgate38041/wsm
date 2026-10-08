# Perbaikan force close WSM RC3

Target pengujian: Guardian Tales `com.kakaogames.gdts` 3.54.0/code 423, LDPlayer Android 14/API 34, x86_64 dengan Houdini ARM64. Ini adalah perbaikan dua jalur crash yang ditemukan, bukan sertifikasi seluruh 47 fitur.

## Akar masalah

**Lobby 22:57:** emulator 3 GiB tanpa swap mencapai MemAvailable 77–78 MiB. LMKD membunuh anak pengawas game dengan alasan direct reclaim/thrashing. Pengawas pasangan di `libdxbase.so` membuat proses utama mengirim SIGKILL kepada dirinya. Trace terkontrol mereproduksi anak dibunuh lalu main self-kill 31.689 ms kemudian. Trace tersebut merupakan reproduksi terpisah; penerapannya pada kejadian asli merupakan inferensi kuat dari log LMKD, relasi parent dan binary identik. Belum ada bukti leak WSM tertentu atau perbandingan beban tanpa WSM.

**Clear Stage 23:05:** empat mobil dungeon `garage` menggunakan `GarageCharacterDamagedBehaviour`, yang bukan turunan `MonsterDamagedBehaviour`. Worker WSM memanggil MethodInfo Damage/Die monster pada receiver mobil, mengakses layout/virtual slots yang berbeda, dan dapat memanggil kematian dua kali. Tombstone merekam worker, SIGSEGV/Houdini fatal execution; alamat instruksi pertama yang salah belum direkam, sehingga nilai slot virtual tertentu tidak diklaim.

## Perbaikan source

- Clear Stage memakai `DamageCommandUtil.OnExecute(CommandTypes.None, DamageInfo)` supaya game memilih implementasi behaviour target melalui pipeline normal. Tidak ada pemaksaan MonsterDamagedBehaviour.Die pada mobil.
- Factory DamageInfo mengikuti ABI `runtime_invoke`: reference argument adalah pointer objek; value argument menunjuk storage nilainya. Box dipertahankan melalui GC handle dan di-unbox lewat API, tanpa salinan struktur hardcoded `0x2F8`.
- Operasi damage berjalan dalam helper ARM64 pada callback `UnitySynchronizationContext.ExecuteTasks`. Thread harus bernama UnityMain. Hook mengubah satu instruksi 4 byte, merelokasi instruksi pertama dan mempertahankan fungsi asli.
- Target dipotret sebagai identitas melalui GC handles. Sebelum setiap target, stage dan keanggotaan daftar diperiksa kembali. Array length divalidasi. Pergantian stage, exception dan cancellation menghentikan operasi; semua handle dilepas. Target tidak dibaca lagi setelah callback kematian.
- OHK/One HP memakai dispatcher yang sama, dengan filter radius. One HP membaca HP terbaru dan menggunakan notMortal. Jalur worker Clear Stage/MDMG yang lama dihentikan.
- UI pause/PANIC mencabut izin sweep. Command baru ditolak selama sweep berjalan. Hasil `applied` baru diterbitkan setelah callback mengakui penyelesaian. Timeout menahan storage request agar tidak digunakan ulang saat masih berjalan.
- Dispatcher menggunakan slot 31 sendiri dan tetap menjadi callback kosong setelah PANIC; restore fitur hanya mencakup slot 0–30. Trampoline tetap hidup sampai proses berakhir.
- SIGSEGV recovery tidak melakukan host siglongjmp melintasi managed/Houdini calls yang dibungkus. Instalasi handler menggunakan pthread_once; parsing ELF tidak lagi mengganti handler sementara.
- Saat kontrol OFF, pemeriksaan scene berjalan tiap 1 detik dan polling karakter berkala dihentikan. Ini mengurangi pekerjaan/alokasi managed, tetapi bukan klaim bahwa konsumsi memori game seluruhnya berasal dari WSM.

## Verifikasi

Build dual ABI, Java/DEX, 16 package regression tests, 9 catalog tests, 5 unit Android dan 8 installer harness cases lulus. Probe ARM64 terisolasi melalui Houdini lulus: dispatcher menolak worker, menerima UnityMain, mempertahankan fungsi asli dan memulihkan patch. Unit sweep meliputi mobil/monster, daftar berubah, stage berubah, cancellation, managed exceptions, GC cleanup, batas array, radius dan One HP idempotence. Probe sintetis tidak menggantikan pengujian dungeon sebenarnya.

Hasil pemasangan, profil memori dan pengujian game aktual akan dicatat di bawah setelah selesai. Bukti mentah yang berisi payload SDK/login tetap lokal dan tidak disertakan ke repository.
