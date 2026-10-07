"""Build an ARM64-only synthetic emitter instrumentation APK; no install here."""
import hashlib, json, subprocess, zipfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
OUT=ROOT/"build/arm64-reloc-probe"
NDK=Path("D:/Android/ndk/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin")
SDK=Path("C:/wsmbuild/sdk")
JAVA=Path("C:/Users/Administrator/AppData/Local/Temp/gt_cheat_analysis/luaj/jdk-17.0.20.1+1/bin")
BT=SDK/"build-tools/34.0.0"
PLATFORM=SDK/"platforms/android-34/android.jar"
def run(*args):
    subprocess.run([str(a) for a in args],check=True)
def build():
    (OUT/"classes").mkdir(parents=True,exist_ok=True)
    (OUT/"dex").mkdir(exist_ok=True)
    run(NDK/"aarch64-linux-android26-clang++.cmd","-std=c++17","-O2","-shared","-fPIC","-fno-exceptions","-fno-rtti","-nostdlib++","-fvisibility=hidden","-Wall","-Wextra","-Werror","-Wno-unused-variable","-Wno-unused-parameter","-Wl,-z,max-page-size=16384",HERE/"probe.cpp","-llog","-ldl","-pthread","-o",OUT/"libwsmrelocprobe.so")
    run(JAVA/"javac.exe","-source","8","-target","8","-cp",PLATFORM,"-d",OUT/"classes",HERE/"Runner.java")
    run(JAVA/"java.exe","-cp",BT/"lib/d8.jar","com.android.tools.r8.D8","--min-api","26","--lib",PLATFORM,"--output",OUT/"dex",OUT/"classes/com/wsm/relocprobe/Runner.class")
    run(BT/"aapt2.exe","link","-o",OUT/"base.apk","--manifest",HERE/"AndroidManifest.xml","-I",PLATFORM)
    with zipfile.ZipFile(OUT/"base.apk") as source,zipfile.ZipFile(OUT/"combined.apk","w",zipfile.ZIP_DEFLATED) as target:
        for name in source.namelist():
            target.writestr(source.getinfo(name),source.read(name),compress_type=zipfile.ZIP_STORED if name=="resources.arsc" else source.getinfo(name).compress_type)
        target.write(OUT/"dex/classes.dex","classes.dex")
        target.write(OUT/"libwsmrelocprobe.so","lib/arm64-v8a/libwsmrelocprobe.so")
    run(BT/"zipalign.exe","-f","4",OUT/"combined.apk",OUT/"aligned.apk")
    run(JAVA/"java.exe","-jar",BT/"lib/apksigner.jar","sign","--ks",ROOT.parents[0]/"fixture/debug.keystore","--ks-pass","pass:android","--key-pass","pass:android","--out",OUT/"wsm-arm64-reloc-probe.apk",OUT/"aligned.apk")
    run(JAVA/"java.exe","-jar",BT/"lib/apksigner.jar","verify",OUT/"wsm-arm64-reloc-probe.apk")
    print(json.dumps({"apk":str(OUT/"wsm-arm64-reloc-probe.apk"),"sha256":hashlib.sha256((OUT/"wsm-arm64-reloc-probe.apk").read_bytes()).hexdigest()}))
if __name__=="__main__":build()
