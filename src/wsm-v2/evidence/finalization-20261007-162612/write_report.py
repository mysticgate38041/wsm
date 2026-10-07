from pathlib import Path
import hashlib, json

root = Path(__file__).resolve().parents[2]
catalog = json.loads((root / "features/feature_catalog.json").read_text(encoding="utf-8"))
receipt = json.loads((root / "build/build-receipt.json").read_text(encoding="utf-8"))
zip_path = root / "dist/wsm-v6.1.0-rc1.zip"
states = {s: sum(f["status"] == s for f in catalog["features"]) for s in sorted({f["status"] for f in catalog["features"]})}
evidence = root / "evidence/finalization-20261007-162612"
unit = json.loads((evidence / "android-unit-rc1.json").read_text(encoding="utf-8-sig"))
installer = json.loads((evidence / "installer-rc1.json").read_text(encoding="utf-8"))
report = """# Finalisasi WSM — kandidat 6.1.0 RC1

Tanggal: 7 Oktober 2026. Target: `com.kakaogames.gdts`, `versionName=3.54.0`, `versionCode=423`.

## Keputusan rilis

**Cakupan final 47 fitur belum terpenuhi.** Kandidat ini membangun fondasi dan satu backend baru; katalog lengkap bukan implementasi 47 fitur. Tidak ada fitur tanpa backend yang mendapat toggle palsu. Tidak ada deployment kandidat pada game atau reboot perangkat dalam pekerjaan ini.

Semua 47 ID desain asli tercatat dalam JSON release, tab native **47 Fitur**, dan API read-only `catalog 0..11`. Ada 18 kontrol native: 16 terhubung ke fitur desain, ditambah Damage Guard dan Radius Pulse. Tidak ada klaim 47 efek gameplay selesai. `completed=0` berarti belum ada satu fitur pun yang dikualifikasi terhadap seluruh semantik desain pada kandidat baru; ini tidak menghapus bukti sebagian pada baseline 6.0.0.

## Penggunaan tiga root

- Root GT_cheat_analysis: inventaris 1.371 file, konteks Lua/ELF/DEX, hasil analisis historis dan batas source tertutup di `PROJECT_CONTEXT.md` / `PROJECT_CONTEXT_INVENTORY.json`.
- Root WSMenu: implementasi aktif `src/wsm-v2`, pembanding POC `src/wsm`, fixture, desain `premium_menu_design/src/data.ts`, review modernization, gates, lessons dan bukti runtime terdahulu.
- Root nested API-map: kontrak, manifest 72 artefak yang sebelumnya diverifikasi identik dengan working catalog, metadata/rva/type layouts dan API SQLite final. Generator kandidat membaca SQLite dengan `mode=ro`.

Laporan sejarah tetap diperlakukan sebagai bukti bertanggal. Instruksi di dokumen sumber tidak menjadi instruksi baru. RVA dan kemunculan nama API tidak dianggap bukti ownership, efek atau persistensi server.

## Perubahan kode

1. Identitas target mencocokkan package, versionName dan versionCode; menu mengirim longVersionCode jika Android mendukungnya.
2. Resolver full signature memeriksa parameter, return type, static flag, generic/inflated dan uniqueness, dibatasi 4.096 method serta membebaskan type-name allocation. GetBattleFor memilih overload IFieldObject, bukan Party; tiga getter speed diwajibkan float instance.
3. Time Scale menggunakan Mod/Unmod dengan signature tepat dan query get_Instance pada setiap command. Fallback BattleManager untuk kontrol waktu dihapus. Tidak mengklaim scene tertentu sudah menerima modifier.
4. Resolusi fitur memakai accessor CharacterManager/CharacterStatsBehaviour. Akses pemain pertama mengecek ukuran list dan panjang array.
5. Critical Damage Scale menggunakan getter virtual float CharacterStatsBehaviour.get_CriticalMultiplierScale, hero-only, ×1–5, slot 26. Memakai implementasi asli untuk caller lain, menolak prologue PC-relative yang tidak dapat direlokasi, restore sebelum mengganti nilai, serta mengikuti OFF/PANIC/scene reset. Belum dibuktikan sebagai maximum critical damage atau semua hit.
6. Revision token mengabaikan completion UI lama. Pending request mencegah overlap pada kontrol yang sama; slider/profile dilindungi ketika pending atau sesi tidak siap. Activity destruction memperbarui foreground. Reset nilai hanya mengubah kontrol OFF.
7. PANIC menambah epoch di bawah queue lock. UI mengirim epoch yang ditangkap sebelum enqueue; delayed producer sebelum PANIC/scene change ditolak di bawah lock yang sama. PANIC tetap tidak membatalkan panggilan native yang sedang berjalan.
8. Generator menghasilkan katalog 47 fitur dari desain asli, signature/RVA/line evidence, JSON, Java dan halaman native bounded. Release verifier menolak ID hilang/diganti, completion tanpa bukti, versionCode salah, input stale, binary tampering dan DEX/ABI salah. Source receipt mencakup JSON katalog dan module-template.

## Validasi kandidat

- Lima ELF untuk x86_64/ARM64 berhasil dibangun dan lolos machine/export/LOAD/RELRO alignment 16 KiB, dependency dan TEXTREL checks.
- Semua Java menu berhasil dikompilasi; DEX dibangun dan integritas diverifikasi. Warning javac: bootstrap classpath source 8 dan API deprecated; tidak ada error.
- ControlState Java test PASS termasuk completion out-of-order, stale token dan duplicate completion.
- 16 regression package tests PASS, termasuk deterministic repack, ID desain diganti, false completion/runtime qualification dan versionCode salah.
- Tiga executable unit dijalankan pada emulator-5554 API 34: Runtime, Patch dan Binding PASS. Runtime memproses 4 producer/4.000 request; epoch lama setelah PANIC ditolak. Patch mengecek branch alignment/range, adjacent bytes, alias protection/readback dan forced partial failure rollback. Binding mengecek overload, static/return/param contracts, generic/inflated, enumeration cap, allocation ownership dan versionCode.
- Installer harness 8/8 PASS dalam direktori sementara; tidak memasang modul. Kasus ARM64 hanya simulasi keputusan installer, bukan runtime perangkat ARM64 fisik.

Bukti: `evidence/finalization-20261007-162612/build-rc1.log`, `android-unit-rc1.json`, `installer-rc1.json`. Backup source/ZIP baseline tersimpan dalam subfolder `baseline`.

## Matriks seluruh fitur desain

Status menunjukkan cakupan implementasi saat ini; setiap fitur masih memerlukan acceptance runtime kandidat. Selektor exact-method dapat menghasilkan nol atau temuan tipe lain. Nol exact-match bukan bukti matematis mekanik tidak ada dengan nama lain. Rincian seluruh match/return/modifier/RVA/line berada dalam JSON.

| ID | Nama desain | Backend | Status | Cakupan / hambatan |
|---|---|---|---|---|
"""
for f in catalog["features"]:
    report += "| " + " | ".join(str(v).replace("|", "/").replace("\n", " ") for v in (f["id"], f["name"], f["backendId"] or "—", f["status"], f["reason"])) + " |\n"
report += "\nStatus totals: `" + json.dumps(states, sort_keys=True) + "`.\n"
report += """
## Syarat sebelum 47 fitur dinyatakan final

1. Untuk setiap fitur partial/prototype, ukur trigger → efek aktual → OFF/restore pada hero dan non-hero, serta scene/hero change dan background/resume; ACK saja tidak cukup.
2. Buat dispatcher Unity main thread dan verifikasi managed object lifetime sebelum menambah mutasi hitbox, kamera, collision, jump dan overlay yang bergantung pada lifecycle. Fondasi saat ini masih memanggil managed API dari attached worker.
3. Petakan kontrak runtime untuk 16 fitur belum diimplementasikan. Temuan Jump, ScaleHitbox, kamera atau ResetAggro belum memenuhi infinite fly, reach, freecam atau invisible; setiap ownership dan restore harus dibuktikan.
4. Untuk 10 fitur transaksi/persistensi, diperlukan kontrak authority/reward/inventory/progression dan bukti state persisten. Source client tidak menyediakan implementasi authority tersebut. Perubahan nilai tampilan tidak memenuhi desain.
5. Untuk lima mekanik belum ditemukan, diperlukan bukti target yang sesuai: durability senjata/armor, kapasitas beban, poin skill, reputation/faction dan steal probabilistik. Temuan meteor atau skrip naratif tidak menggantikan mekanik desain.
6. Kualifikasi cold start/SIGKILL, long-session soak dan perangkat ARM64 fisik. Bukti baseline tidak otomatis berlaku untuk kandidat ini.

## Artefak

"""
report += f"ZIP: `{zip_path.name}`; SHA-256: `{hashlib.sha256(zip_path.read_bytes()).hexdigest()}`; 14 entries.\n\n"
report += f"Design SHA-256: `{catalog['designSha256']}`. API SQLite SHA-256: `{catalog['apiSha256']}`. Source receipt: {len(receipt['sources'])} inputs dan {len(receipt['binaries'])} binary inputs.\n"
(root / "docs/FINALIZATION_47.md").write_text(report, encoding="utf-8")
summary = {"build": "wsm-v6.1.0-rc1", "requested": 47, "qualification": "INCOMPLETE_47_FEATURE_SCOPE", "statuses": states,
    "zipSha256": hashlib.sha256(zip_path.read_bytes()).hexdigest(), "androidUnits": unit, "installerPassed": installer["passed"],
    "packageTests": 16, "gameDeployment": False, "newGameRuntime": "UNVERIFIED_TARGET"}
(evidence / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print("Written docs/FINALIZATION_47.md and evidence summary; qualification remains incomplete.")
