"""Trace all original design controls to implementation and static API evidence.

Static matches are candidates, never runtime acceptance. This generator reads
the existing catalog with SQLite mode=ro and emits deterministic release inputs.
"""
from pathlib import Path
import argparse, ast, hashlib, json, re, sqlite3

ROOT = Path(__file__).resolve().parents[1]
DESIGN = ROOT.parents[1] / "premium_menu_design/src/data.ts"
DB = ROOT.parents[1] / "analysis/gt354-api/catalog-final/guardian_tales_354.sqlite"
EXPECTED = "god hp stam mana poise cd ult immune ohk dmg crit critdmg dura gbreak ammo parry aspd reach gold gem items craft unlockeq upg weight loot exp sp skills mastery rep speed noclip fly fall quest lootesp enemyesp freecam fov dumb aggro freeze onehp drop steal timescale".split()

# backend, implementation status, actual scope / missing contract, method selectors
ROWS = {
"god": ("god", "partial", "Opsi Immortal/Invincible; semua sumber damage dan lingkungan belum terukur.", ["AddCharacterStatsOption", "RemoveCharacterStatsOption"]),
"hp": ("hp", "partial", "Opsi Immortal; belum mengisi HP penuh seperti desain.", ["get_Hp", "set_Hp", "get_MaxHpWoMod"]),
"stam": ("stam", "partial", "Refill periodik, bukan bukti stamina tidak pernah berkurang.", ["set_Stamina"]),
"mana": ("mana", "partial", "Refill periodik, semua jenis resource belum terukur.", ["set_Mana"]),
"poise": ("poise", "partial", "Opsi anti knockback/stun/knockdown; efek setiap status belum terukur.", ["AddCharacterStatsOption"]),
"cd": ("nocd", "partial", "Tiga gate skill dipatch; cakupan semua skill/item belum terbukti.", ["CanTriggerSuperBattleAction", "CanTriggerBattleAction", "Oak.IMythBattleAction.get_CooltimeLeft"]),
"ult": (None, "not_implemented", "Mana refill belum membuktikan ultimate/rage instan; meter dan kontrak target perlu ditentukan.", ["get_ManaFill", "SetUltimateBtnActiveState"]),
"immune": ("immune", "partial", "Mask opsi karakter; cakupan semua status desain belum terbukti.", ["AddCharacterStatsOption"]),
"ohk": ("ohk", "partial", "Auto-kill pulse radius, bukan setiap hit senjata; boss/immune belum terukur.", ["Damage"]),
"dmg": ("dmg", "partial", "Power khusus pulse 1–99 ×100.000; bukan multiplier semua serangan 2–9999.", ["get_AttackDamageMultiplier"]),
"crit": ("crit", "partial", "Flag critical hanya pada DamageInfo pulse, bukan semua hit.", ["get_CriticalChanceWoMult"]),
"critdmg": ("critdmg", "prototype", "Getter hero ×1–5, slot 26, relokasi ADR/ADRP; efek damage belum diuji pada game.", ["get_CriticalMultiplierScale"]),
"dura": (None, "target_absent", "Durability ditemukan pada GuildMeteor; tidak ditemukan kontrak durability senjata/armor.", ["get_Durability", "set_Durability"]),
"gbreak": (None, "not_implemented", "Belum ada kontrak guard/posture yang membuktikan satu-hit break.", ["GuardBreak"]),
"ammo": (None, "not_implemented", "MagazineSize/MaxBullet ditemukan; reload, konsumsi per action dan ownership hero belum dibuktikan.", ["get_MagazineSize", "get_MaxBullet", "set_MaxBullet"]),
"parry": (None, "not_implemented", "ActivateDodge tersedia untuk FugitiveCharacterController; timing/parry universal belum dibuktikan.", ["ActivateDodge"]),
"aspd": (None, "not_implemented", "Speed gerak tidak mengubah attack speed; kontrak animasi belum tersedia.", ["get_AttackSpeed"]),
"reach": (None, "not_implemented", "ScaleHitbox memerlukan nama hitbox, ownership dan restore; belum dibuktikan untuk serangan hero.", ["ScaleHitbox", "RemoveScaleHitbox"]),
"gold": (None, "authority_unverified", "Tidak ada kontrak server atau bukti transaksi persisten; perubahan tampilan bukan infinite gold.", ["get_Gold", "set_Gold"]),
"gem": (None, "authority_unverified", "Tidak ada kontrak server atau bukti transaksi persisten untuk premium currency.", ["get_Gem", "set_Gem"]),
"items": (None, "authority_unverified", "Ownership, konsumsi dan persistensi inventori belum dibuktikan.", ["ConsumeItem"]),
"craft": (None, "authority_unverified", "Validasi material/transaksi crafting belum tersedia.", ["Craft"]),
"unlockeq": (None, "authority_unverified", "Ownership perlengkapan dan transaksi unlock belum dibuktikan.", ["UnlockEquipment"]),
"upg": (None, "authority_unverified", "Kontrak upgrade, biaya dan persistensi belum dibuktikan.", ["Upgrade"]),
"weight": (None, "target_absent", "Tidak ditemukan mekanik kapasitas beban yang sesuai desain; jumlah slot berbeda dari berat.", ["get_MaxWeight"]),
"loot": ("loot", "partial", "Permintaan consume tanpa radius 5–200m; kandidat terakhir 0, penerimaan item belum terukur.", ["FindAndSetConsumeTarget", "set_AutoConsumeOnDropped", "set_PickFlyDistance"]),
"exp": (None, "authority_unverified", "Kontrak reward EXP dan persistensi leveling belum tersedia.", ["AddExp"]),
"sp": (None, "target_absent", "Awakening/tree ditemukan; belum ada kontrak poin skill/atribut tak terbatas yang sesuai desain.", ["get_SkillPoint", "set_SkillPoint"]),
"skills": (None, "authority_unverified", "Kontrak unlock kemampuan dan persistensi belum dibuktikan.", ["UnlockSkill"]),
"mastery": (None, "not_implemented", "Mekanik weapon mastery desain belum dipetakan ke target.", ["get_WeaponMastery"]),
"rep": (None, "target_absent", "Tidak ditemukan kontrak reputation/faction rank yang sesuai desain.", ["get_Reputation", "set_Reputation"]),
"speed": ("speed", "partial", "Walk/dash/soft-dash hero ×1–5; desain ×1–10. Pengukuran terdahulu sekitar ×2,09 hanya baseline.", ["get_WalkSpeed", "get_DashSpeed", "get_SoftDashSpeed"]),
"noclip": (None, "not_implemented", "Teleport relatif tidak sama dengan no-clip; collision/restore belum dipetakan.", ["get_Collider"]),
"fly": (None, "not_implemented", "API Jump ada; belum ada loop terbang/air-jump dan restore fisika yang teruji.", ["Jump"]),
"fall": (None, "not_implemented", "Damage guard belum membuktikan semua fall/environment damage.", ["FallDamage"]),
"quest": (None, "authority_unverified", "Kontrak quest/door/script dan persistensi belum tersedia.", ["CompleteQuest"]),
"lootesp": (None, "not_implemented", "List drop saja belum menyediakan projection, rarity dan overlay ESP teruji.", ["get_DropManager"]),
"enemyesp": (None, "not_implemented", "List musuh ada; projection/chams, lifetime dan overlay belum diimplementasikan.", ["GetAllMonsters"]),
"freecam": (None, "not_implemented", "StageCamera memiliki API kamera; ownership, update Unity dan restore belum diuji.", ["get_Camera", "get_LookAtPosition"]),
"fov": ("fov", "prototype", "OverrideDefaultCameraSize/ResetDefaultCameraSize via Stage.get_StageCamera (v6.4, eksperimental): ukuran kamera orthographic (bukan FOV perspektif); efek visual belum diukur.", ["OverrideDefaultCameraSize", "ResetDefaultCameraSize", "get_FieldOfView"]),
"dumb": ("stunall", "partial", "Gate pemilihan battle action AI; belum membuktikan semua boss tidak menyerang.", ["PickNTriggerBattleAction"]),
"aggro": ("aggro", "partial", "Jalur publik dibuka (v6.4, eksperimental): ResetAggro sweep per-beat aktif lewat gate terproteksi; efektivitas penuh dan perilaku boss belum terukur di runtime.", ["GetBattleFor", "ResetAggro"]),
"freeze": (None, "not_implemented", "Gate attack AI tidak menghentikan seluruh animasi; kontrak animasi enemy-only belum tersedia.", ["PickNTriggerBattleAction"]),
"onehp": ("onehp", "partial", "Ledger pulse per-scene, prioritas OHK; hasil semua tipe musuh belum terukur.", ["Damage", "get_Hp"]),
"drop": (None, "authority_unverified", "Kontrak RNG reward dan rarity/persistensi belum tersedia.", ["DropItem"]),
"steal": (None, "target_absent", "Temuan steal berasal dari skrip naratif; tidak membuktikan mekanik peluang mencuri.", ["Steal"]),
"timescale": ("timescale", "prototype", "Mod/Unmod milik wsm + resolver instance; versi baru belum diuji pada game.", ["Mod", "Unmod", "get_Instance"]),
}

def write_changed(path, content):
    """Keep generated-input mtimes stable when bytes are unchanged."""
    if not path.is_file() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8", newline="\n")


def parse_design():
    category = None
    result = []
    for line in DESIGN.read_text(encoding="utf-8").splitlines():
        if "code:" in line and "label:" in line and re.search(r"id: '([^']+)'", line):
            category = re.search(r"id: '([^']+)'", line).group(1)
        if "{ id:" not in line or "name:" not in line:
            continue
        # Values in this source are scalar literals; parse strings, never eval TS.
        fields = {}
        for key, raw in re.findall(r"(\w+):\s*('(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\"|true|false|-?\d+(?:\.\d+)?)", line):
            fields[key] = raw == "true" if raw in ("true", "false") else ast.literal_eval(raw)
        fields["category"] = category
        result.append(fields)
    if [r["id"] for r in result] != EXPECTED or set(ROWS) != set(EXPECTED):
        raise ValueError("Design changed: review the complete 47-feature mapping")
    return result

def load_snapshot(path):
    document = json.loads(path.read_text(encoding="utf-8"))
    design = parse_design()
    if document.get("schema") != 1 or document.get("requested") != 47 or document.get("completed") != 0:
        raise ValueError("Unqualified catalog snapshot")
    if document.get("designSha256") != hashlib.sha256(DESIGN.read_bytes()).hexdigest():
        raise ValueError("Snapshot does not match the design source")
    features = document.get("features", [])
    if [f.get("id") for f in features] != EXPECTED:
        raise ValueError("Snapshot does not contain all original feature IDs")
    for original, feature in zip(design, features):
        if any(feature.get(key) != value for key, value in original.items()):
            raise ValueError("Snapshot design semantics changed: " + original["id"])
        backend, status, reason, selectors = ROWS[original["id"]]
        if (feature.get("backendId"), feature.get("status"), feature.get("reason")) != (backend, status, reason):
            raise ValueError("Snapshot implementation mapping changed: " + original["id"])
        if feature.get("runtimeAcceptance") != "UNVERIFIED" or [e.get("selector") for e in feature.get("evidence", [])] != selectors:
            raise ValueError("Snapshot evidence/qualification changed: " + original["id"])
    if not re.fullmatch(r"[0-9a-f]{64}", document.get("apiSha256", "")):
        raise ValueError("Missing API provenance hash")
    return document

def build(snapshot=False):
    if snapshot:
        document = load_snapshot(ROOT / "features/feature_catalog.json")
        emit(document)
        return
    features = parse_design()
    with sqlite3.connect(DB.as_uri() + "?mode=ro", uri=True) as db:
        db.row_factory = sqlite3.Row
        for feature in features:
            backend, status, reason, selectors = ROWS[feature["id"]]
            evidence = []
            for selector in selectors:
                rows = db.execute("SELECT t.full_name AS type,m.name,m.declaration,m.rva,m.line FROM methods m JOIN types t ON t.id=m.type_id WHERE m.name=? ORDER BY t.full_name,m.line", (selector,)).fetchall()
                evidence.append({"selector": selector, "matchCount": len(rows), "matches": [dict(r) for r in rows[:12]], "truncated": len(rows) > 12})
            feature.update(backendId=backend, status=status, reason=reason, runtimeAcceptance="UNVERIFIED", evidence=evidence)
    document = {"schema": 1, "requested": 47, "completed": 0,
        "acceptance": "All original semantics require measured target runtime acceptance; static matches and applied ACKs do not establish gameplay effect.",
        "designSha256": hashlib.sha256(DESIGN.read_bytes()).hexdigest(),
        "apiSha256": hashlib.sha256(DB.read_bytes()).hexdigest(), "features": features}
    audit_path=ROOT.parents[1]/"analysis/dump-source-audit/audit.json"
    if audit_path.is_file():
        audit=json.loads(audit_path.read_text(encoding="utf-8"))
        document["originalDump"]={"root":audit["sourceRoot"],"target":audit["target"],
            "auditSha256":hashlib.sha256(audit_path.read_bytes()).hexdigest(),
            "provenancePass":sum(p["pass"] for p in audit["provenance"]),"inputs":len(audit["provenance"]),
            "criticalGetter":[m for m in audit["native"] if m["name"]=="get_CriticalMultiplierScale"]}
    emit(document)

def emit(document):
    features = document["features"]
    out = ROOT / "features/feature_catalog.json"
    out.parent.mkdir(exist_ok=True)
    write_changed(out, json.dumps(document, ensure_ascii=False, indent=2) + "\n")
    java = "package wsm;\n// Generated by scripts/build_feature_catalog.py; no simulated toggles.\nfinal class FeatureCatalog {\n    static final String[][] ROWS={\n"
    for f in features:
        values = [f["id"], f["name"], f["category"], f["status"], f["reason"], f["backendId"] or ""]
        java += "        {" + ",".join(json.dumps(v, ensure_ascii=True) for v in values) + "},\n"
    java += "    };\n}\n"
    write_changed(ROOT / "menu/FeatureCatalog.java", java)
    pages = [{"requested": 47, "completed": 0, "page": i, "pages": 12, "features": [{k: f[k] for k in ("id", "name", "status", "backendId", "reason")} for f in features[i*4:i*4+4]]} for i in range(12)]
    header = "#pragma once\n// Generated bounded read-only catalog pages.\nstatic const char *const WSM_CATALOG_PAGES[]={\n"
    for page in pages:
        value = json.dumps(page, ensure_ascii=True, separators=(",", ":"))
        if len(value.encode()) >= 4096: raise ValueError("Catalog page exceeds native response capacity")
        header += json.dumps(value) + ",\n"
    write_changed(ROOT / "jni/wsm_feature_catalog.h", header + "};\n")
    print(json.dumps({"requested": len(features), "statuses": {s: sum(f["status"] == s for f in features) for s in sorted({f["status"] for f in features})}}, indent=2))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", action="store_true", help="Validate committed evidence and generate inputs without the external SQLite dump; does not re-audit the dump")
    build(parser.parse_args().snapshot)
