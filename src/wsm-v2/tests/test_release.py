import contextlib,hashlib,importlib.util,io,json,tempfile,unittest,warnings,zipfile
from pathlib import Path
from unittest.mock import patch
SPEC=importlib.util.spec_from_file_location("package_release",Path(__file__).resolve().parents[1]/"scripts/package_release.py")
pkg=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(pkg)
class ReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source=pkg.ROOT/("dist/"+pkg.STAMP+".zip")
        with zipfile.ZipFile(cls.source) as z:cls.entries={n:z.read(n) for n in z.namelist()}
    def modified(self,edit,rehash=False):
        entries=dict(self.entries);edit(entries)
        if rehash:entries["verify.list"]="".join(hashlib.sha256(d).hexdigest()+"  "+n+"\n" for n,d in sorted(entries.items()) if n!="verify.list").encode()
        with tempfile.TemporaryDirectory(prefix="wsm-release-") as folder:
            path=Path(folder)/"module.zip"
            with zipfile.ZipFile(path,"w") as z:
                for n,d in entries.items():z.writestr(n,d)
            with self.assertRaises((ValueError,KeyError,json.JSONDecodeError)):pkg.verify(path)
    def test_complete_release(self):self.assertEqual(pkg.verify(self.source)["entries"],14)
    def test_incomplete_feature_catalog(self):
        def edit(e):
            c=json.loads(e["features/feature_catalog.json"]);c["features"].pop();e["features/feature_catalog.json"]=json.dumps(c).encode()
        self.modified(edit,True)
    def test_false_feature_completion(self):
        def edit(e):
            c=json.loads(e["features/feature_catalog.json"]);c["completed"]=47;e["features/feature_catalog.json"]=json.dumps(c).encode()
        self.modified(edit,True)
    def test_replaced_design_feature(self):
        def edit(e):
            c=json.loads(e["features/feature_catalog.json"]);c["features"][0]["id"]="different";e["features/feature_catalog.json"]=json.dumps(c).encode()
        self.modified(edit,True)
    def test_unproven_runtime_qualification(self):
        def edit(e):
            c=json.loads(e["features/feature_catalog.json"]);c["features"][0]["runtimeAcceptance"]="PASS";e["features/feature_catalog.json"]=json.dumps(c).encode()
        self.modified(edit,True)
    def test_incorrect_version_code(self):
        def edit(e):
            r=json.loads(e["release.json"]);r["target"]["versionCode"]=424;e["release.json"]=json.dumps(r).encode()
        self.modified(edit,True)
    def test_missing_guest_payload(self):self.modified(lambda e:e.pop("payload/arm64-v8a.so"))
    def test_native_tampering(self):self.modified(lambda e:e.__setitem__("engine/x86_64.so",e["engine/x86_64.so"]+b"tampered"))
    def test_duplicate_entry(self):
        with tempfile.TemporaryDirectory(prefix="wsm-release-") as folder:
            path=Path(folder)/"module.zip";path.write_bytes(self.source.read_bytes())
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                with zipfile.ZipFile(path,"a") as z:z.writestr("module.prop",self.entries["module.prop"])
            with self.assertRaises(ValueError):pkg.verify(path)
    def test_unexpected_entry(self):self.modified(lambda e:e.__setitem__("extra",b"x"))
    def test_incomplete_manifest(self):self.modified(lambda e:e.__setitem__("verify.list",b""))
    def test_wrong_machine_even_with_valid_hash(self):
        def edit(e):
            d=bytearray(e["engine/x86_64.so"]);d[18:20]=b"\xb7\x00";e["engine/x86_64.so"]=bytes(d)
        self.modified(edit,True)
    def test_corrupt_dex_even_with_valid_hash(self):
        def edit(e):
            d=bytearray(e["dex/wsm_menu.dex"]);d[-1]^=1;e["dex/wsm_menu.dex"]=bytes(d)
        self.modified(edit,True)
    def test_incorrect_version_gate(self):
        def edit(e):
            r=json.loads(e["release.json"]);r["target"]["version"]="0";e["release.json"]=json.dumps(r).encode()
        self.modified(edit,True)
    def test_changed_source_refuses_packaging(self):
        with patch.object(pkg,"source_hashes",return_value={"modified.cpp":"changed"}):
            with self.assertRaises(ValueError):pkg.build()
    def test_deterministic_repack(self):
        before=self.source.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):pkg.build()
        self.assertEqual(before,self.source.read_bytes())
if __name__=="__main__":unittest.main()
