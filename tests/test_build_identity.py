from paths import ROOT
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

with patch.object(sys, 'path', [str(ROOT/'tools/macos'), *sys.path]):
    from build_identity import manifest_revision, source_revision

class BuildIdentityTests(unittest.TestCase):
    revision = '72f014b38c04daf9a1df1045008f57389d762fca'
    def test_source_identity_matches_current_tree(self):
        if (ROOT/'.git').exists():
            actual=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
        else:
            actual=json.loads((ROOT/'SOURCE-MANIFEST.json').read_text())['source_commit']
        self.assertEqual(source_revision(ROOT),actual)
    def test_export_uses_its_own_manifest_not_parent_git_repository(self):
        with tempfile.TemporaryDirectory() as d:
            parent=Path(d);subprocess.run(['git','init','-q',str(parent)],check=True)
            root=parent/'export';root.mkdir()
            (root/'SOURCE-MANIFEST.json').write_text(json.dumps({'source_commit':self.revision}))
            self.assertEqual(source_revision(root),self.revision)
    def test_legacy_manifest_selects_project_revision(self):
        self.assertEqual(manifest_revision([{'component':'metal','revision':'a'*40},{'component':'blood-echoes','revision':self.revision}]),self.revision)
    def test_unknown_or_malformed_identity_fails_required_export(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'SOURCE-MANIFEST.json').write_text('{"source_commit":"0240"}')
            self.assertIsNone(source_revision(root))
            result=subprocess.run([sys.executable,str(ROOT/'tools/macos/build_identity.py'),str(root),'--required'],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('source revision unavailable',result.stderr)
