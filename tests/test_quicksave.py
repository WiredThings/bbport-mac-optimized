import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / 'tools/macos/quicksave.py'
spec = importlib.util.spec_from_file_location('quicksave', MODULE)
q = importlib.util.module_from_spec(spec)
spec.loader.exec_module(q)


class QuicksaveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.user = Path(self.tmp.name).resolve()
        self.live = self.user / 'savedata/1/CUSA00207'
        (self.live / 'SPRJ0005').mkdir(parents=True)
        (self.live / 'SPRJ0005.sce_sys').mkdir()
        (self.live / 'SPRJ0005.sce_sys/param.bin').write_bytes(b'metadata')
        self.character = self.live / 'SPRJ0005/userdata0000'
        self.character.write_bytes(b'checkpoint character')
        (self.live / 'SPRJ0005/userdata0010').write_bytes(b'game data')
        self.saves = q.Checkpoints(self.user, 'CUSA00207')

    def test_round_trip_repeated_loads_preserve_progress_and_checkpoint(self):
        original = q.inventory(self.live)
        slot = self.saves.save()
        for i in range(2):
            self.character.write_bytes(f'progress {i}'.encode())
            newer = q.inventory(self.live)
            backup = self.saves.load()
            self.assertEqual(q.inventory(backup / 'data'), newer)
            self.assertEqual(q.inventory(self.live), original)
            self.assertEqual(q.inventory(slot / 'data'), original)

    def test_load_is_rejected_in_online_host_without_changing_progress(self):
        self.saves.save()
        self.character.write_bytes(b'online progress')
        before = q.inventory(self.live)
        with patch.dict(os.environ, {'BB_ONLINE': '1'}):
            with self.assertRaisesRegex(ValueError, 'offline restart'): self.saves.load()
        self.assertEqual(q.inventory(self.live), before)
        self.assertFalse((self.user / '.quickload-offline.json').exists())

    def test_successful_load_leaves_durable_offline_intent_for_launcher_recovery(self):
        self.saves.save()
        self.character.write_bytes(b'new progress')
        with patch.dict(os.environ, {'BB_ONLINE': '0'}): self.saves.load()
        self.assertEqual(json.loads((self.user / '.quickload-offline.json').read_text()), {'version': 1})

    def test_missing_checkpoint_does_not_change_live_save(self):
        before = q.inventory(self.live)
        with self.assertRaisesRegex(ValueError, 'No quicksave'):
            self.saves.load()
        self.assertEqual(q.inventory(self.live), before)

    def test_second_save_replaces_active_checkpoint_for_every_load(self):
        first = self.saves.save()
        self.character.write_bytes(b'new location after walking')
        second_state = q.inventory(self.live)
        second = self.saves.save()
        self.assertNotEqual(first, second)
        self.assertEqual(self.saves.check()[0], second)
        for i in range(2):
            self.character.write_bytes(f'later progress {i}'.encode())
            displaced = q.inventory(self.live)
            backup = self.saves.load()
            self.assertEqual(q.inventory(backup / 'data'), displaced)
            self.assertEqual(q.inventory(self.live), second_state)
            self.assertEqual(self.saves.check()[0], second)
        self.assertEqual((first / 'data/SPRJ0005/userdata0000').read_bytes(),
                         b'checkpoint character')

    def test_corrupt_checkpoint_rejected_before_replacing_progress(self):
        slot = self.saves.save()
        (slot / 'data/SPRJ0005/userdata0000').write_bytes(b'corrupt')
        before = q.inventory(self.live)
        with self.assertRaisesRegex(ValueError, 'verification'):
            self.saves.load()
        self.assertEqual(q.inventory(self.live), before)

    def test_partial_autosave_rejected_old_quicksave_retained(self):
        slot = self.saves.save()
        (self.live / 'SPRJ0005/userdata0000.123.bbtmp').write_bytes(b'partial')
        with self.assertRaisesRegex(ValueError, 'unfinished'):
            self.saves.save()
        self.assertEqual(self.saves.check()[0], slot)

    def test_copy_failure_keeps_previous_checkpoint(self):
        slot = self.saves.save()
        with patch.object(q.shutil, 'copytree', side_effect=OSError('disk full')):
            with self.assertRaises(OSError):
                self.saves.save()
        self.assertEqual(self.saves.check()[0], slot)

    def test_live_mutation_during_copy_does_not_replace_quicksave(self):
        slot = self.saves.save()
        copy = shutil.copytree
        def changed(source, destination, *args, **kwargs):
            result = copy(source, destination, *args, **kwargs)
            if source == self.live:
                self.character.write_bytes(b'concurrent write')
            return result
        with patch.object(q.shutil, 'copytree', side_effect=changed):
            with self.assertRaisesRegex(ValueError, 'changed'):
                self.saves.save()
        self.assertEqual(self.saves.check()[0], slot)

    def test_rename_failure_restores_previous_progress(self):
        self.saves.save()
        self.character.write_bytes(b'newer progress')
        before = q.inventory(self.live)
        rename = Path.rename
        def failed(path, destination):
            if path.name.startswith('restore-') and destination == self.live:
                raise OSError('rename interrupted')
            return rename(path, destination)
        with patch.object(Path, 'rename', failed):
            with self.assertRaises(OSError):
                self.saves.load()
        self.assertEqual(q.inventory(self.live), before)
        self.assertFalse(self.saves.journal.exists())

    def test_crash_between_renames_recovered_on_next_launch(self):
        slot = self.saves.save()
        after = q.inventory(slot / 'data')
        self.character.write_bytes(b'newer progress')
        before = q.inventory(self.live)
        name = 'before-load-' + 'a' * 32
        backup = self.saves.root / name
        backup.mkdir()
        q.atomic_json(self.saves.journal, {'backup': name, 'before': before, 'after': after})
        self.live.rename(backup / 'data')
        self.saves.recover()
        self.assertEqual(q.inventory(self.live), before)
        self.assertFalse(self.saves.journal.exists())

    def test_symlink_and_path_traversal_rejected(self):
        (self.live / 'SPRJ0005/link').symlink_to(self.character)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.saves.save()
        with self.assertRaisesRegex(ValueError, 'Invalid game title'):
            q.Checkpoints(self.user, '../other')


if __name__ == '__main__':
    unittest.main()
