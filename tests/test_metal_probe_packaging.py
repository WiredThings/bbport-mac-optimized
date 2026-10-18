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

    def test_development_probe_is_rejected_even_if_explicit(self):
        with tempfile.TemporaryDirectory() as d:
            probe=Path(d)/'dev-probe';probe.write_bytes(b'BB_DEVELOPMENT_WORLD_WARPER_V1')
            with self.assertRaisesRegex(ValueError,'Development'):select_metal_probe(Path(d),probe)

    def test_release_payload_rejects_development_marker_and_probe(self):
        from package_optimized import inspect_payload
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);probe=root/'bb-probe-metal';probe.write_bytes(b'BB_DEVELOPMENT_WORLD_WARPER_V1')
            with self.assertRaises(AssertionError):inspect_payload(root)
            self.assertEqual(len(inspect_payload(root,source=True)),1)
            probe.write_bytes(b'production');(root/'DEVELOPMENT-WORLD-WARPER.json').write_text('{}')
            with self.assertRaises(AssertionError):inspect_payload(root)

    def test_development_baseline_rejected_before_creating_release_output(self):
        import package_metal
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);base=root/'Developer.app';engine=base/'Contents/Resources/bbport';engine.mkdir(parents=True)
            (engine/'DEVELOPMENT-WORLD-WARPER.json').write_text('{}');app=root/'Release.app'
            argv=['package_metal','--baseline',str(base),'--gpu-build',str(root),
                  '--launcher',str(root),'--spirv-cross-source',str(root),'--app',str(app),'--output',str(root/'out')]
            with patch.object(sys,'argv',argv),self.assertRaisesRegex(ValueError,'production baseline'):
                package_metal.main()
            self.assertFalse(app.exists())

    def test_public_app_strips_local_automation_but_keeps_runtime_scripts(self):
        from package_optimized import inspect_payload, strip_development_tools
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name in ('tools/macos/run_optimized.sh','tools/macos/quicksave.py',
                         'tools/macos/benchmark.py','tools/macos/benchmark_routes.py',
                         'tools/macos/analyze_metal_gpu.py','tools/macos/dev/world_warper.c'):
                target=root/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text('fixture')
            with self.assertRaisesRegex(AssertionError,'Development tool'):inspect_payload(root)
            removed=strip_development_tools(root)
            self.assertEqual(len(removed),4)
            files=inspect_payload(root)
            self.assertEqual({row['path'] for row in files},
                             {'tools/macos/run_optimized.sh','tools/macos/quicksave.py'})

    def test_public_app_scan_detects_nested_development_tools(self):
        from package_optimized import inspect_payload
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);target=root/'App.app/Contents/Resources/bbport/tools/macos/benchmark.py'
            target.parent.mkdir(parents=True);target.write_text('fixture')
            with self.assertRaisesRegex(AssertionError,'Development tool'):inspect_payload(root)
