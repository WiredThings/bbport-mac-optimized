# SPDX-License-Identifier: GPL-2.0-or-later
from paths import ROOT
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('translation', ROOT / 'scripts/chinese_translation.py')
translation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(translation)
import mods


class ChineseTranslationTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.data = self.root / 'managed'

    def archive(self, prefix='', language='11', extra=None, omit=None):
        archive = self.root / 'translation.zip'
        with zipfile.ZipFile(archive, 'w') as output:
            for name in translation.FILES:
                if name != omit:
                    output.writestr(prefix + name.removeprefix('dvdroot_ps4/'), b'community ' + name.encode())
            locale = 'zhocn' if language == '11' else 'zhotw'
            output.writestr(prefix + f'menu/{locale}/font.gfx', b'locale marker')
            if extra:
                for name, data in extra:
                    output.writestr(name, data)
        return archive

    def test_zip_and_folder_import_are_private_and_integrity_checked(self):
        archive = self.archive(prefix='GOTY/')
        original = hashlib.sha256(archive.read_bytes()).hexdigest()
        report = translation.import_package(archive, self.data, '11')
        folder = Path(report['folder'])
        self.assertTrue(report['valid'])
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), original)
        self.assertEqual({str(p.relative_to(folder)) for p in folder.rglob('*') if p.is_file()},
                         {*translation.FILES, translation.MANIFEST})
        duplicate = translation.import_package(folder, self.data, '11')
        self.assertNotEqual(duplicate['folder'], report['folder'])
        (folder / translation.FILES[0]).write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'integrity'):
            translation.inspect(folder, '11')

    def test_simplified_and_traditional_are_independent_and_mismatch_rejected(self):
        cn = translation.import_package(self.archive(), self.data, '11')
        with self.assertRaisesRegex(ValueError, 'other Chinese'):
            translation.import_package(self.archive(), self.data, '10')
        tw = translation.import_package(self.archive(language='10'), self.data, '10')
        self.assertTrue(translation.inspect(cn['folder'], '11')['valid'])
        self.assertTrue(translation.inspect(tw['folder'], '10')['valid'])
        with self.assertRaisesRegex(ValueError, 'selected Chinese'):
            translation.inspect(cn['folder'], '10')

    def test_incomplete_and_wrong_non_goty_packages_fail_atomically(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete GOTY'):
            translation.import_package(self.archive(omit=translation.FILES[0]), self.data, '11')
        self.assertEqual(list(self.data.iterdir()), [])
        archive = self.root / 'generic.zip'
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('menu/zhocn/font.gfx', b'not English GOTY')
        with self.assertRaisesRegex(ValueError, 'Incomplete GOTY'):
            translation.import_package(archive, self.data, '11')

    def test_archive_traversal_duplicates_and_symlinks_rejected(self):
        for path in ['../outside', '/absolute', 'menu/../outside', 'C:/outside', 'menu\\outside']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                translation.import_package(self.archive(extra=[(path, b'x')]), self.data, '11')
        path = 'MENU/ENGUS/FONT.GFX'
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            translation.import_package(self.archive(extra=[(path, b'x')]), self.data, '11')
        archive = self.archive()
        with zipfile.ZipFile(archive, 'a') as output:
            info = zipfile.ZipInfo('linked')
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            output.writestr(info, '../target')
        with self.assertRaisesRegex(ValueError, 'links'):
            translation.import_package(archive, self.data, '11')

    def test_asset_size_limits_and_post_import_extra_files_rejected(self):
        saved = translation.MAX_FILE
        translation.MAX_FILE = 4
        try:
            with self.assertRaisesRegex(ValueError, 'size limit'):
                translation.import_package(self.archive(), self.data, '11')
        finally:
            translation.MAX_FILE = saved
        folder = Path(translation.import_package(self.archive(), self.data, '11')['folder'])
        (folder / 'dvdroot_ps4/extra.dcx').write_bytes(b'not allowed')
        with self.assertRaisesRegex(ValueError, 'files changed'):
            translation.inspect(folder, '11')

    def test_translation_applies_with_general_mods_disabled_without_changing_original(self):
        folder = Path(translation.import_package(self.archive(), self.data, '11')['folder'])
        game = self.root / 'game'
        game.mkdir()
        (game / 'eboot.bin').write_bytes(b'original executable')
        original = game / translation.FILES[0]
        original.parent.mkdir(parents=True)
        original.write_bytes(b'original font')
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/mods.py'), str(game),
                                 '--out', str(self.root / 'out'), '--mods-dir', str(self.root / 'mods'),
                                 '--enabled', '0', '--last-mod', str(folder)],
                                capture_output=True, text=True, check=True)
        overlay = Path(result.stdout.strip())
        self.assertEqual((overlay / translation.FILES[0]).read_bytes(),
                         (folder / translation.FILES[0]).read_bytes())
        self.assertEqual(original.read_bytes(), b'original font')
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'original executable')
        self.assertFalse((overlay / translation.MANIFEST).exists())
        # General text mods lose only their colliding text/font slots.
        result = mods.build_overlay(game, self.root / 'out', [('ordinary', game / 'dvdroot_ps4'), ('translation', folder)])
        self.assertEqual((result / translation.FILES[0]).read_bytes(),
                         (folder / translation.FILES[0]).read_bytes())

    def test_real_launch_script_routes_language_and_removes_only_temporary_overlay(self):
        folder = Path(translation.import_package(self.archive(), self.data, '11')['folder'])
        game = self.root / 'game'
        game.mkdir()
        (game / 'eboot.bin').write_bytes(b'original')
        original = game / translation.FILES[0]
        original.parent.mkdir(parents=True)
        original.write_bytes(b'original font')
        runtime = self.root / 'runtime'
        scripts = runtime / 'scripts'
        scripts.mkdir(parents=True)
        shutil.copy2(ROOT / 'run.sh', runtime / 'run.sh')
        for name in ('mods.py', 'chinese_translation.py'):
            shutil.copy2(ROOT / 'scripts' / name, scripts / name)
        # Stop before game preparation/execution, recording what the real shell hands it.
        (scripts / 'prepare.py').write_text('''import json,os,sys
from pathlib import Path
game=Path(sys.argv[1]);out=Path(sys.argv[sys.argv.index('--out')+1])
(out/'observed.json').write_text(json.dumps(dict(language=os.environ.get('BB_LANGUAGE'),
    game=str(game),font=(game/'dvdroot_ps4/menu/engus/font.gfx').read_text())))
sys.exit(42)
''')
        for source, expected in [('official', '11'), ('community', '1')]:
            data = self.root / source
            env = {k: v for k, v in os.environ.items() if not k.startswith('BB_')}
            env.update(PYTHON=sys.executable, BB_PREBUILT='1', BB_DATA_DIR=str(data),
                       BB_GAME_DIR=str(game), BB_MODS_ENABLED='0', BB_LANGUAGE='11',
                       BB_CHINESE_SOURCE=source, BB_CHINESE_LANGUAGE='11',
                       BB_CHINESE_TRANSLATION_DIR=str(folder))
            result = subprocess.run(['/bin/bash', str(runtime / 'run.sh')], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 42, result.stderr)
            observed = json.loads((data / 'out/observed.json').read_text())
            self.assertEqual(observed['language'], expected)
            self.assertEqual(observed['font'], 'original font' if source == 'official'
                             else 'community dvdroot_ps4/menu/engus/font.gfx')
            if source == 'community':
                self.assertFalse(Path(observed['game']).exists())
            self.assertEqual(original.read_bytes(), b'original font')
        # Broken selected packages must stop, rather than falling back to untranslated English.
        (folder / translation.FILES[0]).unlink()
        (data / 'out/observed.json').unlink()
        result = subprocess.run(['/bin/bash', str(runtime / 'run.sh')], env=env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse((data / 'out/observed.json').exists())
