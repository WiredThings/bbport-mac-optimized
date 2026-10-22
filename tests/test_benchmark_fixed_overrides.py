import importlib.util
from pathlib import Path
import unittest
import contextlib
import io
import json
import sys
import tempfile
from unittest.mock import patch

s=importlib.util.spec_from_file_location('candidates',Path(__file__).parents[1]/'tools/macos/benchmark_candidates.py')
c=importlib.util.module_from_spec(s);s.loader.exec_module(c)

class FixedOverrideTests(unittest.TestCase):
    def test_heap_history_candidate_is_a_fixed_boolean_override(self):
        self.assertEqual(c.fixed_overrides(['BB_LIFETIME_HEAP_HISTORY_PRIORITY=1'],
                                         ['BB_MTL_RELEASE_WARM_ARCHIVE']),
                         {'BB_LIFETIME_HEAP_HISTORY_PRIORITY': '1'})
        with self.assertRaises(ValueError):
            c.fixed_overrides(['BB_LIFETIME_HEAP_HISTORY_PRIORITY=2'],
                              ['BB_MTL_RELEASE_WARM_ARCHIVE'])

    def test_candidate_child_keeps_rate_deadline_and_shared_budget(self):
        with tempfile.TemporaryDirectory() as d:
            argv=['candidates','--workspace',d,'--app','/unused','--name','forward',
                  '--swap-rapid-window-seconds','30','--stop-at','2099-01-01T17:00:00+08:00']
            with patch.object(sys,'argv',argv),patch.object(c.b,'pressure',return_value=dict(level=1,swap_mib=10000)),\
                 patch.object(c.subprocess,'run',side_effect=RuntimeError('intercept before game launch')) as child,\
                 contextlib.redirect_stdout(io.StringIO()),self.assertRaisesRegex(RuntimeError,'intercept'):
                c.main()
            cmd=child.call_args.args[0]
            self.assertEqual(cmd[cmd.index('--swap-rapid-window-seconds')+1],'30')
            self.assertEqual(cmd[cmd.index('--swap-budget-baseline-mib')+1],'10000')
            self.assertEqual(c.b.authorization_deadline(cmd[cmd.index('--stop-at')+1]),c.b.authorization_deadline(argv[-1]))
            report=json.loads((Path(d)/'forward.json').read_text())
            self.assertEqual(report['swap_rapid_window_seconds'],30)
            self.assertEqual(report['shared_swap_budget_baseline_mib'],10000)

    def test_common_budget_controls_stay_distinct_from_selected_flag(self):
        self.assertEqual(c.fixed_overrides(['BB_MTL_PERSIST_WARM_ARCHIVE=0','BB_MTL_INLINE_UPDATES=0'],['BB_MTL_RELEASE_WARM_ARCHIVE']),
                         {'BB_MTL_PERSIST_WARM_ARCHIVE':'0','BB_MTL_INLINE_UPDATES':'0'})

    def test_refuse_conflicts_and_diagnostic_score_overrides(self):
        for values in (['BB_MTL_RELEASE_WARM_ARCHIVE=1'],['BB_MTL_COMPILE_AUDIT=1'],
                       ['BB_MTL_INLINE_UPDATES=2'],['BB_MTL_INLINE_UPDATES=0','BB_MTL_INLINE_UPDATES=1']):
            with self.subTest(values=values),self.assertRaises(ValueError):
                c.fixed_overrides(values,['BB_MTL_RELEASE_WARM_ARCHIVE'])
