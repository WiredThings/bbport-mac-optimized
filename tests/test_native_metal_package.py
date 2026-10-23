"""Native host packaging must not leak build paths or advertise an unusable OS."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/macos'))
import native_metal as native


class NativePayloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.paths = [self.root/'bin/gpu-metal-arm64/bb-gpu',
                      self.root/'bin/gpu-metal-arm64/libbbgpu.dylib',
                      self.root/'bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu',
                      self.root/'deps-metal-arm64/lib/libSDL3.dylib']
        for p in self.paths:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'fixture')
        self.info = dict(schema=1, default_enabled=True, minimum_os_major=27)
        self.refs = {self.paths[0]: ['@loader_path/../../deps-metal-arm64/lib/libSDL3.dylib']}
        self.addCleanup(patch.stopall)
        patch.object(native.subprocess, 'check_output', return_value='arm64\n').start()
        patch.object(native, 'minimum', return_value=(27, 0)).start()
        patch.object(native, 'loads', side_effect=lambda p: self.refs.get(p, [])).start()

    def test_resolved_local_closure_and_os_gate(self):
        self.assertEqual(native.validate_native_payload(self.root, self.info), 4)
        self.assertFalse(native.native_available(self.info, 26))
        self.assertTrue(native.native_available(self.info, 27))
        self.assertFalse(native.native_available(dict(self.info, default_enabled=False), 27))

    def test_missing_escaping_and_build_machine_dependencies_rejected(self):
        for ref in ('@loader_path/missing.dylib', '@loader_path/../../../../outside.dylib',
                    '/opt/homebrew/lib/libSDL3.dylib', '@rpath/libSDL3.dylib'):
            with self.subTest(ref=ref):
                self.refs[self.paths[0]] = [ref]
                with self.assertRaises(ValueError):
                    native.validate_native_payload(self.root, self.info)

    def test_wrong_architecture_and_newer_dependencies_rejected(self):
        with patch.object(native.subprocess, 'check_output', return_value='x86_64\n'):
            with self.assertRaisesRegex(ValueError, 'Non-native'):
                native.validate_native_payload(self.root, self.info)
        with patch.object(native, 'minimum', return_value=(28, 0)):
            with self.assertRaisesRegex(ValueError, 'minimum OS'):
                native.validate_native_payload(self.root, self.info)

    def test_symlinked_dependency_rejected(self):
        p = self.paths[-1]
        p.unlink()
        p.symlink_to(self.paths[1])
        with self.assertRaisesRegex(ValueError, 'Invalid native package'):
            native.validate_native_payload(self.root, self.info)

if __name__ == '__main__':
    unittest.main()
