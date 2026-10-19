import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('online', ROOT/'scripts/online.py')
online = importlib.util.module_from_spec(spec); spec.loader.exec_module(online)


class OnlineRoutingTests(unittest.TestCase):
    def test_shared_save_routing_stays_outside_save_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            user = Path(directory)
            target = user/"routes"
            path = online.prepare(user, output=target)
            self.assertTrue(path.is_file())
            self.assertEqual(list(user.iterdir()), [target])

    def test_shared_routing_covers_regions_without_persisting_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            user = Path(directory)
            path = online.prepare(user)
            data = json.loads(path.read_text())
            self.assertEqual(data['*.scej-network.jp'], 'https://thehuntersdream.com')
            self.assertEqual(data['bb-playlog-prod.s3.amazonaws.com'], 'https://thehuntersdream.com:18671')
            self.assertNotIn('token', path.read_text().lower())

    def test_link_cannot_overwrite_another_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); user = root/'user'; user.mkdir()
            original = root/'original'; original.write_text('unchanged')
            (user/'host_overrides.json').symlink_to(original)
            with self.assertRaises(ValueError): online.prepare(user)
            self.assertEqual(original.read_text(), 'unchanged')

    def test_custom_endpoints_and_https_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            user = Path(directory)
            path = online.prepare(user, 'https://game.example.test:20443', 'https://np.example.test:18671')
            routes = json.loads(path.read_text())
            self.assertEqual(routes['*.scej-network.jp'], 'https://game.example.test:20443')
            self.assertEqual(routes['bb-playlog-prod.s3.amazonaws.com'], 'https://np.example.test:18671')
            for value in ['http://example.test','https://user:secret@example.test','https://example.test/path','https://example.test:0']:
                with self.assertRaises(ValueError): online.prepare(user, value)

    def test_online_forces_network_choice_without_changing_offline_policy(self):
        spec = importlib.util.spec_from_file_location('patches', ROOT/'scripts/patches.py')
        patches = importlib.util.module_from_spec(spec); spec.loader.exec_module(patches)
        self.assertFalse(patches.skip_network_choice({'BB_ONLINE':'1','BB_SKIP_NETWORK_CHOICE':'1'}))
        self.assertTrue(patches.skip_network_choice({'BB_ONLINE':'0','BB_SKIP_NETWORK_CHOICE':'1'}))

    def test_online_rejects_offline_patch_addresses_even_when_renamed(self):
        spec = importlib.util.spec_from_file_location('patches', ROOT/'scripts/patches.py')
        patches = importlib.util.module_from_spec(spec); spec.loader.exec_module(patches)
        xml = ROOT/'patches/Bloodborne.xml'; segments = [(0, 0x10000000)]
        for name in [patches.SKIP_NETWORK_CHOICE, 'Disable HTTP Requests']:
            writes = patches.compile_patches(xml, [name], '01.09', segments)
            with self.assertRaisesRegex(ValueError, 'offline-only patch'):
                patches.validate_online_writes(writes, xml, '01.09', segments, {'BB_ONLINE':'1'})
            patches.validate_online_writes(writes, xml, '01.09', segments, {'BB_ONLINE':'0'})
        for name in [*patches.FPS_PRESETS['60'], patches.UNLOCK_GAME_REGION]:
            writes = patches.compile_patches(xml, [name], '01.09', segments)
            patches.validate_online_writes(writes, xml, '01.09', segments, {'BB_ONLINE':'1'})

if __name__ == '__main__': unittest.main()
