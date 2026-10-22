import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('submits',Path(__file__).parents[1]/'tools/macos/analyze_gpu_submits.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)


class TimingTests(unittest.TestCase):
    def test_out_of_order_completion_must_not_drop_earlier_work(self):
        intervals=[(20,25),(10,21)]
        self.assertEqual(s.union(intervals),15)
        self.assertEqual(s.ordered_coverage(intervals),5)
    def test_overlapping_queues_are_not_double_counted(self):
        intervals=[(10,20),(12,18),(19,25),(30,31)]
        self.assertEqual(s.union(intervals),16)
        self.assertEqual(s.ordered_coverage(intervals),16)
    def test_cpu_timing_modes_cannot_be_compared_as_equal_overhead(self):
        spec=importlib.util.spec_from_file_location('benchmark',Path(__file__).parents[1]/'tools/macos/benchmark.py')
        b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
        import argparse,json,tempfile
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name,flag in [('off','0'),('on','1')]:
                (root/name).write_text(json.dumps(dict(metrics={},variants={'BB_FRAME_TIMING':flag})))
            with self.assertRaisesRegex(ValueError,'CPU timing instrumentation'):
                b.compare(argparse.Namespace(before=root/'off',after=root/'on'))
