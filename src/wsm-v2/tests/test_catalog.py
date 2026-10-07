"""Offline CI must reject stale design, mapping and qualification snapshots."""
import copy, importlib.util, json, tempfile, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("catalog", ROOT / "scripts/build_feature_catalog.py")
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = json.loads((ROOT / "features/feature_catalog.json").read_text(encoding="utf-8"))

    def check(self, edit=None):
        document = copy.deepcopy(self.snapshot)
        if edit: edit(document)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "snapshot.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            return catalog.load_snapshot(path)

    def test_complete_snapshot(self):
        self.assertEqual(len(self.check()["features"]), 47)

    def test_stale_design(self):
        with self.assertRaises(ValueError): self.check(lambda d: d.update(designSha256="0" * 64))

    def test_missing_feature(self):
        with self.assertRaises(ValueError): self.check(lambda d: d["features"].pop())

    def test_fake_completion(self):
        with self.assertRaises(ValueError): self.check(lambda d: d.update(completed=47))

    def test_changed_design_semantics(self):
        with self.assertRaises(ValueError): self.check(lambda d: d["features"][0].update(name="Different"))

    def test_changed_backend(self):
        with self.assertRaises(ValueError): self.check(lambda d: d["features"][0].update(backendId="fake"))

    def test_unproven_runtime(self):
        with self.assertRaises(ValueError): self.check(lambda d: d["features"][0].update(runtimeAcceptance="PASS"))

    def test_changed_selector(self):
        with self.assertRaises(ValueError): self.check(lambda d: d["features"][0]["evidence"][0].update(selector="fake"))

    def test_missing_provenance(self):
        with self.assertRaises(ValueError): self.check(lambda d: d.update(apiSha256="invalid"))

if __name__ == "__main__": unittest.main()
