"""Deterministic module packaging; refuse stale/incomplete inputs."""
from pathlib import Path
import hashlib,json,posixpath,stat,struct,sys,zipfile,zlib

ROOT=Path(__file__).resolve().parents[1]
STAMP="wsm-v6.1.0-rc2"
FEATURE_IDS="god hp stam mana poise cd ult immune ohk dmg crit critdmg dura gbreak ammo parry aspd reach gold gem items craft unlockeq upg weight loot exp sp skills mastery rep speed noclip fly fall quest lootesp enemyesp freecam fov dumb aggro freeze onehp drop steal timescale".split()
ENTRIES={"zygisk/x86_64.so":"build/libs/x86_64/libwsm_loader.so",
         "zygisk/arm64-v8a.so":"build/libs/arm64-v8a/libwsm_loader.so",
         "engine/x86_64.so":"build/libs/x86_64/libwsm_engine.so",
         "engine/arm64-v8a.so":"build/libs/arm64-v8a/libwsm_engine.so",
         "payload/arm64-v8a.so":"build/libs/arm64-v8a/libwsm_arm64.so",
         "dex/wsm_menu.dex":"menu/wsm_menu.dex"}
def sha(data):return hashlib.sha256(data).hexdigest()
def safe_zip_path(name):
    if not name or "\\" in name or name.startswith("/"):return False
    normalized=posixpath.normpath(name)
    return normalized==name and normalized not in {"", ".", ".."} and not normalized.startswith("../")
def verify(path):
    with zipfile.ZipFile(path) as z:
        if z.testzip():raise ValueError("ZIP CRC failure")
        names=z.namelist()
        for info in z.infolist():
            if not safe_zip_path(info.filename):raise ValueError("unsafe zip path: "+info.filename)
            mode=(info.external_attr>>16) & 0o170000
            if mode==stat.S_IFLNK:raise ValueError("symlink zip entry: "+info.filename)
        if len(names)!=len(set(names)):raise ValueError("duplicate entries")
        expected=set(ENTRIES)|{"module.prop","customize.sh","skip_mount","META-INF/com/google/android/update-binary","META-INF/com/google/android/updater-script","release.json","verify.list","features/feature_catalog.json"}
        if set(names)!=expected:raise ValueError("missing or unexpected module entries")
        manifest={n:h for h,n in (line.split(None,1) for line in z.read("verify.list").decode().splitlines())}
        if set(manifest)!=expected-{"verify.list"}:raise ValueError("incomplete integrity manifest")
        for n,h in manifest.items():
            if sha(z.read(n))!=h:raise ValueError("hash mismatch: "+n)
        release=json.loads(z.read("release.json"))
        if release.get("build")!=STAMP or release.get("protocol")!=3 or release.get("minApi")!=26:raise ValueError("release metadata mismatch")
        if release.get("target")!={"package":"com.kakaogames.gdts","version":"3.54.0","versionCode":423}:raise ValueError("unexpected target")
        catalog=json.loads(z.read("features/feature_catalog.json"))
        ids=[f["id"] for f in catalog["features"]]
        if catalog.get("requested")!=47 or ids!=FEATURE_IDS or catalog.get("completed")!=0:raise ValueError("incomplete or unqualified feature catalog")
        if any(f.get("runtimeAcceptance")!="UNVERIFIED" for f in catalog["features"]):raise ValueError("runtime qualification must be evidence-backed in a later release")
        if release.get("qualification")!="INCOMPLETE_47_FEATURE_SCOPE":raise ValueError("missing qualification gate")
        for n in ENTRIES:
            data=z.read(n)
            if n.endswith(".so"):
                if STAMP.encode() not in data:raise ValueError("stale build stamp: "+n)
                machine=62 if "x86_64" in n else 183
                if data[:6]!=b"\x7fELF\x02\x01" or struct.unpack_from("<H",data,18)[0]!=machine:raise ValueError("wrong ELF: "+n)
            else:
                if len(data)<112 or data[:4]!=b"dex\n" or data[4:7] not in {b"035",b"037",b"038"} or data[7]!=0:raise ValueError("invalid DEX magic/version")
                if struct.unpack_from("<III",data,32)!=(len(data),112,0x12345678):raise ValueError("invalid DEX header")
                if hashlib.sha1(data[32:]).digest()!=data[12:32] or zlib.adler32(data[12:]) & 0xffffffff != struct.unpack_from("<I",data,8)[0]:raise ValueError("invalid DEX integrity")
        return {"package":str(path),"sha256":sha(Path(path).read_bytes()),"entries":len(names),"integrity":"PASS"}
def source_hashes():
    sources={p.relative_to(ROOT).as_posix():sha(p.read_bytes()) for folder in ["jni","menu","payload","scripts","tests","features"] for p in (ROOT/folder).rglob("*") if p.suffix in {".cpp",".h",".hpp",".inc",".java",".mk",".ps1",".py",".sh",".json",".xml"}}
    sources.update({p.relative_to(ROOT).as_posix():sha(p.read_bytes()) for p in (ROOT/"module-template").rglob("*") if p.is_file()})
    return sources
def checkpoint():
    (ROOT/"build").mkdir(exist_ok=True)
    (ROOT/"build/source-inputs.json").write_text(json.dumps(source_hashes(),sort_keys=True),encoding="utf-8")
def record_build():
    sources=source_hashes()
    if sources!=json.loads((ROOT/"build/source-inputs.json").read_text(encoding="utf-8")):raise ValueError("source changed during build; rebuild required")
    receipt={"sources":sources,"binaries":{n:sha((ROOT/p).read_bytes()) for n,p in ENTRIES.items()}}
    (ROOT/"build/build-receipt.json").write_text(json.dumps(receipt,sort_keys=True),encoding="utf-8")
def build():
    source=source_hashes()
    receipt=json.loads((ROOT/"build/build-receipt.json").read_text(encoding="utf-8"))
    if receipt.get("sources")!=source:raise ValueError("source differs from verified build receipt; rebuild required")
    if receipt.get("binaries")!={n:sha((ROOT/p).read_bytes()) for n,p in ENTRIES.items()}:raise ValueError("binary differs from verified build receipt")
    files={p.relative_to(ROOT/"module-template").as_posix():p.read_bytes() for p in (ROOT/"module-template").rglob("*") if p.is_file()}
    files={n:(d.replace(b"\r\n",b"\n") if n.endswith(".sh") or n.startswith("META-INF/") else d) for n,d in files.items()}
    files.update({name:(ROOT/path).read_bytes() for name,path in ENTRIES.items()})
    files["features/feature_catalog.json"]=(ROOT/"features/feature_catalog.json").read_bytes()
    for name,data in files.items():
        if name.startswith(("zygisk/","engine/","payload/")) and STAMP.encode() not in data:raise ValueError("stale build stamp: "+name)
    files["release.json"]=(json.dumps({"build":STAMP,"protocol":3,"ndk":"27.2.12479018","target":{"package":"com.kakaogames.gdts","version":"3.54.0","versionCode":423},"minApi":26,"abis":["x86_64","arm64-v8a"],"sources":source,"qualification":"INCOMPLETE_47_FEATURE_SCOPE","evidence":{"package":"SOURCE_VERIFIED","newGameRuntime":"UNVERIFIED_TARGET"}},sort_keys=True,indent=2)+"\n").encode()
    files["verify.list"]="".join(sha(data)+"  "+name+"\n" for name,data in sorted(files.items())).encode()
    dest=ROOT/("dist/"+STAMP+".zip");dest.parent.mkdir(exist_ok=True)
    temporary=dest.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(files.items()):
            entry=zipfile.ZipInfo(name,(2026,10,7,0,0,0));entry.create_system=3;entry.compress_type=zipfile.ZIP_DEFLATED
            entry.external_attr=(0o100755 if name.endswith(".sh") or name.endswith("update-binary") else 0o100644)<<16
            z.writestr(entry,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
    result=verify(temporary);temporary.replace(dest);result["package"]=str(dest)
    dest.with_suffix(".zip.sha256").write_text(result["sha256"]+"  "+dest.name+"\n",encoding="ascii")
    print(json.dumps(result,indent=2))
if __name__=="__main__":
    if len(sys.argv)>1 and sys.argv[1]=="--checkpoint":checkpoint()
    elif len(sys.argv)>1 and sys.argv[1]=="--record-build":record_build()
    elif len(sys.argv)>1:print(json.dumps(verify(Path(sys.argv[1])),indent=2))
    else:build()
