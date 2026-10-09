"""Build an isolated real-view Android UI fixture using a simulated backend."""
import argparse
import subprocess
import shutil
import zipfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
def run(args):subprocess.run([str(x) for x in args],check=True)
def build(jdk,sdk):
    out=HERE/'build';classes=out/'classes';dex=out/'dex'
    managed=out.resolve()
    for target in (classes,dex):
        resolved=target.resolve()
        if resolved==managed or not resolved.is_relative_to(managed): raise ValueError('Preview cleanup outside output')
        if target.exists(): shutil.rmtree(target)
        target.mkdir(parents=True)
    bt=sdk/'build-tools/34.0.0';android=sdk/'platforms/android-34/android.jar'
    menu=REPO/'src/wsm-v2/menu'
    run([jdk/'bin/javac.exe','-source','8','-target','8','-encoding','UTF-8','-classpath',android,'-d',classes]+sorted(menu.glob('*.java'))+[HERE/'PreviewActivity.java'])
    run([jdk/'bin/java.exe','-cp',bt/'lib/d8.jar','com.android.tools.r8.D8','--release','--min-api','26','--lib',android,'--output',dex]+sorted(classes.rglob('*.class')))
    run([bt/'aapt2.exe','link','-o',out/'base.apk','--manifest',HERE/'AndroidManifest.xml','-I',android,'--min-sdk-version','26','--target-sdk-version','34'])
    with zipfile.ZipFile(out/'base.apk') as source,zipfile.ZipFile(out/'with-dex.apk','w') as target:
        for info in source.infolist():target.writestr(info,source.read(info.filename))
        target.write(dex/'classes.dex','classes.dex',compress_type=zipfile.ZIP_DEFLATED)
    run([bt/'zipalign.exe','-f','4',out/'with-dex.apk',out/'aligned.apk'])
    key=out/'debug.keystore'
    if not key.exists():run([jdk/'bin/keytool.exe','-genkeypair','-keystore',key,'-alias','androiddebugkey','-storepass','android','-keypass','android','-dname','CN=WSM Preview,O=WSM,C=ID','-keyalg','RSA','-keysize','2048','-validity','3650','-noprompt'])
    run([jdk/'bin/java.exe','-jar',bt/'lib/apksigner.jar','sign','--ks',key,'--ks-pass','pass:android','--key-pass','pass:android','--out',out/'wsm-menu-preview.apk',out/'aligned.apk'])
    run([jdk/'bin/java.exe','-jar',bt/'lib/apksigner.jar','verify',out/'wsm-menu-preview.apk'])
    print(out/'wsm-menu-preview.apk')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jdk',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True)
    a=p.parse_args();build(a.jdk,a.sdk)
