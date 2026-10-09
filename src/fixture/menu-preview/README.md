# WSM UI preview

Aplikasi Android terisolasi `com.wsm.preview` menggunakan view, controller, scheduler dan preferences produksi dengan backend simulasi 600 ms. Tidak memanggil JNI, memuat modul atau mengakses proses game. Package ini tidak berada di allowlist loader.

Build dengan JDK 17 dan Android SDK platform/build-tools 34, dari folder fixture:

```powershell
python build_preview.py --jdk C:/tools/jdk-17 --sdk C:/Android/sdk
adb install --no-incremental -r build/wsm-menu-preview.apk
adb shell am start -n com.wsm.preview/.PreviewActivity --es orientation landscape
# Force-stop fixture sebelum mengubah orientasi melalui intent.
adb shell am force-stop com.wsm.preview
adb shell am start -n com.wsm.preview/.PreviewActivity --es orientation portrait
```

Build output dan kunci debug lokal berada di `build/` yang diabaikan Git. Build membersihkan classes/DEX fixture sebelum kompilasi. Periksa ON/OFF, pending/hasil, pencarian, slider, PANIC, collapse, kategori serta simpan/muat profil. Penerapan profil berurutan dapat berjalan sekitar 15 detik; tunggu label selesai sebelum menilai hasil. Pengujian failure/timeout/epoch memakai `MenuControllerTest` dengan scheduler deterministik.
