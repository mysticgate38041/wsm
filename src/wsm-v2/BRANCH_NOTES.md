> Arsip status POC sebelum versi 6. Untuk implementasi/rilis saat ini, baca `README.md` dan `docs/RELEASE_VALIDATION.md`. Klaim read-only dan build lama di bawah tidak berlaku untuk versi 6.

# WSM v2 — BRANCH NOTES
*worm shadow · untuk He · branch "v2" = satu-satunya jalur implementasi; POC `src/wsm` DIBEKUKAN.*

## Aturan branch ini
- Semua klaim mengikuti label bukti (OBSERVED_LOCAL / SOURCE_VERIFIED / PROPOSED / UNVERIFIED_TARGET).
- Tidak ada fallback otomatis: satu Adapter (System.load) dengan gate + handshake; jalur lain dicabut dari alur.
- Kontrak: state machine + epoch (dipakai penuh saat registry hadir), command/ack belum aktif (fase read-only).
- Evidence: setiap langkah build/integrasi → preflight tool + logcat tersimpan (nama unik, tidak menimpa).
- Tidak menyentuh proses game sebelum gate read-only (G5) dinyatakan lulus di fixture.

## Peta penutupan temuan P0/P1 (audit WSM_V2_MODERNIZATION.md)
| ID | Status di v2 (poc1) |
|---|---|
| A01 fallback tanpa gate | DITUTUP — single load decision (System.load saja); jalur B experiment dihapus dari flow |
| A02 provenance handle bridge | DITUTUP — kode trampoline/direct-call dihapus total |
| A03 shorty 'v' + calling convention | DITUTUP — tidak ada pemanggilan trampoline lagi |
| A04 engine POC | BERGANTUNG GATE — engine v2 kini: handshake + probe read-only; fitur tetap UNVERIFIED_TARGET |
| A05 allowlist proses | DITUTUP — exact match 2 nama (game + fixture), tanpa prefix |
| A06 exemptFd bool | DITUTUP — dicek; gagal = jalur load dibatalkan |
| A07 ownership fd | DITUTUP — didokumentasikan di kode; payload fd ditutup setelah load; cleanup cabang gagal |
| A08 batas payload + EINTR | DITUTUP — batas 32 MiB, retry EINTR, error typed |
| A09 verifikasi payload | DITUTUP — ELF magic/class/machine dicek + FNV64 dicatat + munmap dicek |
| A10/A11 JNI refs | DITUTUP — PushLocalFrame/PopLocalFrame + exception tidak saling menimpa |
| A12 handshake | DITUTUP — handshake chan (magic/proto/nonce/pid/uid/build/abi/caps), timeout eksplisit |
| A13 bootstrap idempotent | DITUTUP — satu jalur tunggal; engine punya duplicate-init guard |
| A14 kanal diagnostik | DITUTUP — logcat + channel memfd; tanpa tulis /data/local/tmp |
| A15 build checks | DITUTUP — build.ps1 v2 memeriksa KEDUA library (loader+engine) |
| A16 pin NDK | DITUTUP — pin exact 27.2.12479018 (override eksperimental terpisah) |
| A17 zip reproducible | SEBAGIAN — entry order dinormalisasi; determinisme penuh menyusul |
| A18 min API | DITUTUP — APP_PLATFORM=android-26 (jalur DEX nanti ≥26) |
| A19 logging | DITUTUP — non-match = senyap; hanya match/error yang dilog |
| A20 konsistensi ABI | DITUTUP — installer hanya menerima ABI yang benar-benar dikirim |

## Arsitektur poc1 (fase R = read-only)
```
L1 loader x86_64:  pre → allowlist → stage payload(memfd) + channel(memfd 4K) + env + worker
                   post → sinyal (tanpa kerja berat)
                   worker → System.load(/proc/self/fd/N) → handshake poll → log evidence
L2 engine arm64:   JNI_OnLoad → thread: channel map → ack handshake → JNI callback 2-arah (fixture)
                   → scan maps libil2cpp (≤30s) → dlopen+dlsym capability report → probe report
```
Tidak ada write ke game di fase ini. G3/G4 diuji di app fixture; G5 read-only di GT.

## Build
`powershell -File scripts/build.ps1 -Ndk D:\Android\ndk\android-ndk-r27c` → `dist/wsm-v2.0.0-poc1.zip`
(output: zygisk/x86_64.so, zygisk/arm64-v8a.so, engine/arm64-v8a.so)
