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

    def test_render_parts_and_copy_reason(self):
        def window(every):
            return f"""Metal GPU timing: seconds=5, frames=100, every={every}, invalid=0, capacity-drops=0, busy-drops=0, encoders=200, barriers=10, cpu-boundary-ms=3, cpu-commit-ms=1
Metal GPU stage: name=guest-render, samples=2, sampled-ms=2
Metal GPU render parts: name=guest-render, vertex-samples=2, sampled-vertex-ms=0.3, fragment-samples=1, sampled-fragment-ms=0.5, paired-samples=1, sampled-overlap-ms=0.1, sampled-stage-gap-ms=1.3
Metal GPU hot pass: label=960x540 C6 D vs abc ps def, samples=2, sampled-ms=2, vertex-samples=2, sampled-vertex-ms=0.3, fragment-samples=1, sampled-fragment-ms=0.5, paired-samples=1, sampled-overlap-ms=0.1, sampled-stage-gap-ms=1.3
Metal scene copy: uid=42, mip=0, direction=resolve, reason=transfer-write, source=960x540, target=1920x1080, format=70, calls=20
Metal overwrite discard: candidates=100, dirty=50, skipped-resolves=25
"""
        report = module.analyze(window(8) + window(4))
        part = report['render_parts']['guest-render']
        self.assertEqual(part['vertex_samples'], 4)
        self.assertEqual(part['fragment_samples'], 2)
        self.assertAlmostEqual(part['estimated_vertex_ms_per_frame'], 3.6 / 200)
        self.assertAlmostEqual(part['estimated_fragment_ms_per_frame'], 6 / 200)
        self.assertAlmostEqual(part['estimated_overlap_ms_per_frame'], 1.2 / 200)
        self.assertAlmostEqual(part['estimated_stage_gap_ms_per_frame'], 15.6 / 200)
        hot = report['hot_passes_by_span'][0][1]
        self.assertEqual(hot['render_parts_available'], True)
        self.assertAlmostEqual(hot['estimated_fragment_ms_per_frame'], 6 / 200)
        row = report['scene_copy_rows'][0]
        self.assertEqual((row['source'], row['target'], row['calls']), ('960x540', '1920x1080', 40))
        self.assertEqual(row['reason'], 'transfer-write')
        self.assertEqual(report['overwrite_last']['skipped-resolves'], 25)

    def test_no_game_frames_rejected(self):
        with self.assertRaises(ValueError):
            module.analyze('Metal GPU timing: enabled, every=8\n')
        with self.assertRaises(ValueError):
            module.analyze('Metal GPU timing: seconds=5, frames=0\n')


if __name__ == '__main__':
    unittest.main()
