import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

spec=importlib.util.spec_from_file_location('benchmark',Path(__file__).parents[1]/'tools/macos/benchmark.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)

class BenchmarkTests(unittest.TestCase):
    def test_memory_warning_grace_and_recovery(self):
        g=b.MemoryGuard(100,256);warning=dict(level=2,swap_mib=100)
        g.check(warning,10);g.check(warning,39)
        g.check(dict(level=1,swap_mib=100),40);g.check(warning,50)
        with self.assertRaisesRegex(RuntimeError,'warning guard'):g.check(warning,80)
    def test_memory_scoring_refuses_warning_immediately(self):
        with self.assertRaisesRegex(RuntimeError,'warning guard'):b.MemoryGuard(100,256).check(dict(level=2,swap_mib=100),10,True)
    def test_memory_critical_or_swap_growth_stops_immediately(self):
        for sample in [dict(level=4,swap_mib=100),dict(level=1,swap_mib=357)]:
            with self.assertRaisesRegex(RuntimeError,'Memory guard'):b.MemoryGuard(100,256).check(sample,10)
    def test_capture_refuses_unowned_target(self):
        with patch.object(b,'game_pids',return_value=[]), tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError): b.capture_frame(Mock(pid=1234),Path(d),Mock())
    def test_capture_requires_frame_boundary(self):
        with patch.object(b,'game_pids',return_value=[1234]), patch.object(b.os,'getpgid',return_value=1234), patch.object(b.shutil,'which',return_value='/usr/bin/gpucapture'), patch.object(b,'command',return_value='Device id=1\nQueue id=2'), tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError): b.capture_frame(Mock(pid=1234),Path(d),Mock())
    def test_capture_one_frame_command_and_no_fps_result(self):
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d);(folder/'gpu-frame.gputrace').mkdir();worker=Mock(returncode=0);worker.poll.return_value=0
            with patch.object(b,'game_pids',return_value=[1234]), patch.object(b.os,'getpgid',return_value=1234), patch.object(b.shutil,'which',return_value='/usr/bin/gpucapture'), patch.object(b,'command',return_value='Layer id=3'), patch.object(b,'pressure',return_value=dict(level=2,swap_mib=100)), patch.object(b.subprocess,'Popen',return_value=worker) as popen:
                result=b.capture_frame(Mock(pid=1234),folder,Mock());args=popen.call_args.args[0]
                self.assertEqual(args[args.index('--pid')+1],'1234');self.assertEqual(args[args.index('--count')+1],'1')
                self.assertIn('--disable-unused-recording',args);self.assertIn('--disable-backtrace-recording',args);self.assertNotIn('--all',args)
                self.assertFalse(result['valid']);self.assertNotIn('metrics',result);self.assertFalse((folder/'result.json').exists())
    def test_capture_guard_stops_only_owned_capture(self):
        with tempfile.TemporaryDirectory() as d:
            worker=Mock();worker.poll.return_value=None
            with patch.object(b,'game_pids',return_value=[1234]), patch.object(b.os,'getpgid',return_value=1234), patch.object(b.shutil,'which',return_value='/usr/bin/gpucapture'), patch.object(b,'command',return_value='Layer id=3'), patch.object(b,'pressure',return_value=dict(level=2,swap_mib=100)), patch.object(b.subprocess,'Popen',return_value=worker), patch.object(b.subprocess,'run') as stop, patch.object(b,'stop_owned') as owned:
                with self.assertRaisesRegex(RuntimeError,'guard'):b.capture_frame(Mock(pid=1234),Path(d),Mock(side_effect=RuntimeError('guard')))
                self.assertEqual(stop.call_args.args[0],['/usr/bin/gpucapture','stop','--pid','1234']);owned.assert_called_once_with(worker)
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
    def test_isolated_camera_telemetry_gap_retains_all_frame_times(self):
        r=self.rows();r[500].update(cam_x='',cam_y='',cam_z='',draws='3',frame_ms='100')
        m=b.metrics(r);self.assertEqual(m['frames'],1000);self.assertEqual(m['missing_camera_samples'],1);self.assertAlmostEqual(m['fps'],1000/20.08)
        self.assertEqual(r[500]['cam_x'],'')
    def test_camera_gap_cannot_hide_loading_or_scene_changes(self):
        for changes in [dict(t_s='12'),dict(cam_x='8')]:
            r=self.rows();r[500].update(cam_x='',cam_y='',cam_z='');r[501].update(changes)
            with self.assertRaises(ValueError):b.metrics(r)
    def test_consecutive_and_endpoint_camera_gaps_rejected(self):
        for indices in [(0,),(999,),(500,501)]:
            r=self.rows()
            for i in indices:r[i].update(cam_x='',cam_y='',cam_z='')
            with self.assertRaises(ValueError):b.metrics(r)
    def test_actual_title_or_zero_camera_never_repaired(self):
        for camera in [('0','4','-8'),('0','0','0')]:
            r=self.rows();r[500].update(zip(('cam_x','cam_y','cam_z'),camera))
            with self.assertRaises(ValueError):b.metrics(r)
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
    def test_diagnostic_capture_rejects_gpu_faults(self):
        for log in ['GPU fault at address 0x1','device lost','Command buffer completed with error','assertion failed']:
            with self.assertRaisesRegex(RuntimeError,'GPU faults'):b.check_gpu_faults(log)
        b.check_gpu_faults('Shader cache warmed; no errors')
    def test_display_snapshot_validates_shape_size_and_hash(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'f000_present_2x2_rgba.raw';p.write_bytes(bytes(range(16)))
            result=b.display_snapshot(Path(d));self.assertEqual(result['bytes'],16);self.assertEqual(result['width'],2)
            p.write_bytes(b'incomplete')
            with self.assertRaisesRegex(ValueError,'incomplete'):b.display_snapshot(Path(d))
    def test_display_snapshot_rejects_large_and_linked_outputs(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'f000_present_999999x999999_rgba.raw';p.write_bytes(b'')
            with self.assertRaises(ValueError):b.display_snapshot(Path(d))
            p.unlink();target=Path(d)/'actual';target.write_bytes(bytes(16))
            p=Path(d)/'f000_present_2x2_rgba.raw';p.symlink_to(target)
            with self.assertRaises(ValueError):b.display_snapshot(Path(d))
    def test_capture_validity_requires_balanced_native_encoders(self):
        rows=[dict(op='frame',frame=1,truncated=False),dict(op='close',queue=1,kind=2),dict(op='open',queue=1,kind=1,guest=True),dict(op='attachment',queue=1,slot=0,id=1,w=1,h=1,load=1,store=1),dict(op='close',queue=1,kind=1)]
        text=lambda:'\n'.join('NATIVE '+json.dumps(row) for row in rows)
        with self.assertRaisesRegex(ValueError,'Incomplete encoder'):b.validate_native_capture(text())
        rows.pop(1);self.assertEqual(b.validate_native_capture(text())['attachments'],1)
    def test_capture_series_cli_bounds_before_any_game_start(self):
        base=['benchmark','run','--workspace','/unused','--name','series']
        for flags in [['--capture-native','--capture-count','4'],['--capture-count','2'],['--capture-native','--capture-display','--capture-count','2']]:
            with patch.object(b.sys,'argv',base+flags), patch.object(b,'run') as run,contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):b.main()
                run.assert_not_called()
    def test_compare_rejects_legacy_scored_audits(self):
        for flag in b.DIAGNOSTIC_OVERRIDES:
            a=self.result();c=self.result();c['variants']={flag:'1'}
            with self.assertRaisesRegex(ValueError,'Audit overhead'):self.compare(a,c)
            c['variants'][flag]='0';self.assertEqual(self.compare(a,c)['fps_delta_percent'],0)
    def test_compare_rejects_audio_backend_change(self):
        a=self.result();c=self.result();c['audio_driver']='dummy'
        with self.assertRaisesRegex(ValueError,'audio backends'):self.compare(a,c)
    def test_compare_rejects_different_telemetry_overhead(self):
        a=self.result();c=self.result();c['telemetry_mode']='minimal'
        with self.assertRaisesRegex(ValueError,'telemetry overhead'):self.compare(a,c)
    def test_missing_diagnostic_timings_are_unknown_not_zero(self):
        rows=self.rows()
        for r in rows:del r['rec_busy_ms']
        m=b.metrics(rows);self.assertIsNone(m['rec_busy_ms']);self.assertEqual(m['fps'],50)
    def test_compare_same_workload(self):
        a=self.result();c=self.result();c['renderer_sha256']='new binary';self.assertEqual(self.compare(a,c)['fps_delta_percent'],0)
    def test_capture_diagnostic_cannot_be_compared_as_fps(self):
        with self.assertRaisesRegex(ValueError,'Diagnostic captures'):
            self.compare(self.result(),dict(valid=True,kind='single-layer-frame'))
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
    def test_protected_capture_helpers_use_only_owned_pid_fallback(self):
        own=Mock(pid=1234);own.poll.return_value=None
        with patch.object(b.os,'getpgid',return_value=1234), patch.object(b.os,'killpg',side_effect=PermissionError), patch.object(b.os,'kill') as kill:
            b.stop_owned(own)
            kill.assert_called_once_with(1234,b.signal.SIGTERM)
            own.wait.assert_called_once_with(timeout=12)
    def test_cohort_fingerprint_ignores_renderer_and_detects_config(self):
        m=dict(seed_files={},ini_sha256='ini',game_eboot_sha256='game',environment={},expected_camera=None,external_configs={})
        old=b.cohort_fingerprint(m);m['renderer_sha256']='new';self.assertEqual(old,b.cohort_fingerprint(m));m['ini_sha256']='changed';self.assertNotEqual(old,b.cohort_fingerprint(m))

    def test_entry_capture_cannot_skip_scored_warmup(self):
        base=['benchmark','run','--workspace','/unused','--name','entry']
        for flags in [['--capture-on-entry'],['--capture-on-entry','--capture-frame']]:
            with patch.object(b.sys,'argv',base+flags),patch.object(b,'run') as run,contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):b.main()
                run.assert_not_called()
        with patch.object(b.sys,'argv',base+['--capture-on-entry','--capture-native']),patch.object(b,'run') as run:
            self.assertEqual(b.main(),0);self.assertTrue(run.call_args.args[0].capture_on_entry)

    def test_programmatic_entry_capture_rejects_scoring_before_files_or_game(self):
        args=type('Args',(),dict(capture_on_entry=True,capture_native=False))()
        with self.assertRaisesRegex(ValueError,'never a scored run'):b.run(args)
