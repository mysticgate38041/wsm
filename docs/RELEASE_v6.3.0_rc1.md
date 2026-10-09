# WSM 6.3.0 RC1 — modernisasi runtime dan UI

Prerelease untuk arsitektur dan UI/UX 18 kontrol WSM. Target tetap `com.kakaogames.gdts` 3.54.0 (423), API 26+, x86_64 dan arm64-v8a. Module versionCode 60301, protocol 3.

- Menu dipisah menjadi controller, backend, scheduler, snapshot, preferences dan view. Layout responsif, kategori/pencarian, profil manual, state pending/error dan PANIC tetap terlihat.
- State 18 kontrol diterbitkan atomik sekali per putaran worker. Snapshot status memakai lock terpisah; telemetry merekam antrean/waktu completion. Hasil terlambat dan epoch lama diabaikan. Status belum siap tidak menghasilkan konfirmasi OFF palsu.
- Loader mempunyai fase bootstrap dan durasi/error terstruktur. JNI worker dilepas sebelum diagnostic wait.
- Source/version/fixture graph dipusatkan dalam manifest. Java/DEX memakai cache hash tervalidasi dan D8 release; dua suite Java tetap berjalan pada cache hit. Empat belas fixture native dibangun untuk dua ABI.
- CI menjalankan host native, build-cache dan package tests serta matrix ndk-build/CMake. CD hanya menerbitkan setelah semua gate lulus, dengan checksum serta source/binary provenance.

Lihat [laporan modernisasi](MODERNIZATION_V6_3.md) untuk arsitektur, verifikasi dan keterbatasan. Preview UI memakai backend simulasi. Build/tests tidak membuktikan seluruh efek gameplay atau perbaikan SIGKILL historis. Tidak ada klaim peningkatan FPS/CPU/RAM tanpa benchmark game. Katalog 47 tetap incomplete; backend efek/pulse yang sudah ada belum seluruhnya dimodernisasi.

ZIP, `.sha256` dan `ci-build-provenance.json` berasal dari job ndk-build pada commit/tag/run yang sama. CMake menjadi gate tambahan. Release/tag lama tidak ditimpa.
