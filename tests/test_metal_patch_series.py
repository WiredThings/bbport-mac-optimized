from paths import ROOT
import difflib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('apply_metal_patches', ROOT/'tools/macos/apply_metal_patches.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class MetalPatchSeriesTests(unittest.TestCase):
    def fixture(self, directory):
        root = Path(directory)/'source';root.mkdir()
        subprocess.run(['git','init','-q',str(root)],check=True)
        (root/'file').write_text('base\n')
        subprocess.run(['git','add','file'],cwd=root,check=True)
        subprocess.run(['git','-c','user.name=fixture','-c','user.email=fixture@example.invalid','commit','-qm','base'],cwd=root,check=True)
        patches=[]
        for n,a,b in ((1,'base\n','first\n'),(2,'first\n','second\n')):
            p=Path(directory)/f'{n}.patch'
            p.write_text(''.join(difflib.unified_diff(a.splitlines(True),b.splitlines(True),fromfile='a/file',tofile='b/file')))
            patches.append(p)
        return root,patches

    def test_overlapping_patches_and_repeat_are_idempotent(self):
        with tempfile.TemporaryDirectory() as d:
            root,patches=self.fixture(d)
            self.assertEqual(module.apply_series(root,patches),(0,2))
            self.assertEqual((root/'file').read_text(),'second\n')
            self.assertEqual(module.apply_series(root,patches),(2,2))

    def test_known_partial_prefix_and_unrelated_edits(self):
        with tempfile.TemporaryDirectory() as d:
            root,patches=self.fixture(d)
            subprocess.run(['git','apply',str(patches[0])],cwd=root,check=True)
            (root/'unrelated').write_text('preserved')
            self.assertEqual(module.apply_series(root,patches),(1,2))
            self.assertEqual((root/'unrelated').read_text(),'preserved')

    def test_unknown_target_edits_are_preserved_and_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root,patches=self.fixture(d)
            (root/'file').write_text('user edit\n')
            with self.assertRaisesRegex(RuntimeError,'unrecognized edits'):
                module.apply_series(root,patches)
            self.assertEqual((root/'file').read_text(),'user edit\n')
