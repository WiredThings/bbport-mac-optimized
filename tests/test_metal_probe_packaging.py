"""Regression: a renderer/launcher update must retain enhanced game runtime services."""
from paths import ROOT
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

with patch.object(sys, 'path', [str(ROOT/'tools/macos'), *sys.path]):
    from package_metal import select_metal_probe


class MetalProbePackagingTests(unittest.TestCase):
    def test_update_prefers_enhanced_runtime_over_base(self):
        with tempfile.TemporaryDirectory() as d:
            app=Path(d)/'Existing.app';binary=app/'Contents/Resources/bbport/bin';binary.mkdir(parents=True)
            (binary/'bb-probe').write_bytes(b'base runtime without benchmark services')
            enhanced=b'BB_PAD_FILE_ONLY BB_BENCHMARK_MUTE runtime_memory_snapshot_v1 audio changes'
            (binary/'bb-probe-metal').write_bytes(enhanced)
            selected=select_metal_probe(app)
            self.assertEqual(selected.read_bytes(),enhanced)
            self.assertEqual(selected.name,'bb-probe-metal')

    def test_explicit_verified_recovery_can_override_bad_baseline(self):
        with tempfile.TemporaryDirectory() as d:
            app=Path(d)/'Existing.app';binary=app/'Contents/Resources/bbport/bin';binary.mkdir(parents=True)
            (binary/'bb-probe-metal').write_bytes(b'old base accidentally copied into Metal path')
            recovery=Path(d)/'enhanced-probe';recovery.write_bytes(b'isolated input and memory ABI')
            self.assertEqual(select_metal_probe(app,recovery),recovery)

    def test_legacy_app_without_metal_probe_can_use_its_base(self):
        with tempfile.TemporaryDirectory() as d:
            app=Path(d)/'Legacy.app';binary=app/'Contents/Resources/bbport/bin';binary.mkdir(parents=True)
            (binary/'bb-probe').write_bytes(b'legacy')
            self.assertEqual(select_metal_probe(app).read_bytes(),b'legacy')

    def test_missing_explicit_probe_never_silently_falls_back(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(FileNotFoundError):select_metal_probe(Path(d),Path(d)/'missing')
            with self.assertRaises(FileNotFoundError):select_metal_probe(Path(d))
