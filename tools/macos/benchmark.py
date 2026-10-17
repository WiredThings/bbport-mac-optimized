#!/usr/bin/env python3
"""Isolated, bounded Metal gameplay benchmark. Uses the owner's installed game assets.

init freezes a save/config cohort; run restores its private copy, enters the game, warms up,
measures, and stops only its own process group. compare rejects unlike workloads. No gameplay
assets or saves are distributed, and normal launcher preferences are never written.
"""
import argparse
import csv
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import plistlib
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
from datetime import datetime

DEFAULT_APP = Path('/Applications/Bloodborne Optimized.app')
DEFAULT_DATA = Path.home() / 'Library/Application Support/bbport/optimized-launcher'
DEFAULT_USER = Path.home() / 'Library/Application Support/bbport/user'
OVERRIDES = {'BB_MTL_FRAGMENT_UNIFORMS', 'BB_MTL_UPSCALE_ALPHA', 'BB_MTL_VERTEX_MATH', 'BB_MTL_PROXY_LEAN'}
NEUTRAL = 'lx=128 ly=128 rx=128 ry=128'


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def inventory(root):
    root = Path(root)
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f'Expected a real directory: {root}')
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Symlink is not allowed in benchmark state: {path}')
        if path.is_file() and path.name != '.DS_Store':
            result[str(path.relative_to(root))] = dict(bytes=path.stat().st_size, sha256=digest(path))
    return result


def write_json(path, value):
    path = Path(path)
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    tmp.replace(path)


def command(args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT, timeout=10)


def game_pids():
    return [int(fields[0]) for line in command(['ps', '-axo', 'pid=,comm=']).splitlines()
            if len(fields := line.strip().split(None, 1)) == 2
            and Path(fields[1]).name in ('bb-probe', 'bb-probe-metal', 'bb-gpu')]


def pressure():
    text = command(['sysctl', 'vm.swapusage', 'kern.memorystatus_vm_pressure_level'])
    level = re.search(r'pressure_level:\s*(\d+)', text)
    swap = re.search(r'used\s*=\s*([0-9.]+)M', text)
    if not level or not swap:
        raise ValueError('Cannot read system pressure; refusing an unmonitored benchmark')
    return dict(level=int(level[1]), swap_mib=float(swap[1]))


def frame_rows(path):
    if not path.exists():
        return []
    text=path.read_text()
    if text and not text.endswith('\n'): text=text.rsplit('\n',1)[0]+'\n' if '\n' in text else ''
    rows = list(csv.DictReader(text.splitlines()))
    return [r for r in rows if None not in r and None not in r.values()]


def is_world(row):
    try:
        camera=[float(row[k]) for k in ('cam_x','cam_y','cam_z')]
        return float(row['draws']) > 300 and all(math.isfinite(v) for v in camera) and math.dist(camera,(0,0,0))>.1 and math.dist(camera,(0,4,-8))>.1
    except (KeyError, ValueError, TypeError):
        return False


def world_camera(row):
    try:
        c=[float(row[k]) for k in ('cam_x','cam_y','cam_z')]
        return all(math.isfinite(v) for v in c) and math.dist(c,(0,0,0))>.1 and math.dist(c,(0,4,-8))>.1
    except (KeyError,ValueError,TypeError): return False


def camera_telemetry(rows):
    """Repair only isolated absent camera telemetry bracketed by the same scene within 50 ms.

    Keep every frame and its original time. Never repair a title/zero/invalid camera,
    consecutive gaps, endpoints, or a loading-length gap. Raw CSV remains unchanged.
    """
    keys=('cam_x','cam_y','cam_z')
    missing=[i for i,row in enumerate(rows) if all(row.get(k)=='' for k in keys)]
    if len(missing)>len(rows)*.01: raise ValueError('Too much missing camera telemetry')
    result=list(rows)
    for i in missing:
        if i==0 or i==len(rows)-1 or not world_camera(rows[i-1]) or not world_camera(rows[i+1]):
            raise ValueError('Unbracketed camera telemetry gap')
        a=[float(rows[i-1][k]) for k in keys];b=[float(rows[i+1][k]) for k in keys]
        gap=float(rows[i+1]['t_s'])-float(rows[i-1]['t_s'])
        if math.dist(a,b)>.05 or not 0<gap<=.05 or not float(rows[i-1]['t_s'])<float(rows[i]['t_s'])<float(rows[i+1]['t_s']):
            raise ValueError('Camera telemetry gap crosses a scene change')
        result[i]=dict(rows[i],**{k:rows[i-1][k] for k in keys})
    return result,len(missing)


def scene_matches(row, expected=None):
    if not world_camera(row): return False
    return not expected or math.dist([float(row[k]) for k in ('cam_x','cam_y','cam_z')],expected)<=.15


def stable_scene(rows, expected=None, seconds=3):
    if not rows: return False
    end=float(rows[-1]['t_s'])
    recent=[r for r in rows if end-seconds<=float(r['t_s'])<=end]
    if len(recent)<60 or end-float(recent[0]['t_s'])<seconds-.1: return False
    # Frame-boundary recorder counters can occasionally be low during normal gameplay.
    # Validate the window, retain all frame times, and never count the title camera as gameplay.
    try: recent,_=camera_telemetry(recent)
    except ValueError: return False
    return all(scene_matches(r,expected) for r in recent) and sum(float(r['draws'])>300 for r in recent)/len(recent)>=.99


def metrics(rows):
    rows,missing_camera=camera_telemetry(rows)
    if len(rows) < 100 or not all(world_camera(row) for row in rows) or sum(float(r['draws'])>300 for r in rows)/len(rows)<.99:
        raise ValueError('Measurement contains menus/loading or too few game frames')
    frames = sorted(float(r['frame_ms']) for r in rows)
    if not all(math.isfinite(t) and t > 0 for t in frames):
        raise ValueError('Invalid frame times')
    worst = max(1, math.ceil(len(frames) * .01))
    result = dict(frames=len(frames), missing_camera_samples=missing_camera, low_draw_frame_fraction=sum(float(r['draws'])<=300 for r in rows)/len(rows), fps=1000 / statistics.mean(frames), frame_ms_mean=statistics.mean(frames),
                  frame_ms_p50=statistics.median(frames), frame_ms_p95=frames[min(len(frames)-1, math.ceil(len(frames)*.95)-1)],
                  frame_ms_p99=frames[min(len(frames)-1, math.ceil(len(frames)*.99)-1)],
                  one_percent_low_fps=1000 / statistics.mean(frames[-worst:]), worst_frame_ms=frames[-1])
    for key in ('draws', 'dispatches', 'compiles', 'gpu_ms', 'rec_busy_ms', 'present_wait_ms', 'tick_wait_ms', 'cam_x', 'cam_y', 'cam_z'):
        values = [float(r[key]) for r in rows]
        result[key] = dict(mean=statistics.mean(values), min=min(values), max=max(values))
    return result


def cohort_fingerprint(manifest):
    # Deliberately excludes renderer/probe code and explicit optimization variants.
    keys = ('seed_files', 'ini_sha256', 'game_eboot_sha256', 'environment', 'expected_camera', 'external_configs')
    return hashlib.sha256(json.dumps({k: manifest[k] for k in keys}, sort_keys=True).encode()).hexdigest()


def init(args):
    root = args.workspace.resolve()
    if root.exists():
        raise ValueError('Use a new workspace; init never replaces an existing cohort')
    if game_pids():
        raise ValueError('Exit existing game before capturing a stable save')
    prefs_path = Path.home() / 'Library/Preferences/io.github.bbport.optimized.plist'
    prefs = plistlib.loads(prefs_path.read_bytes())
    if prefs.get('ultrawide', False):
        raise ValueError('Native ultrawide needs a separate benchmark profile')
    data = args.launcher_data.resolve()
    user = Path(prefs.get('userFolder') or DEFAULT_USER).expanduser().resolve()
    game = Path(prefs['gameFolder']).expanduser().resolve()
    source = user / 'savedata'
    before = inventory(source)
    if not any(k.endswith('SPRJ0005/userdata0000') for k in before):
        raise ValueError('No Bloodborne character save found')
    env = dict(BB_RENDERER='metal', BB_GAME_DIR=str(game), BB_LANGUAGE=str(prefs.get('language', '1')),
               BB_QUICKSAVE='0', BB_FPS=str(prefs.get('fpsMode', 'uncap')), BB_FULLSCREEN='0',
               BB_PRESENT_MODE=str(prefs.get('presentMode', 'Fifo')), BB_HDR='0',
               BB_METAL_OBJECT_MOTION='1' if prefs.get('metalObjectMotion', True) else '0',
               BB_METAL_OCCLUSION='1' if prefs.get('metalOcclusion', True) else '0',
               BB_SKIP_NETWORK_CHOICE='1', BB_MODS_ENABLED='1' if prefs.get('modsEnabled', True) else '0',
               BB_MODS_DIR=str(Path(prefs.get('modsFolder') or data/'mods').expanduser()),
               BB_MODS_CONFIG=str(data/'mods.json'), BB_PATCHES_DIR=str(Path(prefs.get('patchesFolder') or data/'patches').expanduser()),
               BB_PATCHES_CONFIG=str(data/'patches.json'))
    for pref, key in [('drawPipe', 'BB_DRAW_PIPE'), ('readbacks', 'BB_READBACKS'), ('preupload', 'BB_PREUPLOAD')]:
        if prefs.get(pref):
            env[key] = str(prefs[pref])
    if prefs.get('fpsLimit', 0):
        env['BB_FPS_LIMIT'] = str(prefs['fpsLimit'])
    root.mkdir(parents=True)
    (root/'seed-user').mkdir()
    shutil.copytree(source, root/'seed-user/savedata', ignore=shutil.ignore_patterns('.DS_Store'))
    if inventory(source) != before or inventory(root/'seed-user/savedata') != before:
        raise ValueError('Source save changed while copying')
    shutil.copy2(data/'bbport.ini', root/'settings.ini')
    # Separate disk shader cache, seeded once; never clear the normal player's cache.
    cache = data/'gpu-cache/metal1'
    if cache.is_dir():
        shutil.copytree(cache, root/'cache')
    else:
        (root/'cache').mkdir()
    manifest = dict(schema=1, source_save=str(source), seed_files=before, ini_sha256=digest(root/'settings.ini'),
                    game_eboot_sha256=digest(game/'eboot.bin'), environment=env, expected_camera=args.expected_camera,
                    external_configs={env[k]:digest(env[k]) if Path(env[k]).is_file() else None for k in ('BB_MODS_CONFIG','BB_PATCHES_CONFIG')},
                    created=datetime.now().astimezone().isoformat(), profile='static saved-game scene')
    manifest['cohort_fingerprint'] = cohort_fingerprint(manifest)
    write_json(root/'cohort.json', manifest)
    print(f'Benchmark cohort created: {root}; original saves untouched')


def stop_owned(process):
    if process.poll() is not None:
        return
    if os.getpgid(process.pid) != process.pid:
        raise RuntimeError('Unexpected process group; refusing broad termination')
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=12)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=5)


def run(args):
    root = args.workspace.resolve()
    lock = (root/'run.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    manifest = json.loads((root/'cohort.json').read_text())
    if cohort_fingerprint(manifest) != manifest['cohort_fingerprint'] or inventory(root/'seed-user/savedata') != manifest['seed_files'] or digest(root/'settings.ini') != manifest['ini_sha256']:
        raise ValueError('Frozen cohort changed; create a new cohort instead')
    for path, sha in manifest['external_configs'].items():
        if (digest(path) if Path(path).is_file() else None) != sha:
            raise ValueError('Mod/patch selections changed; create a new cohort')
    if game_pids():
        raise ValueError('Another game is running; refusing concurrent memory use')
    initial_pressure = pressure()
    if initial_pressure['level'] != 1:
        raise ValueError('System is under memory pressure before boot; retry after it recovers')
    app = args.app.resolve(); engine = app/'Contents/Resources/bbport'
    if '--benchmark' not in (engine/'tools/macos/run_optimized.sh').read_text() or b'BB_PAD_FILE_ONLY' not in (engine/'bin/bb-probe-metal').read_bytes():
        raise ValueError('App lacks isolated benchmark entry / input support; update first')
    if digest(Path(manifest['environment']['BB_GAME_DIR'])/'eboot.bin') != manifest['game_eboot_sha256']:
        raise ValueError('Game executable changed; comparison requires a new cohort')
    if not re.fullmatch('[A-Za-z0-9_-]{1,60}', args.name):
        raise ValueError('Use a short alphanumeric run name')
    folder = root/'runs'/(datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+args.name)
    folder.mkdir(parents=True, exist_ok=False)
    shutil.copytree(root/'seed-user', folder/'user')
    (folder/'data').mkdir();shutil.copy2(root/'settings.ini', folder/'data/bbport.ini')
    pad = folder/'pad.txt';pad.write_text(NEUTRAL)
    env = {k:v for k,v in os.environ.items() if not k.startswith('BB_') and k not in ('DYLD_LIBRARY_PATH','PYTHONHOME','PYTHONPATH','VK_DRIVER_FILES','VK_ICD_FILENAMES')}
    env.update(manifest['environment'])
    variants = {}
    for item in args.env:
        key, sep, value = item.partition('=')
        if not sep or key not in OVERRIDES or value not in ('0','1'):
            raise ValueError(f'Unsupported benchmark override: {item}')
        variants[key] = value
    env.update(variants)
    env.update(BB_USER_DIR=str(folder/'user'), BB_DATA_DIR=str(folder/'data'), BB_CONFIG=str(folder/'data/bbport.ini'),
               BB_GPU_USER_DIR=str(root/'cache'), BB_PAD_FILE=str(pad), BB_PAD_FILE_ONLY='1', BB_FRAME_STATS='1',
               BB_TIMEOUT=str(args.startup_timeout+args.warmup+args.seconds+90))
    if args.profile_gpu:
        env.update(BB_MTL_GPU_TIMING='1', BB_MTL_GPU_TIMING_EVERY='8')
    log_file = (folder/'run.log').open('w')
    source_before = inventory(manifest['source_save'])
    process = None; samples=[]; started=time.monotonic();last_guard=0;next_press=None;held=False;presses=0;world=False;input_pause_until=0;left_title=False
    log_path=folder/'run.log';frames=folder/'data/out/frames.csv'
    def guard():
        nonlocal last_guard
        if process.poll() is not None:
            raise RuntimeError(f'Benchmark game exited ({process.returncode}); see {log_path}')
        now=time.monotonic()
        if now-last_guard<3:
            return
        last_guard=now;p=pressure()
        if p['level']>=4 or p['swap_mib']-initial_pressure['swap_mib'] > args.max_swap_growth_mib:
            raise RuntimeError('Memory guard stopped this benchmark; no result accepted')
        lines=log_path.read_text(errors='replace').splitlines()
        totals=[s for s in lines if s.startswith('Metal memory totals:')][-1:]
        if totals and int(re.search(r'footprint=(\d+)',totals[0])[1]) > 9*1024**3:
            raise RuntimeError('Game footprint exceeded 9 GiB guard')
        samples.append(dict(elapsed=now-started,pressure=p,memory=totals))
        write_json(folder/'system-samples.json',samples)
    def interrupt(signum, frame):
        raise KeyboardInterrupt(f'Benchmark interrupted by signal {signum}')
    old_signals={sig:signal.signal(sig,interrupt) for sig in (signal.SIGTERM,signal.SIGINT)}
    try:
        process=subprocess.Popen(['bash',str(engine/'tools/macos/run_optimized.sh'),'--benchmark'],env=env,stdout=log_file,stderr=subprocess.STDOUT,start_new_session=True)
        write_json(folder/'owner.json',dict(pid=process.pid,created=time.time(),app=str(app)))
        print(f'Booting private save: {folder}',flush=True)
        deadline=started+args.startup_timeout
        while time.monotonic()<deadline:
            guard();now=time.monotonic();rr=frame_rows(frames)
            if rr and is_world(rr[-1]):
                # Stop title input on a plausible world frame; require a complete stable window
                # before entering warmup. The title's 3D preview camera is explicitly excluded.
                pad.write_text(NEUTRAL);held=False;input_pause_until=now+12;left_title=True
            if stable_scene(rr,manifest['expected_camera']):
                world=True;pad.write_text(NEUTRAL);break
            log=log_path.read_text(errors='replace')
            if 'Runtime: pad opened' in log and next_press is None:
                next_press=now+15
            if next_press is not None and now>=next_press and now>=input_pause_until and not left_title:
                if held:
                    pad.write_text(NEUTRAL);held=False;next_press=now+7
                elif presses<32:
                    pad.write_text('cross '+NEUTRAL);held=True;presses+=1;next_press=now+.25
                    print(f'Title confirm {presses}',flush=True)
            time.sleep(.2)
        if not world:
            raise RuntimeError('Did not enter a controllable scene before deadline')
        print('Scene detected; warming shader and resource caches',flush=True)
        warm_start=time.monotonic();quiet_since=warm_start
        while time.monotonic()-warm_start<args.warmup or time.monotonic()-quiet_since<15:
            guard();rr=frame_rows(frames)
            if not stable_scene(rr,manifest['expected_camera']):
                quiet_since=time.monotonic()
                # Loading transitions restart the quiet window; one transient frame is not a failure.
                if time.monotonic()-warm_start>args.warmup+90:
                    raise RuntimeError('Scene did not settle during bounded warmup')
                time.sleep(.5)
                continue
            if float(rr[-1]['compiles'])>0:
                quiet_since=time.monotonic()
            if time.monotonic()-warm_start>args.warmup+90:
                raise RuntimeError('Shader warmup did not settle')
            time.sleep(.5)
        first_t=float(frame_rows(frames)[-1]['t_s']);measure_start=time.monotonic()
        write_json(folder/'measurement-window.json',dict(first_t=first_t,seconds=args.seconds))
        print(f'Measuring {args.seconds}s; GPU stage timing={args.profile_gpu}',flush=True)
        while time.monotonic()-measure_start<args.seconds:
            guard();time.sleep(.5)
        rr=frame_rows(frames);selected=[r for r in rr if float(r['t_s'])>first_t]
        stats=metrics(selected)
        if stats['compiles']['max']>0:
            raise ValueError('Shader compilation occurred during measurement; not a warm-cache result')
        camera=[stats[k]['mean'] for k in ('cam_x','cam_y','cam_z')]
        expected=manifest['expected_camera']
        if expected and math.dist(camera,expected)>.15:
            raise ValueError(f'Loaded a different location: {camera}, expected {expected}')
        if any(stats[k]['max']-stats[k]['min']>.05 for k in ('cam_x','cam_y','cam_z')):
            raise ValueError('Player moved during static-scene measurement')
        log=log_path.read_text(errors='replace')
        faults=[line for line in log.splitlines() if re.search(r'GPU fault|device lost|command buffer.*error|assertion failed|Execution of the command buffer was aborted',line,re.I)]
        if faults:
            raise RuntimeError(f'GPU faults: {faults[:2]}')
        result=dict(schema=1,valid=False,name=args.name,scene_profile=manifest['profile'],cohort_fingerprint=manifest['cohort_fingerprint'],
                    profile_gpu=args.profile_gpu,metrics=stats,app_version=plistlib.loads((app/'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'],
                    renderer_sha256=digest(engine/'bin/gpu-metal/libbbgpu.dylib'),probe_sha256=digest(engine/'bin/bb-probe-metal'),
                    variants=variants,settings_sha256=manifest['ini_sha256'],camera=camera,samples=samples,
                    system=dict(model=command(['sysctl','-n','hw.model']).strip(),os=command(['sw_vers','-productVersion']).strip()),
                    duration=time.monotonic()-started,notes=['Static gameplay benchmark, not whole-game average.','1% low = reciprocal of mean of slowest ceil(1% of frames).','GPU timing is opt-in; compare only matching instrumentation modes.'])
        write_json(folder/'result.json',result)
        print(json.dumps(dict(result=str(folder/'result.json'),fps=stats['fps'],one_percent_low_fps=stats['one_percent_low_fps'],frame_ms_p95=stats['frame_ms_p95'])),flush=True)
    except BaseException as e:
        write_json(folder/'failure.json',dict(error=str(e),seconds=time.monotonic()-started))
        raise
    finally:
        pad.write_text(NEUTRAL)
        if process is not None:
            stop_owned(process)
        log_file.close()
        for sig, handler in old_signals.items(): signal.signal(sig,handler)
        if inventory(manifest['source_save'])!=source_before:
            (folder/'result.json').unlink(missing_ok=True)
            raise RuntimeError('Original save changed externally; result invalidated')
        write_json(folder/'cleanup.json',dict(own_process_stopped=True,original_saves_unchanged=True,launcher_preferences_written=False))
        if (folder/'result.json').exists():
            completed=json.loads((folder/'result.json').read_text());completed['valid']=True;write_json(folder/'result.json',completed)
        print('Own process stopped; original saves and launcher preferences untouched',flush=True)


def compare(args):
    a=json.loads(args.before.read_text());b=json.loads(args.after.read_text())
    if not a['valid'] or not b['valid'] or a['cohort_fingerprint']!=b['cohort_fingerprint'] or a['profile_gpu']!=b['profile_gpu'] or a['system']!=b['system']:
        raise ValueError('Unmatched workloads, instrumentation or host; refusing comparison')
    if math.dist(a['camera'],b['camera'])>.05:
        raise ValueError('Different player positions')
    draw_delta=(b['metrics']['draws']['mean']/a['metrics']['draws']['mean']-1)*100
    if abs(draw_delta)>2:
        raise ValueError('Draw count changed by over 2%; inspect camera/workload first')
    print(json.dumps(dict(before=a['metrics'],after=b['metrics'],fps_delta_percent=(b['metrics']['fps']/a['metrics']['fps']-1)*100,draw_count_delta_percent=draw_delta,notes='Use repeated interleaved runs; one pair does not establish statistical significance.'),indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('init');p.add_argument('--workspace',type=Path,required=True);p.add_argument('--launcher-data',type=Path,default=DEFAULT_DATA);p.add_argument('--expected-camera',nargs=3,type=float);p.set_defaults(func=init)
    p=sub.add_parser('run');p.add_argument('--workspace',type=Path,required=True);p.add_argument('--app',type=Path,default=DEFAULT_APP);p.add_argument('--name',required=True);p.add_argument('--warmup',type=int,default=45);p.add_argument('--seconds',type=int,default=45);p.add_argument('--startup-timeout',type=int,default=300);p.add_argument('--max-swap-growth-mib',type=int,default=2048);p.add_argument('--profile-gpu',action='store_true');p.add_argument('--env',action='append',default=[]);p.set_defaults(func=run)
    p=sub.add_parser('compare');p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.set_defaults(func=compare)
    args=parser.parse_args()
    if args.command=='run' and not (15<=args.warmup<=180 and 15<=args.seconds<=180 and 30<=args.startup_timeout<=600 and 256<=args.max_swap_growth_mib<=2048):
        parser.error('Use bounded warmup/sample (15..180s), startup (30..600s), and swap growth (256..2048 MiB)')
    try: args.func(args)
    except Exception as e:
        print(f'Benchmark failed: {e}',file=sys.stderr);return 1
    return 0


if __name__=='__main__':
    raise SystemExit(main())
