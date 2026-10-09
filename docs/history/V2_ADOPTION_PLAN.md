# WSM v2 — ADOPSI REVIEW (keputusan & rencana eksekusi)
*worm shadow · 6 Okt 2026 · status: review DITERIMA — menunggu GO eksekusi dari He*

## Keputusan
- Paket `WSM-v2-review-and-preflight.zip` **DITERIMA** sebagai baseline proses v2.
- POC `src/wsm` **DIBEKUKAN (frozen)** — tidak ada fitur baru di atasnya; jadi referensi & bukti sejarah.
- Implementasi v2 di `src/wsm-v2/` mengikuti `WSM_V2_MODERNIZATION.md` (kontrak + gate G0–G9).
- Prinsip baru dikunci: label bukti (OBSERVED_LOCAL / SOURCE_VERIFIED / PROPOSED / UNVERIFIED_TARGET), state machine proses, epoch (process/activity/scene/module), command/ack dengan desired ≠ accepted ≠ applied ≠ observed, session gate fail-closed, evidence imutabel lewat `tools/wsm_preflight.py`.

## Temuan audit yang DIAKUI (A01–A20)
- **P0:** A01 fallback tanpa gate · A02 provenance handle bridge (host dlopen ≠ guest handle) · A03 shorty `v` vs `V` + audit calling-convention · A04 engine baru sebatas POC.
- **P1:** A05 allowlist proses · A06 check bool `exemptFd` · A07 ownership fd · A08 batas payload + EINTR · A09 verifikasi identitas payload · A10–A11 disiplin JNI reference/exception · A12 handshake sesi (build ID/PID/protocol/capability/nonce) · A13 bootstrap idempotent terpadu · A14 kanal diagnostik milik sendiri · A15 checks ELF utk engine juga · A16 pin NDK exact 27.2.12479018 · A17 normalisasi ZIP/reproducibility · A18 API level vs jalur DEX (26/27/29) · A19 logging pre-filter · A20 konsistensi ABI installer.
- **Koreksi desain (§4) diadopsi:** atomicity patch (verify+write satu blok; restore lewat jalur izin tulis), state-restore ≠ byte-restore, hook butuh ABI lengkap (calling convention/FP/callee-saved/stack), retraksi klaim timing translator, kontrak main-thread Unity, semantik WorldToScreenPoint, aturan publication snapshot.

## Komponen v2 (dari §5) — diadopsi
EnvironmentProbe · IdentityVerifier · BootstrapCoordinator · RuntimeAdapter · LifecycleController · SessionGate · CommandDispatcher · FeatureRegistry · SnapshotChannel · Diagnostics.

## Gate & status awal
G0 PASS (audit/review ini) · G1 partial · G2–G9 NOT_RUN/BLOCKED sesuai §13.
Fault-injection list (§13) wajib jadi test plan.

## Langkah eksekusi (dari §13, urut)
1. Buat branch v2 (`src/wsm-v2/`) + catatan; POC frozen.
2. Tutup P0: BootstrapCoordinator (satu keputusan load + handshake exactly-once) · RuntimeAdapter + provenance kontrak · audit calling convention · engine skeleton state machine.
3. Fixture milik sendiri: app Android minimal + payload identitas (tanpa menyentuh game) → kontrak JNI/ABI/lifecycle di native ABI dulu.
4. Core/control plane: registry, dispatcher, snapshot, config, session gate + unit/concurrency tests + fault injection.
5. Integrasi read-only target (device siap) → telemetry.
6. UI visual → satu fitur perubahan keadaan di fixture lokal → regresi → kandidat target (matriks §11).
7. Tooling: `tools/wsm_preflight.py` jadi gate wajib tiap langkah; output evidence nama unik/imutabel.

## Keputusan menunggu He
- **GO eksekusi** (mulai langkah 1–2).
- Nama folder v2: `src/wsm-v2/` — oke?
- Izin bikin + install **"WSM Fixture"** (apk kita sendiri) ke LDPlayer untuk gate G3/G4.
- LDPlayer dinyalakan saat gate integrasi read-only (G5).

## Anchor bukti
- Paket review: `WSMenu/v2-review/` (10 file; hash di `MANIFEST.json`).
- Verifikasi independen (6 Okt ~08:15): 36/36 unit test PASS; preflight BLOCKED (4× BINARY_MISSING + NO_READY_DEVICE) — bukti: `v2-review/evidence/preflight-verify-wormshadow-2026-10-06.json`.
- Mesin host sehat pasca-reboot; clang 18.0.3 OK; exclusion Defender terpasang.
