"""Exercise customize.sh in an isolated /data/local/tmp tree; never installs a module."""
import argparse,json,subprocess,tempfile,time,zipfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument("--adb",required=True);p.add_argument("--serial",required=True);p.add_argument("--output",type=Path,required=True);a=p.parse_args()
project=Path(__file__).resolve().parents[1];prefix=[a.adb,"-s",a.serial]
def adb(*args):
    r=subprocess.run(prefix+list(args),capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=30)
    return r
session="wsm-installer-unit-"+str(time.time_ns());remote="/data/local/tmp/"+session
results=[]
with tempfile.TemporaryDirectory(prefix="wsm-installer-") as folder:
    stage=Path(folder)/session;stage.mkdir()
    with zipfile.ZipFile(project/"dist/wsm-v6.1.0-rc2.zip") as z:
        for n in z.namelist():
            if not n.startswith("META-INF/"):
                path=stage/n;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(z.read(n))
    (stage/"harness.sh").write_text("""#!/system/bin/sh
BOOTMODE="$2";API="$3";ARCH="$4";MAGISK_VER_CODE="$5";MODPATH="$1"
abort() { echo "ABORT: $*"; exit 91; }
ui_print() { echo "$*"; }
set_perm_recursive() { [ "$2/$3/$4/$5" = "0/0/0755/0644" ]; }
. "$MODPATH/customize.sh"
""",encoding="utf-8",newline="\n")
    r=adb("push",str(stage),"/data/local/tmp/");assert r.returncode==0,r.stderr
for name,boot,api,arch,magisk,expected in [
    ("x86_64", "true","34","x64","26000",0),
    ("arm64", "true","26","arm64","26000",0),
    ("old_api", "true","25","x64","26000",91),
    ("unsupported_abi", "true","34","arm","26000",91),
    ("recovery", "false","34","x64","26000",91),
    ("old_magisk", "true","34","x64","25000",91),
    ("invalid_api", "true","NaN","x64","26000",91)]:
    r=adb("shell","sh "+remote+"/harness.sh "+" ".join([remote,boot,api,arch,magisk]))
    results.append({"case":name,"expected":expected,"exit":r.returncode,"pass":r.returncode==expected,"output":r.stdout.strip(),"stderr":r.stderr.strip()})
adb("shell","printf tampered >> "+remote+"/module.prop")
r=adb("shell","sh "+remote+"/harness.sh "+remote+" true 34 x64 26000")
results.append({"case":"tampered_hash","expected":91,"exit":r.returncode,"pass":r.returncode==91,"output":r.stdout.strip(),"stderr":r.stderr.strip()})
report={"scope":"INSTALLER_UNIT_ONLY_NO_MODULE_INSTALLED","serial":a.serial,"temporaryDeviceDirectory":remote,"cases":results,"passed":sum(x["pass"] for x in results),"total":len(results)}
a.output.parent.mkdir(parents=True,exist_ok=True)
with a.output.open("x",encoding="utf-8") as f:json.dump(report,f,indent=2)
print(json.dumps(report,indent=2));raise SystemExit(0 if all(x["pass"] for x in results) else 1)
