#!/bin/bash
# Build WSM Fixture APK (com.wsm.fixture) — minimal manual pipeline: javac -> d8 -> aapt2 -> zip -> zipalign -> apksigner.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="C:/wsmbuild/sdk"
BT="$SDK/build-tools/34.0.0"
PLAT="$SDK/platforms/android-34/android.jar"
JH_MSYS="$LOCALAPPDATA/Temp/gt_cheat_analysis/luaj/jdk-17.0.20.1+1"
export JAVA_HOME="$(cygpath -w "$JH_MSYS" 2>/dev/null || echo "$JH_MSYS")"
JAVAC="$JH_MSYS/bin/javac.exe"
KEYTOOL="$JH_MSYS/bin/keytool.exe"
OUT="$HERE/build"
HERE_M="$(cygpath -m "$HERE")"
OUT_M="$(cygpath -m "$OUT")"

for f in "$BT/d8.bat" "$BT/aapt2.exe" "$BT/zipalign.exe" "$BT/apksigner.bat" "$PLAT" "$JAVAC" "$KEYTOOL"; do
  if [ ! -f "$f" ]; then echo "MISSING: $f"; exit 1; fi
done

rm -rf "$OUT"; mkdir -p "$OUT/classes" "$OUT/dex"
cd "$HERE"

echo "== 1. javac =="
"$JAVAC" --release 8 -cp "$PLAT" -d "$OUT_M/classes" "$HERE_M/MainActivity.java" "$HERE_M/Probe.java"

echo "== 2. d8 =="
"$BT/d8.bat" --min-api 26 --lib "$PLAT" --output "$OUT_M/dex" "$OUT_M/classes/com/wsm/fixture/MainActivity.class" "$OUT_M/classes/com/wsm/fixture/Probe.class"
ls -la "$OUT/dex/classes.dex"

echo "== 3. aapt2 link =="
"$BT/aapt2.exe" link -o "$OUT_M/base.apk" --manifest "$HERE_M/AndroidManifest.xml" -I "$PLAT" --min-sdk-version 26 --target-sdk-version 34

echo "== 4. inject classes.dex =="
python - "$OUT_M" <<'PY'
import sys, os, zipfile
out = sys.argv[1].replace('\\', '/')
base = os.path.join(out, "base.apk"); dex = os.path.join(out, "dex", "classes.dex"); dst = os.path.join(out, "base_dex.apk")
zin = zipfile.ZipFile(base, 'r'); zout = zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED)
for it in zin.infolist():
    zout.writestr(it, zin.read(it.filename))
zout.write(dex, "classes.dex")
zout.close(); zin.close()
print("injected ->", dst)
PY

echo "== 5. zipalign =="
"$BT/zipalign.exe" -f 4 "$OUT_M/base_dex.apk" "$OUT_M/aligned.apk"

echo "== 6. keystore + sign =="
if [ ! -f "$HERE/debug.keystore" ]; then
  "$KEYTOOL" -genkeypair -keystore "$HERE_M/debug.keystore" -alias androiddebugkey \
    -storepass android -keypass android -dname "CN=WSM Fixture,O=WSM,C=ID" \
    -keyalg RSA -keysize 2048 -validity 10000 -noprompt
fi
MSYS_NO_PATHCONV=1 cmd /c "$(cygpath -w "$BT/apksigner.bat") sign --ks $(cygpath -w "$HERE/debug.keystore") --ks-pass pass:android --key-pass pass:android --out $(cygpath -w "$OUT/wsm-fixture.apk") $(cygpath -w "$OUT/aligned.apk")"

echo "== 7. verify =="
MSYS_NO_PATHCONV=1 cmd /c "$(cygpath -w "$BT/apksigner.bat") verify --print-certs $(cygpath -w "$OUT/wsm-fixture.apk")" | head -6
ls -la "$OUT/wsm-fixture.apk"
echo "FIXTURE-BUILD-OK"
