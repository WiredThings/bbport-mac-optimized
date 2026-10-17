import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('metal_gpu', Path(__file__).parents[1] / 'tools/macos/analyze_metal_gpu.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GpuReportTest(unittest.TestCase):
    def test_sampling_weights_and_warmup(self):
        def window(frames, every, ms, samples):
            return f'''Metal GPU timing: seconds=5.000, frames={frames}, sampled-cb=20, every={every}, invalid=0, capacity-drops=0, busy-drops=0, encoders=500, barriers=50, fence-waits=100, cpu-boundary-ms=10, cpu-commit-ms=2, probe-encoders=4, sampled-cb-ms=5, sampled-covered-ms=4, sampled-uncovered-ms=1; spans may overlap
Metal GPU stage: name=velocity-depth, samples={samples}, sampled-ms={ms}, mean-us=1, max-us=2, estimated-ms/frame=99
Metal GPU hot pass: label=960x540 C1 D vs abc ps def, samples=2, sampled-ms=0.5, estimated-ms/frame=0.4; first pipeline of pass
Metal GPU transitions: 1>2=4 2>1=3 (1=render 2=compute 3=blit)
'''
        report = module.analyze(window(10, 8, 1000, 1) + window(100, 8, 5, 20) + window(200, 4, 10, 40), 1)
        self.assertEqual(report['frames'], 300)
        self.assertEqual(report['hot_passes_by_span'][0][0], '960x540 C1 D vs abc ps def')
        self.assertAlmostEqual(report['hot_passes_by_span'][0][1]['estimated_ms_per_frame'], 6 / 300)
        stage = report['stages_by_span'][0]
        self.assertEqual(stage[0], 'velocity-depth')
        self.assertAlmostEqual(stage[1]['estimated_ms_per_frame'], 80 / 300)
        self.assertEqual(stage[1]['samples'], 60)
        self.assertAlmostEqual(stage[1]['mean_us_per_encoder'], 250)
        self.assertEqual(report['windows'][0]['transitions']['1>2'], 4)
        self.assertAlmostEqual(report['cpu_boundary_ms_per_frame'], 20 / 300)

    def test_no_game_frames_rejected(self):
        with self.assertRaises(ValueError):
            module.analyze('Metal GPU timing: enabled, every=8\n')
        with self.assertRaises(ValueError):
            module.analyze('Metal GPU timing: seconds=5, frames=0\n')


if __name__ == '__main__':
    unittest.main()
