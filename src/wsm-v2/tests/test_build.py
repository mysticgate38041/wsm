"""Build cache invalidation and generated-input drift are release correctness gates."""
import importlib.util
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def load(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
menu=load('build_menu');config=load('generate_build_config')
class BuildTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.source=self.root/'source';self.output=self.root/'output'
        self.source.write_bytes(b'source');self.output.write_bytes(b'compiled')
        self.cache=menu.BuildCache(self.root/'cache.json');self.options={'mode':'release'}
        self.cache.save([self.source],[self.output],self.options)
    def valid(self):return self.cache.valid([self.source],[self.output],self.options)
    def test_unchanged_cache(self):self.assertTrue(self.valid())
    def test_changed_source(self):self.source.write_bytes(b'changed');self.assertFalse(self.valid())
    def test_tampered_output(self):self.output.write_bytes(b'changed');self.assertFalse(self.valid())
    def test_missing_output(self):self.output.unlink();self.assertFalse(self.valid())
    def test_changed_build_options(self):self.options['mode']='debug';self.assertFalse(self.valid())
    def test_added_source(self):
        extra=self.root/'extra';extra.write_bytes(b'extra')
        self.assertFalse(self.cache.valid([self.source,extra],[self.output],self.options))
    def test_corrupt_receipt(self):self.cache.path.write_text('{');self.assertFalse(self.valid())
    def test_extra_output(self):
        extra=self.root/'extra.class';extra.write_bytes(b'stale')
        self.assertFalse(self.cache.valid([self.source],[self.output,extra],self.options))
    def test_cleanup_rejects_external_path(self):
        with self.assertRaises(ValueError):menu.reset_directory(self.root)
        self.assertTrue(self.source.is_file())
    def test_generated_build_inputs_match(self):config.generate(check=True)
    def test_generation_preserves_unchanged_input_timestamp(self):
        path=self.root/'generated.h';path.write_text('same',encoding='utf-8')
        before=path.stat().st_mtime_ns
        with patch.object(config,'ROOT',self.root),patch.object(config,'outputs',return_value={'generated.h':'same'}):
            config.generate()
            self.assertEqual(path.stat().st_mtime_ns,before)
        with patch.object(config,'ROOT',self.root),patch.object(config,'outputs',return_value={'generated.h':'new'}):
            config.generate()
            self.assertEqual(path.read_text(encoding='utf-8'),'new')
if __name__=='__main__':unittest.main()
