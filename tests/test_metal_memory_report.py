from paths import ROOT
import importlib.util
import unittest
spec=importlib.util.spec_from_file_location('metal_memory_report',ROOT/'tools/macos/analyze_metal_memory.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class MetalMemoryReportTests(unittest.TestCase):
    def test_preserves_overlap_instead_of_inventing_total(self):
        report=m.analyze('''Metal memory category staging: allocated=1024 bytes, retired=64 bytes, discarded-capacity=512 bytes
Metal memory totals: footprint=4096 bytes, graphics-ledger=2048 bytes, owned=1024 bytes, guest-alias-capacity=8192 bytes
Guest memory categories: direct-live=8000 bytes, imported-free-retained=192 bytes, direct-release-calls=2
''')
        self.assertEqual(report['process_footprint_gib'],4096/1024**3)
        self.assertEqual(report['latest']['categories']['staging']['allocated'],1024)
        self.assertTrue(report['direct_liveness_available'])
    def test_joins_matching_snapshot_and_discards_incomplete_tail(self):
        report=m.analyze('''Metal memory category staging: allocated=1 bytes
Metal memory totals: footprint=100 bytes
Guest memory categories: direct-release-calls=2
Metal memory category staging: allocated=2 bytes
Metal memory totals: footprint=90 bytes
Guest memory categories: direct-release-calls=5
Metal memory category staging: allocated=10000 bytes
''')
        self.assertEqual(report['snapshot_count'],2)
        self.assertEqual(report['footprint_change_bytes'],-10)
        self.assertEqual(report['latest']['categories']['staging']['allocated'],2)
        self.assertEqual(report['direct_release_calls_since_first_snapshot'],3)
    def test_old_probe_unknown_liveness_is_not_zero_free_memory(self):
        report=m.analyze('Metal memory totals: footprint=123 bytes\nGuest memory categories: snapshot ABI unavailable; imported liveness unknown\n')
        self.assertFalse(report['direct_liveness_available'])
    def test_old_log_is_explicitly_rejected(self):
        with self.assertRaisesRegex(ValueError,'No categorized memory'):
            m.analyze('Metal memory budget: 11000 MiB\n')

    def test_internal_generations_are_attached_to_matching_snapshot(self):
        report=m.analyze("""Metal memory category sampled-textures: allocated=100 bytes
Metal guest association sampled-textures: live=20 bytes, released=30 bytes, reused=10 bytes, unknown=40 bytes
Guest resource lifetime heap: allocations=7, releases=3, live-bytes=123, unknown-frees=2, dropped=0
Guest resource reclaim: images=1, allocation-bytes=90, gpu-pending=4
Metal memory totals: footprint=1000 bytes
Guest memory categories: direct-live=500 bytes
Metal guest association sampled-textures: live=9999 bytes
""")
        snapshot=report['latest']
        self.assertEqual(snapshot['associations']['sampled-textures']['released'],30)
        self.assertEqual(snapshot['internal_gpu_allocators']['heap']['unknown-frees'],2)
        self.assertEqual(snapshot['internal_gpu_allocators']['reclaim']['allocation-bytes'],90)
        self.assertEqual(report['process_footprint_gib'],1000/1024**3)

    def test_two_level_capacity_is_diagnostic_not_additional_footprint(self):
        report=m.analyze("""GPU BDA page table: mode=two-level, allocated=5570560 bytes, root=262144 bytes, leaves=80, flat-equivalent=1073741824 bytes
Metal memory totals: footprint=1234 bytes
""")
        self.assertEqual(report['latest']['bda_page_table']['mode'],'two-level')
        self.assertEqual(report['latest']['bda_page_table']['leaves'],80)
        self.assertEqual(report['process_footprint_gib'],1234/1024**3)
