"""Read-only evidence collector for the explicitly authorized local test device."""
import subprocess,shlex,base64,struct,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ADB=r"C:\LDPlayer\LDPlayer14\adb.exe"
PY=r"C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
NM=r"D:\Android\ndk\android-ndk-r27c\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe"
PRE=[ADB,"-s","emulator-5554"]
def shell(cmd,root=False):
    if root:cmd="/data/local/tmp/su -c "+shlex.quote(cmd)
    q=subprocess.run(PRE+["shell",cmd],capture_output=True,timeout=20)
    if q.returncode:raise RuntimeError(q.stdout.decode(errors="replace")+q.stderr.decode(errors="replace"))
    return q.stdout.decode(errors="replace").strip()
def command(cmd):
    q=subprocess.run([PY,"-B","-X","utf8",str(ROOT/"scripts/wsmctl.py"),"--adb",ADB,"--serial","emulator-5554","--su","/data/local/tmp/su","--timeout","12"]+cmd.split(),capture_output=True,text=True,encoding="utf-8",timeout=30)
    try:return json.loads(q.stdout)
    except ValueError:raise RuntimeError(q.stderr+q.stdout)
def pid():return int(shell("pidof com.kakaogames.gdts").split()[0])
def memory(address,count,p=None):
    if p is None:p=pid()
    data=shell("/data/adb/magisk/busybox dd if=/proc/%d/mem bs=1 skip=%d count=%d 2>/dev/null | /data/adb/magisk/busybox base64 -w0"%(p,address,count),True)
    b=base64.b64decode(data);assert len(b)==count,(hex(address),len(b),count);return b
def symbols():
    p=pid();maps=shell("cat /proc/%d/maps"%p,True)
    candidates=[int(line.split('-')[0],16) for line in maps.splitlines() if 'memfd:wsm_payload' in line and line.split()[2]=='00000000']
    assert len(candidates)==1,candidates
    base=candidates[0];raw=subprocess.check_output([NM,"-an",str(ROOT/"build/obj/local/x86_64/libwsm_engine.so")],text=True)
    names=['g_method_flags','g_target_base','ctl_klass','ctl_mi_mod','ctl_mi_unmod','g_scene_hero','g_cls_stats','g_m_fos_applydmg','g_m_cb_cantsuper','g_m_cext_cantrig','ctl_instance']
    result={"pid":p,"engineBias":hex(base)}
    for name in names:
        line=next(x for x in raw.splitlines() if x.endswith(name+'E'))
        addr=base+int(line.split()[0],16);value=struct.unpack('<Q',memory(addr,8,p))[0]
        result[name]=hex(value)
        if name.startswith('ctl_mi') or name.startswith('g_m_'):
            if value:result[name+'.method']=memory(value,80,p).hex();result[name+'.code']=hex(struct.unpack('<Q',memory(value,8,p))[0])
    return result
if __name__=='__main__':print(json.dumps(symbols(),indent=2))
