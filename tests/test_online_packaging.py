"""Corresponding networking source must come from pinned pristine archives, not build trees."""
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/macos'))
from package_online import export_online_sources


class OnlinePackagingTests(unittest.TestCase):
    def test_pinned_cached_archives_export_sources_and_record_digests(self):
        pins = json.loads((ROOT/'gpu/bbnet/DEPENDENCIES.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); cache = root/'cache'; cache.mkdir()
            for name, pin in pins.items():
                with tarfile.open(cache/(name+'-'+pin['commit']+'.tar.gz'), 'w:gz') as archive:
                    for relative, body in [('LICENSE',b'fixture license'),('src/library.c',b'fixture source')]:
                        entry = tarfile.TarInfo(name+'-'+pin['commit']+'/'+relative)
                        entry.size = len(body); archive.addfile(entry, io.BytesIO(body))
            records = export_online_sources(root/'source', cache)
            self.assertEqual(len(records),4)
            for record in records:
                name = record['component'].split('/')[-1]
                self.assertEqual(record['revision'],pins[name]['commit'])
                self.assertEqual(len(record['archive_sha256']),64)
                self.assertEqual((root/'source'/name/'src/library.c').read_bytes(),b'fixture source')
                self.assertTrue((root/'source'/name/'LICENSE').is_file())

    def test_source_pins_match_dependency_build_script(self):
        script=(ROOT/'tools/macos/setup_online_deps.sh').read_text()
        pins=json.loads((ROOT/'gpu/bbnet/DEPENDENCIES.json').read_text())
        for pin in pins.values():
            self.assertIn(pin['repository']+' '+pin['commit'],script)

if __name__=='__main__':unittest.main()
