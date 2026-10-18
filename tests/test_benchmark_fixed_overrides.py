import importlib.util
from pathlib import Path
import unittest

s=importlib.util.spec_from_file_location('candidates',Path(__file__).parents[1]/'tools/macos/benchmark_candidates.py')
c=importlib.util.module_from_spec(s);s.loader.exec_module(c)

class FixedOverrideTests(unittest.TestCase):
    def test_common_budget_controls_stay_distinct_from_selected_flag(self):
        self.assertEqual(c.fixed_overrides(['BB_MTL_PERSIST_WARM_ARCHIVE=0','BB_MTL_INLINE_UPDATES=0'],['BB_MTL_RELEASE_WARM_ARCHIVE']),
                         {'BB_MTL_PERSIST_WARM_ARCHIVE':'0','BB_MTL_INLINE_UPDATES':'0'})

    def test_refuse_conflicts_and_diagnostic_score_overrides(self):
        for values in (['BB_MTL_RELEASE_WARM_ARCHIVE=1'],['BB_MTL_COMPILE_AUDIT=1'],
                       ['BB_MTL_INLINE_UPDATES=2'],['BB_MTL_INLINE_UPDATES=0','BB_MTL_INLINE_UPDATES=1']):
            with self.subTest(values=values),self.assertRaises(ValueError):
                c.fixed_overrides(values,['BB_MTL_RELEASE_WARM_ARCHIVE'])
