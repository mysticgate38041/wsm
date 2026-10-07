# GT ENV REGRESSION — catatan forensik
*worm shadow · 6 Okt 2026, ~10:30 · status: OPEN — bukan isu WSM*

## Gejala
`com.kakaogames.gdts` (3.54.0) mati ~0.6–1.0 detik setelah launch, SETIAP kali, TANPA tombstone/crash log, dibunuh SIGKILL dari luar (log: `Zygote: exited due to signal 9`, tanpa `am_kill`). Data GT terakhir ditulis **06:54** (sesi main terakhir He); sejak itu semua launch mati (percobaan pertama yang tercatat: 10:02).

## Kontrol yang sudah dijalankan (semua GT TETAP mati)
1. WSM aktif + Nifuji aktif → mati (~0.4s)
2. WSM di-disable → mati
3. WSM + Nifuji di-disable (NOL modul, dibuktikan ZN menghormati flag disable lewat tes fixture) → mati
4. Cold restart LDPlayer (quit+launch) → mati
5. Reboot device berkali-kali / host reboot → mati
6. Reinstall GT (data kejaga) → mati
7. Launch via `ldconsole runapp` (cara resmi) → mati
8. `setenforce 1` (enforcing) → crash beda (SIGSEGV zygote; env ini butuh permissive) → direvert

**Kesimpulan: regresi level environment, TIDAK terkait modul apa pun.**

## Petunjuk kuat
- 10–15 ms sebelum SETIAP kematian: proses `comm=which` (app=com.kakaogames.gdts) melakukan probe ke `/system/system_ext/bin/su` (getattr + execute attempt). Pola sama di run1 (10:02) dan run reinstall (10:21).
- Probe itu juga membaca `/proc/uptime`.
- `/system/bin/su -> ./magisk` (symlink) ADA dan **TIDAK BISA dihapus** (`/system/bin` read-only, gak bisa remount — "/system" bahkan tidak ada di /proc/mounts).
- `mount --bind` kosong di atas kedua jalur su: bind SUKSES tapi cek keberadaan file (`access(F_OK)`) tetap lolos → `which su` masih menemukan.
- SELinux **permissive** (setiap boot `setenforce 0`) → denial SELinux (getattr/execute su oleh untrusted_app) TIDAK diblokir.
- Houdini gagal fake cpuinfo tiap spawn: `Failed to bind-mount /data/local/cfg-ednye/ as /proc/cpuinfo: Not a directory` + `/system/etc/cpuinfo.arm64.txt: No such file or directory` → /proc/cpuinfo asli = GenuineIntel terlihat. (Bind manual cpuinfo palsu → GT tetap mati; bukan trigger tunggal.)
- magisk.db: hanya policy uid 2000 & 0 (ALLOW). Game (10075) tanpa policy.
- Kandidat penyebab regresi: (a) serangan reboot paksa host (shutdown /f) saat LDPlayer hidup (2×: ~08:00 & ~09:50) → state instance/houdini cfg rusak; (b) mekanisme hide-root LDPlayer (rootMode=false) sudah tidak bekerja.

## Yang TIDAK bisa dipakai sebagai solusi
- Hapus su fisik: /system/bin ro. ✗
- Hide per-app via SELinux: permissive. ✗
- Enforcing: merusak environment (zygote SIGSEGV). ✗
- Remount /system: tidak ada di /proc/mounts; /system/system_ext rw tapi /system/bin tidak. ✗

## Senjata yang tersisa (untuk sesi berikutnya)
- **Root darurat tersembunyi: `/data/local/tmp/su`** (symlink ke `/data/local/tmp/su_bak/magisk_bin`, chmod 755) — TERBUKTI: `/data/local/tmp/su -c id` → uid=0 magisk. Apps tidak bisa lihat /data/local/tmp.
- Backup: `/data/local/tmp/su_bak/{magisk_bin,su_bin}`.
- Evidence log di `src/wsm-v2/evidence/` (g5*, g3c, g4).

## Opsi perbaikan (untuk He / sesi lanjut)
1. **Rebuild environment**: instance LDPlayer baru → root ulang (LDPlayerRoot) → GT fresh install → tes. Paling bersih.
2. **Re-run LDPlayerRoot Verify** — mungkin mengulang langkah hide yang hilang (cek `LDPlayerRoot-main/tools/ldroot_magisk.ps1` untuk aksi hide/invisible).
3. **Deeper RE**: bongkar logika proteksi GT (libil2cpp/il2cpp dump ada di Downloads\Mod\gt_dump) untuk cari kondisi kill → craft bypass (level lanjut).
4. **Tanya He**: apa persisnya keadaan saat terakhir GT jalan (06:54) — dari cara dia launch sampai perubahan setelahnya.

## UPDATE 10:47 — ✅ SOLVED: pelakunya paket fixture kita sendiri!
**Tes eliminasi terakhir mengungkap: `com.wsm.fixture` (app tes WSM kita, terinstall 09:33) = pemicu kematian GT.** GT's protection membunuh game saat paket itu terinstall:
- fixture terinstall → GT mati ~1s, KONSISTEN (9/9 percobaan: 10:02–10:27)
- fixture di-uninstall → GT HIDUP (10:41; kematian sesekali = race quirk first-launch, retry langsung hidup)
- A/B/A: install→mati, uninstall→hidup (dengan 1 kematian race tambahan, normal)
- Tersangka ciri pemicu: `android:debuggable=\"true\"` di manifest fixture (atau nama paket). **Next: rebuild fixture non-debuggable untuk coexistence.**
- Dugaan lama (root/magisk/zygisk/modul/su/permissive/cpuinfo) = SEMUA TERELIMINASI sebagai penyebab kematian ini (tetap berguna sebagai catatan environment).
- **G5 JALAN:** GT hidup dengan WSM + Nifuji sama-sama ter-inject: handshake OK (uid 10075 ×3), probe maps ketemu `libil2cpp base=0x7df545dae000`; `dlopen=FAIL 0/9 sym` (batasan bridge x86_64→ARM64; next: resolusi simbol via ELF-parse file yang ke-map, ala Nifuji by-name).
- Evidence: `src/wsm-v2/evidence/g5c-gt-full-*.log`, `gt-running-with-wsm.png`. Operasional: fixture cuma dipasang saat tes WSM; uninstall sebelum main GT.
