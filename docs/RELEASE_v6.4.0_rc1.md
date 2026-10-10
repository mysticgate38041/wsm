# WSM 6.4.0 RC1 — GM mode, kanal terenkripsi, waypoint, kamera

Prerelease untuk era GM: command group, stealth berlapis dan kontrol baru. Target tetap `com.kakaogames.gdts` 3.54.0 (423), API 26+, x86_64 dan arm64-v8a. Module versionCode 60401, protocol 3.

- **Grup `gm`**: `gm reset/status/max/all/preset save|load|list` sebagai komposisi murni di atas handler produksi yang sudah terverifikasi; preset in-memory (8 slot; nama `[a-z0-9_]` ≤16). `gm reset` setara PANIC dan dikecualikan dari gate sesi seperti panic.
- **Waypoint**: `gm pos save|load|list <0..7>` — teleport absolut ke titik tersimpan lewat jalur teleport produksi, lengkap dengan bukti read-back `ok=1`.
- **Kamera**: `fov 0|2..40` — override ukuran kamera (orthographic) via API resmi `Stage.get_StageCamera` + `StageCamera.OverrideDefaultCameraSize/ResetDefaultCameraSize`; state owned direstore oleh PANIC dan pergantian scene. Eksperimental (katalog: prototype).
- **`feat aggro 0|1`** dibuka sebagai eksperimental — sweep `ResetAggro` per-beat lewat gate terproteksi (katalog: partial).
- **Menu native**: blok **"GM · AKSI PAKET"** — GM ALL ON/OFF, MAX DMG, MAX AURA, SIMPAN/MUAT GM, SIMPAN/MUAT POS (slot 0) — tampil pada kategori Semua/Pertempuran.
- **Stealth F1**: memfd memakai nama kelas runtime Android (`jit-cache`, `dalvik-jit-code-cache`, `jit-zygote-cache`) sehingga `/proc/*/maps` tidak pernah memuat string "wsm"; environment `WSM_*` dikonsumsi dan dibersihkan tuntas setelah bootstrap (nonce dicache di proses); `WSM_PROTOCOL` dihapus; file transport memakai nama token bergaya app.
- **Stealth F2**: kanal command terenkripsi per-proses — nonce dibuat pada fase pre-specialize (root) dan dijatuhkan 0600 ke module dir (hanya tooling root yang bisa membaca); frame `E1:<hex>` dua arah (keystream splitmix64; vektor interoperabilitas terkunci di fixture transport); frame plaintext lama tetap diterima untuk kompatibilitas. Logcat `CTL:` direduksi menjadi token pertama (command/ack utuh dan nonce tidak pernah masuk log).
- **Tool audit**: `scripts/stealth_check.py` — audit bocor read-only (module dir + nonce perms, maps/environ/fd proses game, isi transport, higiene logcat). Exit 2 bila ada temuan.
- **16 suite tes**: 15 fixture native per ABI (baru: `transport` dengan vektor terkunci) + 2 suite Java/DEX regression.

Verifikasi lokal sebelum tag: build penuh dua ABI (ndk-build + gate CMake), Java/DEX, paket + receipt PASS; battery fixture di device 15/15 PASS (termasuk vektor kripto terkunci). Gameplay acceptance per-fitur tetap UNVERIFIED sesuai disiplin repo — ACK bukan bukti efek; katalog 47 tetap incomplete. Ekonomi server-authoritative (gem/gold/gacha/reward) dan mode ranked/co-op tetap di luar scope; server tidak pernah disentuh.

ZIP, `.sha256` dan `ci-build-provenance.json` berasal dari job ndk-build pada commit/tag/run yang sama. Release/tag lama tidak ditimpa.
