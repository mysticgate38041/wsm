"""Read-only original dump inventory, provenance and WSM candidate evidence."""
import collections, hashlib, json, os, re, sqlite3, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
DUMP=Path("C:/Users/Administrator/Downloads/Mod/gt_dump")
API=ROOT/"analysis/gt354-api/guardian-tales-3.54.0-api-map"
OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(API))
from build_api_map import Elf,digest

# Broader names are discovery candidates, not silently mapped implementations.
PATTERNS={
    "god":["Invincib","Immortal"],"hp":["Heal","Hp","Health"],"stam":["Stamina"],"mana":["Mana"],
    "poise":["Knockback","Stagger","Armor"],"cd":["Cooltime","Cooldown","CanTrigger"],"ult":["Ultimate","SuperBattle","Rage"],
    "immune":["Ailment","Immun","Stun"],"ohk":["Damage","Kill","Die"],"dmg":["AttackDamage","DamageMult","AttackModifier"],
    "crit":["CriticalChance","Critical"],"critdmg":["CriticalMultiplier","CriticalMult"],"dura":["Durability","Durable"],
    "gbreak":["Guard","Posture","Break"],"ammo":["Ammo","Bullet","Ammunition","Magazine"],"parry":["Parry","Dodge","Evade","Avoid"],
    "aspd":["AttackSpeed","ActionSpeed","AnimationSpeed","AnimatorSpeed"],"reach":["Hitbox","HitBox","AttackRange","Hittable"],
    "gold":["Gold","Money","Currency"],"gem":["Gem","Diamond","Jewel"],"items":["ConsumeItem","Inventory","ItemCount"],
    "craft":["Craft","Recipe","Material","Forge"],"unlockeq":["Equipment","Weapon","Unlock"],"upg":["Upgrade","Enhance","Evolve"],
    "weight":["Weight","Capacity","MaxSlot"],"loot":["ConsumeTarget","PickFly","DropItem","Looter"],
    "exp":["Exp","Experience"],"sp":["SkillPoint","AttributePoint","Awakening"],"skills":["UnlockSkill","Awakening","Ability"],
    "mastery":["Mastery","WeaponLevel"],"rep":["Reputation","Faction","Renown"],"speed":["WalkSpeed","DashSpeed"],
    "noclip":["Collision","Collider","WallPass","WallPassing"],"fly":["Jump","Flying","AirMove"],"fall":["FallDamage","FallingDamage"],
    "quest":["Quest","Requirement","Condition"],"lootesp":["DropManager","WorldToScreen","PickFly"],
    "enemyesp":["GetAllMonsters","WorldToScreen","Renderer"],"freecam":["StageCamera","CameraPosition","LookAtPosition"],
    "fov":["FieldOfView","CameraSize","CameraFov"],"dumb":["BattleAction","PickNTrigger"],"aggro":["Aggro","Targetable","Detect"],
    "freeze":["AnimationSpeed","AnimatorSpeed","Freeze","PauseAnimation"],"onehp":["ChangeHpToFixedValue","set_HP"],
    "drop":["DropRate","Reward","Rarity"],"steal":["Steal","PickPocket"],"timescale":["TimeScale","GlobalTimeManager"],
}
SELECTED=[("Oak.CharacterStatsBehaviour",n) for n in ["get_CriticalMultiplierScale","get_CriticalChanceWoMult","get_WalkSpeed","get_DashSpeed","get_SoftDashSpeed"]]
SELECTED += [("GlobalTimeManager",n) for n in ["get_Instance","Mod","Unmod"]]
SELECTED += [("Oak.FieldObjectStatsBehaviour",n) for n in ["ChangeHpToFixedValue","get_HP","get_MaxHpWoMod"]]

def main():
    inventory=[];groups=collections.defaultdict(lambda:{"files":0,"bytes":0});extensions=collections.Counter()
    for directory,dirs,files in os.walk(DUMP):
        dirs.sort()
        for name in sorted(files):
            path=Path(directory)/name;relative=path.relative_to(DUMP).as_posix();size=path.stat().st_size
            role="/".join(relative.split("/")[:2]) if "/" in relative else "root"
            groups[role]["files"]+=1;groups[role]["bytes"]+=size;extensions[path.suffix.lower()]+=1
            inventory.append({"path":relative,"bytes":size})
    (OUT/"inventory.json").write_text(json.dumps({"root":str(DUMP),"files":inventory,"groups":groups,"extensions":dict(extensions)},indent=2),encoding="utf-8")
    print("Inventory",len(inventory),"files",sum(r["bytes"] for r in inventory),"bytes",flush=True)
    provenance=[]
    summary=json.loads((API/"catalog-final/summary.json").read_text(encoding="utf-8"))
    for source in summary["inputs"]:
        path=Path(source["path"]);actual=digest(path) if path.is_file() else None
        provenance.append({**source,"actualSha256":actual,"pass":actual==source["sha256"] and path.stat().st_size==source["bytes"]})
    print("Provenance",sum(r["pass"] for r in provenance),"/",len(provenance),flush=True)
    lua=[]
    for path in sorted((DUMP/"apk_base/assets/GameScript").rglob("*.lua")):
        content=path.read_text(encoding="utf-8-sig");lines=content.splitlines()
        lua.append({"path":path.relative_to(DUMP).as_posix(),"sha256":digest(path),"bytes":path.stat().st_size,"lines":len(lines),
            "managedReferences":sorted(set(re.findall(r"CS\.[A-Za-z_][\w.]*",content))),
            "featureLines":[{"line":i,"text":line.strip()} for i,line in enumerate(lines,1) if re.search(r"critical|damage_info|publish_damage|cooltime|attack_speed|hittable|hitbox",line,re.I)][:160]})
    manifest=json.loads((DUMP/"raw/manifest.json").read_text(encoding="utf-8"))
    elf=Elf(DUMP/"out/carve/libil2cpp_reloc_norela.so")
    raw=Elf(DUMP/"apk_config/lib/arm64-v8a/libil2cpp.so")
    db=sqlite3.connect((API/"catalog-final/guardian_tales_354.sqlite").as_uri()+"?mode=ro",uri=True);db.row_factory=sqlite3.Row
    features=[]
    for feature,terms in PATTERNS.items():
        where=" OR ".join(["(m.name LIKE ? OR t.full_name LIKE ?)" for _ in terms]);params=["%"+term+"%" for term in terms for _ in range(2)]
        where="("+where+") AND (t.namespace='Oak' OR t.full_name LIKE 'UnityEngine.%' OR t.full_name='GlobalTimeManager')"
        total=db.execute("SELECT count(*) FROM methods m JOIN types t ON t.id=m.type_id WHERE "+where,params).fetchone()[0]
        matches=[dict(r) for r in db.execute("SELECT t.full_name AS owner,m.name,m.declaration,m.rva,m.line FROM methods m JOIN types t ON t.id=m.type_id WHERE "+where+" ORDER BY t.full_name,m.line LIMIT 100",params)]
        features.append({"id":feature,"terms":terms,"count":total,"candidates":matches,"truncated":total>100,"qualification":"DISCOVERY_ONLY"})
    native=[]
    objdump="D:/Android/ndk/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-objdump.exe"
    for owner,name in SELECTED:
        for method in db.execute("SELECT m.* FROM methods m JOIN types t ON t.id=m.type_id WHERE t.full_name=? AND m.name=?",(owner,name)):
            row=dict(method);rva=row["rva"]
            if not rva:continue
            offset=elf.offset(rva,32);raw_offset=raw.offset(rva,32)
            words=struct.unpack_from("<4I",elf.data,offset)
            pc_relative=[i for i,w in enumerate(words) if (w & 0x1f000000)==0x10000000]
            row.update(owner=owner,fileOffsetVerified=offset,rawFileOffset=raw_offset,first16=elf.data[offset:offset+16].hex(),
                originalCodeMatches=raw.data[raw_offset:raw_offset+32]==elf.data[offset:offset+32],addressInstructionPositions=pc_relative)
            disasm=subprocess.run([objdump,"-d","--start-address="+hex(rva),"--stop-address="+hex(rva+160),str(elf.path)],capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=30)
            if disasm.returncode:raise RuntimeError(disasm.stderr)
            destination=OUT/(owner.replace(".","_")+"-"+name+".asm.txt")
            destination.write_text(disasm.stdout,encoding="utf-8");row["disassembly"]=destination.name;native.append(row)
    db.close()
    result={"sourceRoot":str(DUMP),"requestedPathExists":(DUMP.parent/"gt-dump").exists(),"sourceReadOnly":True,
        "inventoryFiles":len(inventory),"inventoryBytes":sum(r["bytes"] for r in inventory),"groups":groups,
        "target":{k:manifest[k] for k in ["package_name","version_name","version_code","min_sdk_version","target_sdk_version"]},
        "catalogCounts":summary["counts"],"provenance":provenance,"lua":lua,"featureDiscovery":features,"native":native,
        "dumperAttributeErrors":sum("ERROR: Error while restoring attributeIndex" in line for line in (DUMP/"out/dumper_final.log").read_text(encoding="utf-8").splitlines()),
        "limits":["Inventory includes every physical file; does not mean every media pixel, third-party SDK method or binary body was semantically decompiled.","Empty dump.cs bodies and DummyDlls contain declarations, not recovered C# implementation.","Raw ARM64 instruction prefixes were compared with relocation copy; execution and game effects require separate tests.","Broad name matches are candidates, not feature implementations or proof of absence."]}
    (OUT/"audit.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"files":len(inventory),"provenancePass":sum(r["pass"] for r in provenance),"inputs":len(provenance),"gameplayLuaFiles":len(lua),"nativeMethods":len(native),"featureCoverage":len(features)},indent=2))
if __name__=="__main__":main()
