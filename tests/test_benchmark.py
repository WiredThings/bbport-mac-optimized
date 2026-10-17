import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('benchmark',Path(__file__).parents[1]/'tools/macos/benchmark.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)

class BenchmarkTests(unittest.TestCase):
    def rows(self):
        return [dict(t_s=str(i*.02),frame_ms='20',draws='800',dispatches='5',compiles='0',gpu_ms='17',rec_busy_ms='9',present_wait_ms='0',tick_wait_ms='0',cam_x='-9',cam_y='-4',cam_z='-18') for i in range(1000)]
    def test_warm_game_metrics_and_one_percent_low(self):
        r=self.rows();r[0]['frame_ms']='100'
        m=b.metrics(r);self.assertAlmostEqual(m['fps'],1000/20.08);self.assertAlmostEqual(m['one_percent_low_fps'],1000/28);self.assertEqual(m['frame_ms_p95'],20)
    def test_loading_and_menu_not_accepted(self):
        for key,value in [('draws','20'),('cam_x','0')]:
            r=self.rows()
            if key=='cam_x':
                for row in r:row.update(cam_x='0',cam_y='0',cam_z='0')
            else:
                for row in r:row[key]=value
            with self.assertRaises(ValueError):b.metrics(r)
    def test_3d_title_preview_is_not_a_game_scene(self):
        r=self.rows()
        for row in r:row.update(cam_x='0',cam_y='4',cam_z='-8',draws='550')
        self.assertFalse(b.stable_scene(r));self.assertFalse(b.is_world(r[-1]))
    def test_one_transition_frame_never_enters_warmup(self):
        r=self.rows()
        for row in r[:-1]:row.update(cam_x='0',cam_y='4',cam_z='-8')
        self.assertFalse(b.stable_scene(r,[-9,-4,-18]))
        self.assertTrue(b.stable_scene(self.rows(),[-9,-4,-18]))
        self.assertFalse(b.stable_scene(self.rows(),[7,2,8]))
    def test_low_recorder_count_keeps_slow_frame_in_score(self):
        r=self.rows();r[0].update(draws='20',frame_ms='100')
        m=b.metrics(r);self.assertEqual(m['frames'],1000);self.assertAlmostEqual(m['fps'],1000/20.08);self.assertEqual(m['low_draw_frame_fraction'],.001)
        self.assertTrue(b.stable_scene(r,[-9,-4,-18]))
        for row in r[-30:]:row['draws']='20'
        self.assertFalse(b.stable_scene(r))
    def test_nonfinite_and_zero_frame_times_rejected(self):
        for invalid in ['nan','0','-1','inf']:
            r=self.rows();r[0]['frame_ms']=invalid
            with self.assertRaises(ValueError):b.metrics(r)
    def test_incomplete_csv_row_ignored(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'frames.csv';p.write_text('draws,cam_x,cam_y,cam_z\n800,-9,-4,-18\n800,-9,')
            self.assertEqual(len(b.frame_rows(p)),1)
    def test_save_inventory_rejects_symlinks_and_detects_changes(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'save').write_text('original');old=b.inventory(p);(p/'save').write_text('modified');self.assertNotEqual(old,b.inventory(p));(p/'link').symlink_to(p/'save')
            with self.assertRaises(ValueError):b.inventory(p)
    def result(self):
        return dict(valid=True,cohort_fingerprint='same',profile_gpu=False,system={'model':'M4'},camera=[-9,-4,-18],metrics=b.metrics(self.rows()))
    def compare(self,a,c):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'a').write_text(json.dumps(a));(p/'b').write_text(json.dumps(c))
            with contextlib.redirect_stdout(io.StringIO()) as output:b.compare(argparse.Namespace(before=p/'a',after=p/'b'))
            return json.loads(output.getvalue())
    def test_compare_same_workload(self):
        a=self.result();c=self.result();c['renderer_sha256']='new binary';self.assertEqual(self.compare(a,c)['fps_delta_percent'],0)
    def test_compare_rejects_different_modes_or_save_or_host(self):
        for key,value in [('valid',False),('cohort_fingerprint','different'),('profile_gpu',True),('system',{'model':'other'})]:
            a=self.result();c=self.result();c[key]=value
            with self.assertRaises(ValueError):self.compare(a,c)
    def test_compare_rejects_position_and_workload_changes(self):
        a=self.result();c=self.result();c['camera'][0]+=1
        with self.assertRaises(ValueError):self.compare(a,c)
        c=self.result();c['metrics']['draws']['mean']=900
        with self.assertRaises(ValueError):self.compare(a,c)
    def test_stop_only_the_process_group_it_created(self):
        own=subprocess.Popen(['sleep','15'],start_new_session=True)
        other=subprocess.Popen(['sleep','15'],start_new_session=True)
        try:
            b.stop_owned(own);self.assertIsNotNone(own.poll());self.assertIsNone(other.poll())
        finally:b.stop_owned(other)
    def test_cohort_fingerprint_ignores_renderer_and_detects_config(self):
        m=dict(seed_files={},ini_sha256='ini',game_eboot_sha256='game',environment={},expected_camera=None,external_configs={})
        old=b.cohort_fingerprint(m);m['renderer_sha256']='new';self.assertEqual(old,b.cohort_fingerprint(m));m['ini_sha256']='changed';self.assertNotEqual(old,b.cohort_fingerprint(m))
