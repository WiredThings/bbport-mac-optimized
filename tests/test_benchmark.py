import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

spec=importlib.util.spec_from_file_location('benchmark',Path(__file__).parents[1]/'tools/macos/benchmark.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)

class BenchmarkTests(unittest.TestCase):
    def test_reviewed_30s_policy_loading_burst_and_continuous_stops(self):
        g=b.MemoryGuard(9626.56,12288,24576,30)
        for t,swap in [(0,9775.62),(37.38,9797),(40.55,10095.62),(43.72,10393.38),(47,10393.38)]:
            g.check(dict(level=2,swap_mib=swap),t)
        for rate,stop,label in [(40,30,'sustained'),(128,9,'emergency')]:
            g=b.MemoryGuard(9626.56,12288,24576,30)
            for t in range(0,stop,3):g.check(dict(level=2,swap_mib=10000+t*rate),t)
            with self.assertRaisesRegex(RuntimeError,label):g.check(dict(level=2,swap_mib=10000+stop*rate),stop)

    def test_30s_policy_does_not_join_separated_bursts(self):
        for samples in [([(t,10000+(t//9)*500) for t in range(0,100,3)]),
                        [(0,10000),(3,10100),(6,10000),(9,10100)],
                        [(t,10000+t*5) for t in range(0,301,3)]]:
            g=b.MemoryGuard(9626.56,12288,24576,30)
            for t,swap in samples:g.check(dict(level=2,swap_mib=swap),t)
        for level,swap,error in [(4,10000,'critical'),(1,24577,'absolute'),(1,22000,'total swap')]:
            with self.assertRaisesRegex(RuntimeError,error):
                b.MemoryGuard(9626.56,12288,24576,30).check(dict(level=level,swap_mib=swap),0)

    def test_deadline_requires_timezone_stops_before_game_or_files(self):
        stamp=b.authorization_deadline('2026-10-22T17:00:00+08:00')
        self.assertEqual(stamp,b.authorization_deadline('2026-10-22T09:00:00+00:00'))
        b.check_authorization_deadline(stamp,stamp-1)
        with self.assertRaisesRegex(RuntimeError,'deadline'):b.check_authorization_deadline(stamp,stamp)
        with self.assertRaises(argparse.ArgumentTypeError):b.authorization_deadline('2026-10-22T17:00:00')
        with patch.object(b.time,'time',return_value=stamp),self.assertRaisesRegex(RuntimeError,'deadline'):
            b.run(argparse.Namespace(stop_at=stamp))

    def test_cli_records_reviewed_rate_and_deadline(self):
        argv=['benchmark','run','--workspace','/unused','--name','rate-test','--swap-rapid-window-seconds','30','--stop-at','2026-10-22T17:00:00+08:00']
        with patch.object(sys,'argv',argv),patch.object(b,'run') as run:b.main()
        self.assertEqual(run.call_args.args[0].swap_rapid_window_seconds,30)
        self.assertEqual(run.call_args.args[0].stop_at,b.authorization_deadline(argv[-1]))
        a=self.result();c=self.result();c['swap_rapid_window_seconds']=30
        with self.assertRaisesRegex(ValueError,'guard policies'):self.compare(a,c)

    def test_cli_accepts_expanded_budget_without_accessing_game(self):
        argv=['benchmark','run','--workspace','/unused','--name','budget-test',
              '--max-swap-growth-mib','12288','--max-swap-total-mib','24576','--swap-budget-baseline-mib','9626.56']
        with patch.object(sys,'argv',argv),patch.object(b,'run') as run:
            b.main()
        args=run.call_args.args[0]
        self.assertEqual((args.max_swap_growth_mib,args.max_swap_total_mib,args.swap_budget_baseline_mib),(12288,24576,9626.56))

    def test_cli_defaults_use_new_capacity_and_reject_nonfinite_baseline(self):
        argv=['benchmark','run','--workspace','/unused','--name','budget-test']
        with patch.object(sys,'argv',argv),patch.object(b,'run') as run:
            b.main()
        self.assertEqual(run.call_args.args[0].max_swap_growth_mib,12288)
        with patch.object(sys,'argv',argv+['--swap-budget-baseline-mib','nan']),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
            b.main()

    def test_expanded_swap_capacity_does_not_treat_existing_10gib_as_failure(self):
        g=b.MemoryGuard(10000,b.DEFAULT_SWAP_GROWTH_MIB,b.DEFAULT_SWAP_TOTAL_MIB)
        g.check(dict(level=1,swap_mib=10000),0)
        g.check(dict(level=2,swap_mib=21000),1000) # slow growth, within both limits
        with self.assertRaisesRegex(RuntimeError,'total swap growth'):
            g.check(dict(level=2,swap_mib=22289),2000)
        with self.assertRaisesRegex(RuntimeError,'absolute system swap'):
            b.MemoryGuard(24000,12288,24576).check(dict(level=1,swap_mib=24577),0)

    def test_expanded_capacity_keeps_critical_and_rapid_stops(self):
        with self.assertRaisesRegex(RuntimeError,'critical'):
            b.MemoryGuard(10000,12288).check(dict(level=4,swap_mib=10000),0)
        g=b.MemoryGuard(10000,12288)
        for t,swap in [(0,10000),(3,10300)]:g.check(dict(level=2,swap_mib=swap),t)
        with self.assertRaisesRegex(RuntimeError,'rapid swap'):
            g.check(dict(level=2,swap_mib=10600),6)

    def test_shared_allowance_does_not_reset_batch_and_respects_absolute_cap(self):
        self.assertEqual(b.swap_allowance(9626.56,9807.62),11850)
        self.assertEqual(b.swap_allowance(9626.56,18000),3658)
        self.assertEqual(b.swap_allowance(24000,24000),320)
        self.assertEqual(b.swap_allowance(9626.56,22000),0)
        self.assertEqual(b.swap_allowance(9626.56,9000),12288)
        for invalid in (-1,float('nan'),float('inf')):
            with self.assertRaises(ValueError):b.swap_allowance(invalid,9000)

    def test_compare_rejects_unlike_absolute_or_shared_budgets(self):
        for key,value in [('max_swap_total_mib',24576),('shared_swap_budget_baseline_mib',9626.56)]:
            a=self.result();c=self.result();c[key]=value
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'guard policies'):
                self.compare(a,c)

    def test_visual_capture_refuses_score_before_game_or_file_access(self):
        with self.assertRaisesRegex(ValueError,'refuse a scored run'):
            b.run(argparse.Namespace(visual_observation='present'))

    def test_comparison_keeps_legacy_visual_state_unverified(self):
        result=self.compare(self.result(),self.result())
        self.assertEqual(result['visual_observation'],'unverified')
        self.assertIn('not certified',result['notes'])

    def test_visual_observation_modes_must_match_and_never_include_capture(self):
        for left,right in [('absent','unverified'),('present','present'),('absent','present'),('invented','invented')]:
            a=self.result();c=self.result()
            a['visual_observation']=left;c['visual_observation']=right
            with self.subTest(left=left,right=right),self.assertRaisesRegex(ValueError,'[Vv]isual observation'):
                self.compare(a,c)
        a=self.result();a['visual_observation']='absent'
        result=self.compare(a,a)
        self.assertEqual(result['visual_observation'],'absent')
        self.assertIn('not automatically detected',result['notes'])

    def test_gpu_pointer_watch_is_bounded_and_never_scored(self):
        self.assertIn('BB_CAPTURE_COPY_POINTERS',b.DIAGNOSTIC_OVERRIDES)
        for native,watch in ((False,None),(True,None),(False,'0x7056eed0e0,192')):
            with self.assertRaisesRegex(ValueError,'GPU pointer watch requires'):
                b.run(argparse.Namespace(env=['BB_CAPTURE_COPY_POINTERS=1'],capture_native=native,watch_buffer=watch))
    def test_srt_watch_requires_bounded_capture_before_game_setup(self):
        for native,watch in ((False,None),(True,None),(False,'0x7056eed0e0,192')):
            with self.assertRaisesRegex(ValueError,'SRT reads require'):
                b.run(argparse.Namespace(env=['BB_CAPTURE_SRT_READS=1'],capture_native=native,watch_buffer=watch))
    def test_development_marker_must_be_in_executed_embedded_game(self):
        with tempfile.TemporaryDirectory() as d:
            engine=Path(d);top=engine/'bin/bb-probe-metal';top.parent.mkdir();top.write_bytes(b'BB_DEVELOPMENT_WORLD_WARPER_V1');top.chmod(0o755)
            self.assertEqual(b.executed_game_probe(engine),top)
            (engine/'DEVELOPMENT-WORLD-WARPER.json').write_text('{}')
            b.validate_development_runtime(engine)
            nested=engine/'bin/Bloodborne Game.app/Contents/MacOS/bb-probe-metal'
            nested.parent.mkdir(parents=True);nested.write_bytes(b'release runtime');nested.chmod(0o755)
            self.assertEqual(b.executed_game_probe(engine),nested)
            with self.assertRaisesRegex(ValueError,'actual executed'):b.validate_development_runtime(engine)
            nested.write_bytes(top.read_bytes());b.validate_development_runtime(engine)
    def test_buffer_watch_is_bounded_and_diagnostic_only(self):
        self.assertEqual(b.watch_buffer_range('0x7056eed0e0,192'),'0x7056eed0e0,192')
        for bad in ['0,192','0x10000000000,1','4096,4097','4096,0','-1,192','garbage','1,2,3']:
            with self.assertRaises(argparse.ArgumentTypeError):b.watch_buffer_range(bad)
        with self.assertRaisesRegex(ValueError,'never scored'):
            b.run(argparse.Namespace(watch_buffer='0x7056eed0e0,192',capture_native=False))
    def test_memory_warning_is_recorded_without_abort(self):
        g=b.MemoryGuard(100,256);warning=dict(level=2,swap_mib=100)
        for t in range(0,300,3):g.check(warning,t,True)
        g.check(dict(level=1,swap_mib=100),301)
    def test_memory_rapid_swap_growth_stops_before_total_cap(self):
        g=b.MemoryGuard(100,2048)
        g.check(dict(level=1,swap_mib=100),0)
        g.check(dict(level=2,swap_mib=400),3)
        with self.assertRaisesRegex(RuntimeError,'rapid swap'):g.check(dict(level=2,swap_mib=650),6,True)
    def test_memory_slow_growth_and_swap_recovery_do_not_trigger_rate_guard(self):
        g=b.MemoryGuard(100,2048)
        for t in range(0,100,3):g.check(dict(level=2,swap_mib=100+t*5),t,True)
        g.check(dict(level=2,swap_mib=200),102)
    def test_single_loading_swap_burst_is_not_sustained_growth(self):
        g=b.MemoryGuard(8639.06,2048)
        for t in range(0,27,3):g.check(dict(level=2,swap_mib=8639.06),t)
        g.check(dict(level=2,swap_mib=9181.38),27)
        for t in range(30,120,3):g.check(dict(level=2,swap_mib=9181.38),t,True)
    def test_growth_rate_resets_after_a_flat_interval(self):
        g=b.MemoryGuard(100,2048)
        for t,swap in [(0,100),(3,400),(6,400),(9,700),(12,700)]:g.check(dict(level=2,swap_mib=swap),t)
    def test_session_lock_display_sleep_and_monitoring_gap_invalidate(self):
        ready=dict(locked=False,all_displays_asleep=False)
        for bad in [dict(locked=True,all_displays_asleep=False),dict(locked=False,all_displays_asleep=True)]:
            with self.assertRaisesRegex(RuntimeError,'Session guard'):b.SessionGuard().check(bad,0,100)
        for now,wall in [(20,120),(3,120)]:
            g=b.SessionGuard();g.check(ready,0,100)
            with self.assertRaisesRegex(RuntimeError,'interruption'):g.check(ready,now,wall)
        g=b.SessionGuard();g.check(ready,0,100);g.check(ready,3,103)
    def test_compare_rejects_different_guard_policies(self):
        a=self.result();c=self.result();c['guard_policy']=b.GUARD_POLICY
        with self.assertRaisesRegex(ValueError,'guard policies'):self.compare(a,c)
    def test_paired_session_clocks_allow_slow_but_monitored_sampling(self):
        ready=dict(locked=False,all_displays_asleep=False)
        g=b.SessionGuard()
        g.check(ready,0,100,sample_duration=.05)
        g.check(ready,3,103,sample_duration=2.4)
        g.check(ready,6,106,sample_duration=.05)
        # A truly unmonitored sample remains invalid even with matching clock pairs.
        with self.assertRaisesRegex(RuntimeError,'exceeded 12 seconds'):
            g.check(ready,9,109,sample_duration=12.01)
    def test_memory_critical_or_swap_growth_stops_immediately(self):
        for sample in [dict(level=4,swap_mib=100),dict(level=1,swap_mib=357)]:
            with self.assertRaisesRegex(RuntimeError,'Memory guard'):b.MemoryGuard(100,256).check(sample,10)
    def test_copy_scope_observer_cannot_score_before_file_or_game_access(self):
        args=argparse.Namespace(env=['BB_CAPTURE_COPY_SCOPE=1'],capture_native=False)
        with self.assertRaisesRegex(ValueError,'never a performance score'):b.run(args)

    def test_fragment_guard_cannot_be_scored_or_open_game_files(self):
        self.assertIn('BB_CAPTURE_FRAGMENT_GUARD',b.DIAGNOSTIC_OVERRIDES)
        args=argparse.Namespace(env=['BB_CAPTURE_FRAGMENT_GUARD=1'],capture_native=False)
        with self.assertRaisesRegex(ValueError,'never a performance score'):b.run(args)

    def test_gather_reuse_and_current_math_are_score_overrides(self):
        for key in ('BB_MTL_GATHER_REUSE','BB_MTL_RELAXED_MATH'):
            self.assertIn(key,b.OVERRIDES)
            self.assertNotIn(key,b.DIAGNOSTIC_OVERRIDES)

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
    def test_verified_corridor_keeps_low_count_frames_but_rejects_wrong_scene(self):
        rows=self.rows()
        for row in rows[::10]:row.update(draws='150',frame_ms='80')
        self.assertFalse(b.stable_scene(rows))
        self.assertTrue(b.stable_scene(rows,[-9,-4,-18]))
        score=b.metrics(rows,[-9,-4,-18])
        self.assertEqual(score['frames'],1000)
        self.assertAlmostEqual(score['fps'],1000/26)
        self.assertAlmostEqual(score['low_draw_frame_fraction'],.1)
        with self.assertRaises(ValueError):b.metrics(rows,[7,2,8])
        for row in rows:row['draws']='20'
        self.assertFalse(b.stable_scene(rows,[-9,-4,-18]))
        with self.assertRaises(ValueError):b.metrics(rows,[-9,-4,-18])
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
    def test_consecutive_capture_requires_identity_and_unbroken_frame_numbers(self):
        def frame(number,index,count=3):
            rows=[dict(op='frame',frame=number,truncated=False,series_count=count,series_member=index),
                  dict(op='open',queue=1,kind=1,guest=True),
                  dict(op='attachment',queue=1,slot=0,id=1,w=1,h=1,load=1,store=1),
                  dict(op='close',queue=1,kind=1)]
            return '\n'.join('NATIVE '+json.dumps(r) for r in rows)
        valid=[frame(10+i,i+1) for i in range(3)]
        self.assertEqual([r['frame'] for r in b.validate_consecutive_captures(valid)],[10,11,12])
        with self.assertRaisesRegex(ValueError,'Gap'):b.validate_consecutive_captures([valid[0],frame(12,2),frame(13,3)])
        with self.assertRaisesRegex(ValueError,'identity'):b.validate_consecutive_captures([valid[0],valid[1],frame(12,1)])
        with self.assertRaisesRegex(ValueError,'identity'):b.validate_consecutive_captures([frame(10,1,1),valid[1],valid[2]])
        with self.assertRaises(ValueError):b.validate_consecutive_captures(valid[:1])
    def test_consecutive_capture_cli_rejects_score_or_single_frame_mode(self):
        base=['benchmark','run','--workspace','/unused','--name','consecutive']
        for flags in [['--capture-consecutive'],['--capture-native','--capture-consecutive'],
                      ['--capture-consecutive','--capture-count','2']]:
            with patch.object(b.sys,'argv',base+flags),patch.object(b,'run') as run,contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):b.main()
                run.assert_not_called()
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
    def test_owned_helper_exit_between_poll_and_group_lookup_is_reaped(self):
        own=subprocess.Popen([b.sys.executable,'-c','import sys; sys.stdin.buffer.read()'],stdin=subprocess.PIPE,start_new_session=True)
        other=subprocess.Popen(['sleep','15'],start_new_session=True)
        native_getpgid=b.os.getpgid
        def exit_then_lookup(pid):
            self.assertEqual(pid,own.pid)
            own.stdin.close();self.assertEqual(own.wait(timeout=5),0)
            return native_getpgid(pid)
        try:
            with patch.object(b.os,'getpgid',side_effect=exit_then_lookup),patch.object(b.os,'killpg') as terminate:
                b.stop_owned(own)
                terminate.assert_not_called()
            self.assertEqual(own.returncode,0);self.assertIsNone(other.poll())
        finally:
            if own.stdin and not own.stdin.closed:own.stdin.close()
            own.wait(timeout=5);b.stop_owned(other)

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
