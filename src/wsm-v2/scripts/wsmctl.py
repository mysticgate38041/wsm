"""Private, correlated WSM 6 command transport and read-only Android diagnostics."""
import argparse,json,re,shlex,subprocess,sys,time
from pathlib import Path
CMD="/data/user/0/com.kakaogames.gdts/files/.7d1b0c33aa94e6f28e5b10c4d9a2f607"
ACK="/data/user/0/com.kakaogames.gdts/files/.7d1b0c33aa94e6f28e5b10c4d9a2f608"
NONCE_PATH="/data/adb/modules/wsm_gt/.nonce"
M64=(1<<64)-1
def splitmix(state):
    state=(state+0x9E3779B97F4A7C15)&M64
    z=state
    z=((z^(z>>30))*0xBF58476D1CE4E5B9)&M64
    z=((z^(z>>27))*0x94D049BB133111EB)&M64
    return state,(z^(z>>31))&M64
def xor_stream(nonce,data):
    out=bytearray(len(data));s=nonce
    for i in range(len(data)):
        s,r=splitmix(s);out[i]=data[i]^(r&0xFF)
    return bytes(out)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--adb",required=True);p.add_argument("--serial",required=True)
    p.add_argument("--su",default="su");p.add_argument("--timeout",type=float,default=40)
    p.add_argument("--diagnostics",action="store_true");p.add_argument("--output",type=Path)
    p.add_argument("command",nargs="*",default=[]);a=p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_./-]+",a.su):p.error("invalid su path")
    if not re.fullmatch(r"[A-Za-z0-9._:-]+",a.serial):p.error("invalid adb serial")
    if not 1<=a.timeout<=180:p.error("timeout must be 1..180 seconds")
    prefix=[a.adb,"-s",a.serial]
    def adb(args,data=None):
        r=subprocess.run(prefix+args,input=data,text=True,encoding="utf-8",errors="replace",capture_output=True,timeout=min(a.timeout,20))
        if r.returncode:raise RuntimeError(r.stderr.strip() or r.stdout.strip())
        return r.stdout.strip()
    def shell(command,root=False,data=None):
        if root:command=shlex.quote(a.su)+" -c "+shlex.quote(command)
        return adb(["shell",command],data)
    nonce_cache=[None]
    def channel_nonce(refresh=False):
        if nonce_cache[0] is not None and not refresh:return nonce_cache[0]
        try:
            raw=shell("cat "+shlex.quote(NONCE_PATH),root=True)
            if re.fullmatch(r"[0-9a-fA-F]{16}",raw):
                nonce_cache[0]=int(raw,16);return nonce_cache[0]
        except (RuntimeError,OSError):pass
        nonce_cache[0]=None;return None
    def request(command):
        token=time.time_ns()
        cmd_q=shlex.quote(CMD);ack_q=shlex.quote(ACK)
        line="@"+str(token)+" "+command
        nonce=channel_nonce()
        payload=("E1:"+xor_stream(nonce,line.encode("utf-8")).hex()) if nonce is not None else line
        write="umask 077; [ ! -L "+cmd_q+" ] || exit 1; cat > "+cmd_q+" && chown \"$(stat -c '%u:%g' /data/user/0/com.kakaogames.gdts)\" "+cmd_q+" && chmod 0600 "+cmd_q
        shell(write,True,payload+"\n")
        deadline=time.monotonic()+a.timeout
        while time.monotonic()<deadline:
            try:
                reply=shell("cat "+ack_q,True)
                if reply.startswith("E1:"):
                    decoded=None
                    for refresh in (False,True):
                        key=channel_nonce(refresh=refresh)
                        if key is None:continue
                        try:
                            decoded=xor_stream(key,bytes.fromhex(reply[3:])).decode("utf-8")
                            break
                        except (ValueError,UnicodeDecodeError):decoded=None
                    if decoded is None:
                        time.sleep(.15);continue
                    reply=decoded
                parsed=json.loads(reply)
                if parsed.get("request")==token:return parsed
            except (ValueError,RuntimeError):pass
            time.sleep(.15)
        raise TimeoutError("No correlated acknowledgement; check target process and WSM logs")
    if a.diagnostics:
        report={"serial":a.serial,"utc":time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime()),"runtimeVerdict":"NOT_INFERRED_FROM_LOGS"}
        for key,cmd in {"api":"getprop ro.build.version.sdk","abis":"getprop ro.product.cpu.abilist","package":"dumpsys package com.kakaogames.gdts | grep -E 'versionName|versionCode'","process":"pidof com.kakaogames.gdts","logs":"logcat -d -t 500 -v threadtime WSM:I WSMEngine:I WSM-H64:I '*:S'"}.items():
            try:report[key]=shell(cmd)
            except Exception as e:report[key]={"error":str(e)}
    else:
        command=" ".join(a.command) or "status"
        if len(command)>=192 or any(ord(c)<32 for c in command):p.error("command too long or contains control characters")
        report=request(command)
        deadline=time.monotonic()+a.timeout
        while report.get("state")=="accepted" and time.monotonic()<deadline:
            time.sleep(.2);report=request("result "+str(report["id"]))
        if report.get("state")=="accepted":report["transportState"]="timeout; outcome may still be pending"
    text=json.dumps(report,ensure_ascii=False,indent=2)+"\n"
    if a.output:
        a.output.parent.mkdir(parents=True,exist_ok=True)
        with a.output.open("x",encoding="utf-8") as f:f.write(text)
    print(text,end="")
    return 0 if a.diagnostics or report.get("state") not in {"rejected","fault","expired","stale"} else 2
if __name__=="__main__":
    try:sys.exit(main())
    except (OSError,RuntimeError,TimeoutError,subprocess.TimeoutExpired) as e:print(str(e),file=sys.stderr);sys.exit(1)
