#!/usr/bin/env python3
"""Isolated, bounded Metal gameplay benchmark. Uses the owner's installed game assets.

init freezes a save/config cohort; run restores its private copy, enters the game, warms up,
measures, and stops only its own process group. compare rejects unlike workloads. No gameplay
assets or saves are distributed, and normal launcher preferences are never written.
"""
import argparse
import importlib.util
import csv
import ctypes
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

_attachment_spec=importlib.util.spec_from_file_location('benchmark_native_attachment_validation',Path(__file__).with_name('analyze_metal_attachments.py'))
_attachments=importlib.util.module_from_spec(_attachment_spec);_attachment_spec.loader.exec_module(_attachments)

def validate_native_capture(text):
    summary=_attachments.analyze(text)
    if not summary['attachments']:
        raise ValueError('Native capture has no attachment metadata')
    return summary


def validate_consecutive_captures(texts):
    if not 2<=len(texts)<=3:
        raise ValueError('Consecutive capture requires 2..3 members')
    summaries=[validate_native_capture(t) for t in texts]
    headers=[next(json.loads(line[7:]) for line in t.splitlines()
                  if line.startswith('NATIVE ') and json.loads(line[7:]).get('op')=='frame') for t in texts]
    for index,(summary,header) in enumerate(zip(summaries,headers)):
        if header.get('series_count')!=len(texts) or header.get('series_member')!=index+1:
            raise ValueError('Missing or mismatched consecutive capture identity')
        if summary['frame']!=summaries[0]['frame']+index:
            raise ValueError('Gap in consecutive captured frames')
    return summaries

DEFAULT_APP = Path('/Applications/Blood Echoes.app')
DEFAULT_DATA = Path.home() / 'Library/Application Support/bbport/optimized-launcher'
DEFAULT_USER = Path.home() / 'Library/Application Support/bbport/user'
DIAGNOSTIC_OVERRIDES = {'BB_MTL_COMPILE_AUDIT', 'BB_AUDIO_STATS', 'BB_MTL_BUFFER_RANGE_AUDIT', 'BB_MTL_COPY_AUDIT', 'BB_MTL_STAGING_AUDIT'}
OVERRIDES = {'BB_MTL_GATHER_REUSE', 'BB_MTL_RELAXED_MATH', 'BB_MTL_PERSIST_WARM_ARCHIVE', 'BB_MTL_RELEASE_WARM_ARCHIVE', 'BB_MTL_INLINE_UPDATES','BB_MTL_FILL_UPDATES', 'BB_MTL_COMPILE_AUDIT', 'BB_AUDIO_STATS', 'BB_MTL_FOLD_CLEARS', 'BB_MTL_PRECISE_COMPUTE', 'BB_MTL_TEXTURE_TILING_HALF', 'BB_MTL_QUERY_COMPUTE_CHAIN', 'BB_MTL_COALESCE_BARRIERS', 'BB_MTL_BUFFER_RANGE_AUDIT', 'BB_MTL_ADAPTIVE_STAGING','BB_MTL_TEXTURE_TILING','BB_MTL_PRECISE_RESIDENCY', 'BB_MTL_STAGING_AUDIT', 'BB_MTL_DIRECT_COPY', 'BB_MTL_COPY_AUDIT', 'BB_MTL_BATCH_CLEARS', 'BB_MTL_FRAGMENT_UNIFORMS', 'BB_MTL_UPSCALE_ALPHA', 'BB_MTL_VERTEX_MATH', 'BB_MTL_PROXY_LEAN'}
OVERRIDES.add('BB_TEXTURE_INVALID_RECLAIM')
OVERRIDES.add('BB_TEXTURE_RELEASED_STORAGE')
OVERRIDES.add('BB_LIFETIME_HEAP_HISTORY_PRIORITY')
OVERRIDES.add('BB_MTL_CONTINUE_CLEARS')
OVERRIDES.add('BB_MTL_QUERY_WRITE_EPOCH')
OVERRIDES.add('BB_CAPTURE_COPY_SCOPE')
DIAGNOSTIC_OVERRIDES.add('BB_CAPTURE_COPY_SCOPE')
OVERRIDES.add('BB_MTL_NATIVE_CLEAR_BATCH')
OVERRIDES.add('BB_FRAME_TIMING')
OVERRIDES.update({'BB_MTL_MERGE_OVERLAY', 'BB_LAYER_USE_CACHE', 'BB_MTL_COPY_PAIR', 'BB_MTL_RENDER_UPDATES'})
OVERRIDES.add('BB_CAPTURE_SRT_READS')
DIAGNOSTIC_OVERRIDES.add('BB_CAPTURE_SRT_READS')
OVERRIDES.add('BB_CAPTURE_COPY_POINTERS')
DIAGNOSTIC_OVERRIDES.add('BB_CAPTURE_COPY_POINTERS')
OVERRIDES.add('BB_CAPTURE_FRAGMENT_GUARD')
DIAGNOSTIC_OVERRIDES.add('BB_CAPTURE_FRAGMENT_GUARD')
NEUTRAL = 'lx=128 ly=128 rx=128 ry=128'
GUARD_POLICY = 'critical-sustained-swap-paired-session-v6'
DEFAULT_SWAP_GROWTH_MIB = 12 * 1024
DEFAULT_SWAP_TOTAL_MIB = 24 * 1024

def swap_allowance(baseline,current,max_growth=DEFAULT_SWAP_GROWTH_MIB,max_total=DEFAULT_SWAP_TOTAL_MIB,reserve=256):
    """Remaining shared budget; recovery does not reset the original baseline."""
    if not all(math.isfinite(v) and v>=0 for v in (baseline,current,max_growth,max_total,reserve)):
        raise ValueError('Invalid swap budget')
    return max(0,math.floor(min(max_growth,baseline+max_growth-current-reserve,max_total-current-reserve)))


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


def executed_game_probe(engine):
    """Mirror packaged run.sh selection; the nested app can differ from bin/."""
    engine=Path(engine)
    embedded=engine/'bin/Bloodborne Game.app/Contents/MacOS/bb-probe-metal'
    return embedded if embedded.is_file() and os.access(embedded,os.X_OK) else engine/'bin/bb-probe-metal'


def validate_development_runtime(engine):
    marker=b'BB_DEVELOPMENT_WORLD_WARPER_V1'
    if not (engine/'DEVELOPMENT-WORLD-WARPER.json').is_file() or marker not in executed_game_probe(engine).read_bytes():
        raise ValueError('World Warper requires the actual executed private development runtime')


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


class MemoryGuard:
    """Warning is telemetry, not an abort. Bound rapid and total swap growth."""
    def __init__(self, initial_swap, max_growth, max_total=DEFAULT_SWAP_TOTAL_MIB, rapid_window=6):
        if rapid_window not in (6,30):raise ValueError('Use a 6 or 30 second rapid swap window')
        self.rapid_window=rapid_window;self.soft_start=None;self.emergency_start=None
        self.initial_swap=initial_swap;self.max_growth=max_growth;self.max_total=max_total;self.history=[];self.fast_intervals=0
    def check(self, sample, now, measuring=False):
        if sample['level']>=4:
            raise RuntimeError('Memory guard: critical system pressure; no result accepted')
        if sample['swap_mib']>self.max_total:
            raise RuntimeError('Memory guard: absolute system swap limit; no result accepted')
        if sample['swap_mib']-self.initial_swap>self.max_growth:
            raise RuntimeError('Memory guard: total swap growth limit; no result accepted')
        if self.rapid_window==30:
            if self.history:
                t,swap=self.history[-1];elapsed=now-t
                rate=(sample['swap_mib']-swap)/elapsed if elapsed>0 else 0
                self.soft_start=(self.soft_start or (t,swap)) if rate>=32 else None
                self.emergency_start=(self.emergency_start or (t,swap)) if rate>=128 else None
            self.history=[(now,sample['swap_mib'])]
            for start,seconds,rate,label in ((self.soft_start,30,32,'sustained'),(self.emergency_start,6,128,'emergency')):
                if start:
                    elapsed=now-start[0];growth=sample['swap_mib']-start[1]
                    if elapsed>=seconds and growth>=1024 and growth/elapsed>=rate:
                        raise RuntimeError(f'Memory guard: {label} rapid swap growth (>=1 GiB at >={rate} MiB/s for >={seconds}s); no result accepted')
            return
        if self.history:
            elapsed=now-self.history[-1][0];growth=sample['swap_mib']-self.history[-1][1]
            self.fast_intervals=self.fast_intervals+1 if elapsed>0 and growth/elapsed>=32 else 0
        self.history=[(t,s) for t,s in self.history if now-t<=15]
        self.history.append((now,sample['swap_mib']))
        # Require both a meaningful byte increase and a sustained observation interval.
        for t,swap in self.history:
            elapsed=now-t;growth=sample['swap_mib']-swap
            if self.fast_intervals>=2 and elapsed>=6 and growth>=512 and growth/elapsed>=32:
                raise RuntimeError('Memory guard: rapid swap growth (>=512 MiB at >=32 MiB/s across consecutive intervals); no result accepted')


def authorization_deadline(value):
    """Require an explicit timezone; never infer the operator's local timezone."""
    try:
        parsed=datetime.fromisoformat(value)
        if parsed.tzinfo is None or parsed.utcoffset() is None:raise ValueError()
        stamp=parsed.timestamp()
        if not math.isfinite(stamp):raise ValueError()
        return stamp
    except (ValueError,OverflowError):
        raise argparse.ArgumentTypeError('Use an ISO-8601 deadline with timezone')


def check_authorization_deadline(deadline,now=None):
    if deadline is not None and (time.time() if now is None else now)>=deadline:
        raise RuntimeError('Authorization deadline reached; stop owned test')


class SessionMonitor:
    """Read lock state and online-display sleep without screen capture or UI control."""
    def __init__(self):
        self.cf=ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
        self.io=ctypes.CDLL('/System/Library/Frameworks/IOKit.framework/IOKit')
        self.cg=ctypes.CDLL('/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics')
        self.cf.CFStringCreateWithCString.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint32];self.cf.CFStringCreateWithCString.restype=ctypes.c_void_p
        self.cf.CFGetTypeID.argtypes=[ctypes.c_void_p];self.cf.CFGetTypeID.restype=ctypes.c_ulong
        self.cf.CFBooleanGetTypeID.restype=ctypes.c_ulong
        self.cf.CFBooleanGetValue.argtypes=[ctypes.c_void_p];self.cf.CFBooleanGetValue.restype=ctypes.c_bool
        self.cf.CFRelease.argtypes=[ctypes.c_void_p]
        self.io.IORegistryGetRootEntry.argtypes=[ctypes.c_uint32];self.io.IORegistryGetRootEntry.restype=ctypes.c_uint32
        self.io.IORegistryEntryCreateCFProperty.argtypes=[ctypes.c_uint32,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32];self.io.IORegistryEntryCreateCFProperty.restype=ctypes.c_void_p
        self.io.IOObjectRelease.argtypes=[ctypes.c_uint32]
        self.cg.CGGetOnlineDisplayList.argtypes=[ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)];self.cg.CGGetOnlineDisplayList.restype=ctypes.c_int32
        self.cg.CGDisplayIsAsleep.argtypes=[ctypes.c_uint32];self.cg.CGDisplayIsAsleep.restype=ctypes.c_bool
    def sample(self):
        root=self.io.IORegistryGetRootEntry(0)
        if not root:raise RuntimeError('Cannot inspect console lock state; refusing unmonitored benchmark')
        key=self.cf.CFStringCreateWithCString(None,b'IOConsoleLocked',0x08000100)
        value=None
        try:
            value=self.io.IORegistryEntryCreateCFProperty(root,key,None,0)
            if not value or self.cf.CFGetTypeID(value)!=self.cf.CFBooleanGetTypeID():
                raise RuntimeError('Console lock state unavailable; result invalid')
            locked=bool(self.cf.CFBooleanGetValue(value))
        finally:
            if value:self.cf.CFRelease(value)
            self.cf.CFRelease(key);self.io.IOObjectRelease(root)
        displays=(ctypes.c_uint32*32)();count=ctypes.c_uint32()
        if self.cg.CGGetOnlineDisplayList(32,displays,ctypes.byref(count))!=0 or not 0<count.value<=32:
            raise RuntimeError('Online display state unavailable; result invalid')
        return dict(locked=locked,all_displays_asleep=all(self.cg.CGDisplayIsAsleep(displays[i]) for i in range(count.value)),online_displays=count.value)


class SessionGuard:
    def __init__(self):self.last=None
    def check(self,sample,now,wall,sample_duration=0):
        if sample['locked'] or sample['all_displays_asleep']:
            raise RuntimeError('Session guard: console locked or all displays asleep; result invalid')
        if sample_duration>12:
            raise RuntimeError('Session guard: monitoring sample exceeded 12 seconds; result invalid')
        if self.last:
            mono_gap=now-self.last[0];wall_gap=wall-self.last[1]
            if mono_gap>12 or abs(wall_gap-mono_gap)>2:
                raise RuntimeError('Session guard: sleep or monitoring interruption; result invalid')
        self.last=(now,wall)


def check_gpu_faults(log):
    faults=[line for line in log.splitlines() if re.search(r'GPU fault|device lost|command buffer.*error|assertion failed|Execution of the command buffer was aborted',line,re.I)]
    if faults:raise RuntimeError(f'GPU faults: {faults[:2]}')


def display_snapshot(folder):
    files=list(folder.glob('f*_present_*_*.raw'))
    if len(files)!=1:raise ValueError('Expected one bounded display snapshot')
    p=files[0];m=re.fullmatch(r'f[0-9]+_present_([0-9]+)x([0-9]+)_(rgba|bgra)\.raw',p.name)
    if not m or p.is_symlink():raise ValueError('Invalid display snapshot path')
    width,height=int(m[1]),int(m[2]);size=width*height*4
    if not width or not height or size>32*1024**2 or p.stat().st_size!=size:
        raise ValueError('Display snapshot is incomplete or over 32 MiB')
    return dict(path=str(p),width=width,height=height,order=m[3],bytes=size,sha256=digest(p))


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
    stationary=all(max(float(r[k]) for r in recent)-min(float(r[k]) for r in recent)<=.05 for k in ('cam_x','cam_y','cam_z'))
    # A visually verified, pinned camera identifies sparse corridors too. Recorder draw
    # counts fluctuate at frame boundaries; do not reject or trim their slower frames.
    draws_valid = (statistics.mean(float(r['draws']) for r in recent)>300 if expected
                   else sum(float(r['draws'])>300 for r in recent)/len(recent)>=.95)
    return stationary and all(scene_matches(r,expected) for r in recent) and draws_valid


def metrics(rows, expected_camera=None):
    rows,missing_camera=camera_telemetry(rows)
    draws_valid = (all(scene_matches(r,expected_camera) for r in rows) and
                   statistics.mean(float(r['draws']) for r in rows)>300 if rows and expected_camera
                   else bool(rows) and sum(float(r['draws'])>300 for r in rows)/len(rows)>=.99)
    if len(rows) < 100 or not all(world_camera(row) for row in rows) or not draws_valid:
        raise ValueError('Measurement contains menus/loading or too few game frames')
    frames = sorted(float(r['frame_ms']) for r in rows)
    if not all(math.isfinite(t) and t > 0 for t in frames):
        raise ValueError('Invalid frame times')
    worst = max(1, math.ceil(len(frames) * .01))
    result = dict(frames=len(frames), missing_camera_samples=missing_camera, low_draw_frame_fraction=sum(float(r['draws'])<=300 for r in rows)/len(rows), fps=1000 / statistics.mean(frames), frame_ms_mean=statistics.mean(frames),
                  frame_ms_p50=statistics.median(frames), frame_ms_p95=frames[min(len(frames)-1, math.ceil(len(frames)*.95)-1)],
                  frame_ms_p99=frames[min(len(frames)-1, math.ceil(len(frames)*.99)-1)],
                  one_percent_low_fps=1000 / statistics.mean(frames[-worst:]), worst_frame_ms=frames[-1])
    for key in ('draws', 'dispatches', 'compiles', 'gpu_ms', 'rec_busy_ms', 'present_wait_ms', 'tick_wait_ms', 'gpu_wait_ms', 'replay_wait_ms', 'gnm_wait_ms', 'submit_age_ms', 'ready_age_ms', 'guest_age_ms', 'find_images', 'cam_x', 'cam_y', 'cam_z'):
        if any(key not in r or r[key]=='' for r in rows):
            result[key]=None;continue
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
    try:
        group = os.getpgid(process.pid)
    except ProcessLookupError:
        # A self-terminating owned helper (caffeinate -w) can exit after poll().
        # Reap and verify its terminal state instead of invalidating a good run.
        process.wait(timeout=5)
        return
    if group != process.pid:
        raise RuntimeError('Unexpected process group; refusing broad termination')
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.wait(timeout=5)
        return
    except PermissionError:
        # Capture tooling may add protected helper processes to the group. The
        # Popen-owned leader is still ours; never broaden termination to helpers.
        if process.poll() is not None:
            return
        os.kill(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=12)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except PermissionError:
            if process.poll() is None: os.kill(process.pid,signal.SIGKILL)
        process.wait(timeout=5)


def capture_frame(process, folder, guard, max_swap_growth_mib=1024, shared_memory=False):
    """Capture one layer frame of the owned game; never score capture-instrumented FPS."""
    if process.pid not in game_pids() or os.getpgid(process.pid) != process.pid:
        raise ValueError('Capture target is not the private game process')
    tool = shutil.which('gpucapture')
    if not tool:
        raise ValueError('Apple gpucapture is unavailable')
    boundaries = command([tool, 'boundaries', '--pid', str(process.pid)])
    (folder/'capture-boundaries.txt').write_text(boundaries)
    # The default selects a unique CAMetalLayer; without a layer it captures only one
    # command buffer, which must never be mistaken for a complete frame.
    if not re.search(r'\b(?:Layer|CAMetalLayer)\b', boundaries, re.I):
        raise ValueError('No layer boundary: refusing a partial command-buffer capture')
    config=command([tool,'config','--pid',str(process.pid)]) if shared_memory else ''
    if shared_memory:
        config+='\n'+command([tool,'config','--pid',str(process.pid),'--shared-mem-opt','enabled'])
        (folder/'capture-config.txt').write_text(config)
    output=folder/'gpu-frame.gputrace'
    args=[tool,'start','--pid',str(process.pid),'--count','1','--disable-unused-recording',
          '--disable-backtrace-recording','--output',str(output)]
    initial=pressure();started=time.monotonic();worker=None;limit=min(max_swap_growth_mib,1024)
    try:
        with (folder/'capture-tool.log').open('w') as log:
            worker=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            while worker.poll() is None:
                guard()
                p=pressure()
                if p['level']>=4 or p['swap_mib']-initial['swap_mib']>limit:
                    raise RuntimeError('Capture memory guard stopped this diagnostic')
                if time.monotonic()-started>45:
                    raise RuntimeError('Single-frame capture timed out')
                time.sleep(.5)
            if worker.returncode:
                raise RuntimeError('gpucapture failed; see capture-tool.log')
        guard()
        final=pressure()
        if final['level']>=4 or final['swap_mib']-initial['swap_mib']>limit:
            raise RuntimeError('Capture exceeded its memory guard')
        if not output.exists():
            raise RuntimeError('gpucapture did not produce a trace')
        return dict(valid=False,kind='single-layer-frame',trace=str(output),seconds=time.monotonic()-started,shared_memory_optimized=shared_memory,
                    command=args,notes=['Diagnostic only; not an FPS result.','Unused resources and backtraces omitted.'])
    finally:
        if worker is not None and worker.poll() is None:
            # Stop only this game's capture and the CLI process group we created.
            try: subprocess.run([tool,'stop','--pid',str(process.pid)],capture_output=True,timeout=5)
            finally: stop_owned(worker)


def watch_buffer_range(value):
    """Small original-guest range for existing CPU binding diagnostics, never scoring."""
    try:
        address_text, size_text = value.split(',')
        address, size = int(address_text,0), int(size_text,0)
    except (ValueError,TypeError):
        raise argparse.ArgumentTypeError('Use guest-address,size (decimal or 0x hex)') from None
    if not 0 < address < (1<<40) or not 1 <= size <= 4096 or address+size > (1<<40):
        raise argparse.ArgumentTypeError('Use a nonzero guest address and a 1..4096 byte range')
    return f'{address:#x},{size}'

def run(args):
    stop_at=getattr(args,'stop_at',None)
    check_authorization_deadline(stop_at)
    visual_observation=getattr(args,'visual_observation','unverified')
    diagnostic=any(getattr(args,key,False) for key in
                   ('capture_frame','capture_native','capture_submit_timing','record_route','control_session'))
    if visual_observation=='present' and not diagnostic:
        raise ValueError('Visual observation/recording is present; refuse a scored run')
    if 'BB_CAPTURE_FRAGMENT_GUARD=1' in getattr(args,'env',[]) and not getattr(args,'capture_native',False):
        raise ValueError('Fragment guard is native diagnostics only; never a performance score')
    if 'BB_CAPTURE_COPY_SCOPE=1' in getattr(args,'env',[]) and not getattr(args,'capture_native',False):
        raise ValueError('Copy scope inspection requires --capture-native; never a performance score')
    if 'BB_CAPTURE_COPY_POINTERS=1' in getattr(args,'env',[]) and not (
            getattr(args,'capture_native',False) and getattr(args,'watch_buffer',None)):
        raise ValueError('GPU pointer watch requires --capture-native and a bounded --watch-buffer; never scored')
    if 'BB_CAPTURE_SRT_READS=1' in getattr(args,'env',[]) and not (
            getattr(args,'capture_native',False) and getattr(args,'watch_buffer',None)):
        raise ValueError('SRT reads require --capture-native and a bounded --watch-buffer; never scored')
    if getattr(args,'watch_buffer',None) and not getattr(args,'capture_native',False):
        raise ValueError('Buffer watch overhead is native diagnostics only, never scored FPS')
    if getattr(args,'capture_on_entry',False) and not getattr(args,'capture_native',False):
        raise ValueError('Scene-entry capture is native diagnostics only, never a scored run')
    root = args.workspace.resolve()
    record_route = getattr(args, 'record_route', False)
    route_json = getattr(args, 'route_json', None)
    control_session = getattr(args, 'control_session', False)
    lock = (root/'run.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    manifest = json.loads((root/'cohort.json').read_text())
    routes = route = None
    if record_route or route_json or control_session:
        route_spec=importlib.util.spec_from_file_location('benchmark_routes',Path(__file__).with_name('benchmark_routes.py'))
        routes=importlib.util.module_from_spec(route_spec);route_spec.loader.exec_module(routes)
        if route_json:
            route=routes.read_route(route_json,manifest['cohort_fingerprint'])
            routes.validate_repeats(route,args.route_repeats)
    if cohort_fingerprint(manifest) != manifest['cohort_fingerprint'] or inventory(root/'seed-user/savedata') != manifest['seed_files'] or digest(root/'settings.ini') != manifest['ini_sha256']:
        raise ValueError('Frozen cohort changed; create a new cohort instead')
    for path, sha in manifest['external_configs'].items():
        if (digest(path) if Path(path).is_file() else None) != sha:
            raise ValueError('Mod/patch selections changed; create a new cohort')
    if game_pids():
        raise ValueError('Another game is running; refusing concurrent memory use')
    initial_pressure = pressure()
    if initial_pressure['level'] >= 4:
        raise ValueError('System is under critical memory pressure before boot')
    session_monitor=SessionMonitor()
    initial_session=session_monitor.sample()
    SessionGuard().check(initial_session,time.monotonic(),time.time())
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
    env = {k:v for k,v in os.environ.items() if not k.startswith(('BB_','MTLCAPTURE_')) and k not in ('MTL_CAPTURE_ENABLED','DYLD_LIBRARY_PATH','PYTHONHOME','PYTHONPATH','VK_DRIVER_FILES','VK_ICD_FILENAMES')}
    env.update(manifest['environment'])
    variants = {}
    for item in args.env:
        key, sep, value = item.partition('=')
        if not sep or key not in OVERRIDES or value not in ('0','1'):
            raise ValueError(f'Unsupported benchmark override: {item}')
        variants[key] = value
    if any(variants.get(key)=='1' for key in DIAGNOSTIC_OVERRIDES) and not args.capture_native:
        raise ValueError('Audit overhead is diagnostic only; use --capture-native, not a scored FPS run')
    env.update(variants)
    if getattr(args,'watch_buffer',None):
        env['BB_WATCH']=args.watch_buffer
    if variants.get('BB_FRAME_TIMING')=='1':
        if not args.minimal_stats or args.profile_gpu or args.capture_frame or args.capture_native:
            raise ValueError('CPU frame timing requires minimal telemetry without capture/stage profiling')
        if b'BB_FRAME_TIMING' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():
            raise ValueError('App lacks lightweight CPU timing support')
    env.update(BB_USER_DIR=str(folder/'user'), BB_DATA_DIR=str(folder/'data'), BB_CONFIG=str(folder/'data/bbport.ini'),
               BB_GPU_USER_DIR=str(root/'cache'), BB_PAD_FILE=str(pad), BB_PAD_FILE_ONLY='1', BB_FRAME_STATS='1',
               BB_TIMEOUT=str(args.startup_timeout+args.warmup+args.seconds+90))
    if getattr(args,'capture_submit_timing',False):
        if b'BB_MTL_SUBMIT_AUDIT' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():
            raise ValueError('App lacks bounded command-buffer timing')
        env.update(BB_MTL_SUBMIT_AUDIT=str(folder/'gpu-submits.csv'),BB_MTL_SUBMIT_TRIGGER=str(folder/'submit.trigger'))
    if args.minimal_stats:
        if b'BB_FRAME_LOG_ONLY' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():
            raise ValueError('App lacks lightweight frame telemetry support; update first')
        env.pop('BB_FRAME_STATS',None);env['BB_FRAME_LOG_ONLY']='1'
    if control_session:
        if b'display dump exceeds the 32 MiB diagnostic bound' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():
            raise ValueError('App lacks bounded display snapshots')
        env.update(BB_PRESENT_DUMP_TRIGGER=str(folder/'display.trigger'),BB_DUMP_DIR=str(folder),
                   BB_TIMEOUT=str(args.startup_timeout+args.warmup+args.control_seconds+180))
        if getattr(args,'world_warper',False):
            validate_development_runtime(engine)
            tool=folder/'user/world-warper';tool.mkdir()
            (folder/'user/.private-world-warper').write_text(manifest['cohort_fingerprint']+'\n')
            env['BB_DEV_WORLD_WARPER_DIR']=str(tool)
        if getattr(args,'texture_audit',False):
            if not (engine/'DEVELOPMENT-WORLD-WARPER.json').is_file() or b'BB_TEXTURE_AUDIT_DIR' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():
                raise ValueError('Texture audit requires an instrumented development app')
            audit=folder/'user/texture-audit';audit.mkdir()
            env['BB_TEXTURE_AUDIT_DIR']=str(audit)
    elif routes:
        if b'BB_PAD_RECORD' not in (engine/'bin/bb-probe-metal').read_bytes() or b'BB_PAD_REPLAY' not in (engine/'bin/bb-probe-metal').read_bytes():
            raise ValueError('App lacks route recording/replay support')
        if record_route:
            env.update(BB_PAD_RECORD=str(folder/'route.pad'),BB_PAD_FILE_ONLY='0')
            route_budget=600
        else:
            shutil.copy2(route['pad_path'],folder/'replay.pad')
            env['BB_PAD_REPLAY']=str(folder/'replay.pad')
            route_budget=(route['seconds']+args.route_settle+2)*args.route_repeats
        env['BB_TIMEOUT']=str(math.ceil(args.startup_timeout+args.warmup+route_budget+180))
    if args.mute:
        if b'BB_BENCHMARK_MUTE' not in (engine/'bin/bb-probe-metal').read_bytes():
            raise ValueError('App lacks per-stream benchmark mute support; refusing an audible run')
        env['BB_BENCHMARK_MUTE']='1'
    if args.profile_gpu:
        env.update(BB_MTL_GPU_TIMING='1', BB_MTL_GPU_TIMING_EVERY='8')
    if args.capture_frame:
        env['MTL_CAPTURE_ENABLED']='1'
    if args.capture_native and args.capture_count>1 and b'Frame capture: native series member' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():
        raise ValueError('App lacks unique bounded native capture series support; update first')
    if args.capture_native:
        env.update(BB_CAPTURE_NATIVE='1',BB_CAPTURE_DIR=str(folder),BB_CAPTURE_TRIGGER=str(folder/'capture.trigger'),BB_PASS_LABELS='1')
        if getattr(args,'capture_consecutive',False):
            if b'Frame capture: consecutive chain' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():
                raise ValueError('App lacks consecutive native capture support')
            env['BB_CAPTURE_CONSECUTIVE_COUNT']=str(args.capture_count)
        if args.capture_display:
            if b'display dump exceeds the 32 MiB diagnostic bound' not in (engine/'bin/gpu-metal/libbbgpu.dylib').read_bytes():raise ValueError('App lacks bounded display snapshot support; update first')
            env.update(BB_PRESENT_DUMP_TRIGGER=str(folder/'display.trigger'),BB_DUMP_DIR=str(folder))
    log_file = (folder/'run.log').open('w')
    source_before = inventory(manifest['source_save'])
    process = None; awake_process=None; completed_ok=False; samples=[]; started=time.monotonic();last_guard=0;next_press=None;held=False;presses=0;world=False;input_pause_until=0;left_title=False
    log_path=folder/'run.log';frames=folder/'data/out/frames.csv'
    shared_baseline=getattr(args,'swap_budget_baseline_mib',None)
    total_limit=getattr(args,'max_swap_total_mib',DEFAULT_SWAP_TOTAL_MIB)
    rapid_window=getattr(args,'swap_rapid_window_seconds',6)
    memory_guard=MemoryGuard(initial_pressure['swap_mib'] if shared_baseline is None else shared_baseline,args.max_swap_growth_mib,total_limit,rapid_window);measuring=False
    memory_guard.check(initial_pressure,started)
    session_guard=SessionGuard()
    def guard():
        nonlocal last_guard
        check_authorization_deadline(stop_at)
        if process.poll() is not None:
            raise RuntimeError(f'Benchmark game exited ({process.returncode}); see {log_path}')
        now=time.monotonic()
        if now-last_guard<3:
            return
        # Compare clocks taken at the SAME instant. sysctl/VM/log reads can block:
        # mixing a pre-sample monotonic time with a post-sample wall time mistook
        # a 2.4-second sampling delay for sleep, despite uninterrupted guard calls.
        sample_wall=time.time()
        last_guard=now;p=pressure();session=session_monitor.sample()
        lines=log_path.read_text(errors='replace').splitlines()
        totals=[s for s in lines if s.startswith('Metal memory totals:')][-1:]
        sample=dict(elapsed=now-started,wall=sample_wall,pressure=p,memory=totals,session=session,phase='score' if measuring else 'startup-warmup')
        if routes:
            sample['log_bytes']=log_path.stat().st_size
            sample['vm']=routes.vm_counters(command(['vm_stat']))
        sample['sampler_seconds']=time.monotonic()-now
        samples.append(sample)
        write_json(folder/'system-samples.json',samples)
        session_guard.check(session,now,sample_wall,sample['sampler_seconds'])
        memory_guard.check(p,now,measuring)
        if totals and int(re.search(r'footprint=(\d+)',totals[0])[1]) > 9*1024**3:
            raise RuntimeError('Game footprint exceeded 9 GiB guard')
    def interrupt(signum, frame):
        raise KeyboardInterrupt(f'Benchmark interrupted by signal {signum}')
    old_signals={sig:signal.signal(sig,interrupt) for sig in (signal.SIGTERM,signal.SIGINT)}
    try:
        entry=routes.recording_entry(engine,folder) if record_route else engine/'tools/macos/run_optimized.sh'
        check_authorization_deadline(stop_at)
        process=subprocess.Popen(['bash',str(entry),'--benchmark'],env=env,stdout=log_file,stderr=subprocess.STDOUT,start_new_session=True)
        # Owned, temporary idle-sleep assertion; manual lock still invalidates the run.
        awake_process=subprocess.Popen(['/usr/bin/caffeinate','-di','-w',str(process.pid)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
        write_json(folder/'owner.json',dict(pid=process.pid,created=time.time(),app=str(app),idle_assertion_pid=awake_process.pid,guard_policy=GUARD_POLICY,initial_pressure=initial_pressure,initial_session=initial_session,max_swap_growth_mib=args.max_swap_growth_mib,max_swap_total_mib=total_limit,swap_rapid_window_seconds=rapid_window,stop_at=stop_at,swap_budget_baseline_mib=memory_guard.initial_swap,shared_swap_budget_baseline_mib=shared_baseline))
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
        if args.capture_on_entry:
            print('Scene detected; capturing diagnostic metadata before warmup (not an FPS score)',flush=True)
        else:
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
        if routes:
            measuring=True;last_guard=0;guard()
            if control_session:
                control_spec=importlib.util.spec_from_file_location('benchmark_control',Path(__file__).with_name('benchmark_control.py'))
                control=importlib.util.module_from_spec(control_spec);control_spec.loader.exec_module(control)
                control.execute(args,folder,frames,pad,guard,manifest,write_json,routes.FrameTail)
            else:
                routes.execute(args,folder,frames,pad,guard,manifest,write_json)
            check_gpu_faults(log_path.read_text(errors='replace'))
            for name in ('route.json','route-result.json','control-result.json'):
                if (folder/name).exists():
                    metadata=json.loads((folder/name).read_text())
                    metadata.update(renderer_sha256=digest(engine/'bin/gpu-metal/libbbgpu.dylib'),
                                    probe_sha256=digest(engine/'bin/bb-probe-metal'),executed_probe_sha256=digest(executed_game_probe(engine)),variants=variants,
                                    settings_sha256=manifest['ini_sha256'],guard_policy=GUARD_POLICY,
                                    max_swap_growth_mib=args.max_swap_growth_mib,max_swap_total_mib=total_limit,swap_rapid_window_seconds=rapid_window,stop_at=stop_at,shared_swap_budget_baseline_mib=shared_baseline,telemetry_mode='minimal',
                                    route_settle_seconds=args.route_settle,profile_gpu=False)
                    write_json(folder/name,metadata)
            completed_ok=True
            return
        if getattr(args,'capture_submit_timing',False):
            print('Recording bounded command-buffer timing; diagnostic only, not an FPS score',flush=True)
            (folder/'submit.trigger').write_text('1')
            trace=folder/'gpu-submits.csv';deadline=time.monotonic()+60
            while time.monotonic()<deadline:
                guard()
                if trace.exists() and trace.stat().st_size<512*1024:
                    rows=list(csv.DictReader(trace.read_text().splitlines()))
                    if len(rows)==2048 and all(r.get('end_s') for r in rows):break
                time.sleep(.2)
            else:raise RuntimeError('Bounded command-buffer timing did not complete')
            for row in rows:
                times=[float(row[k]) for k in ('commit_s','start_s','end_s')]
                if not all(math.isfinite(v) for v in times) or not 0<times[0]<=times[1]<=times[2]:
                    raise ValueError('Invalid command-buffer clock pair')
            check_gpu_faults(log_path.read_text(errors='replace'))
            write_json(folder/'capture.json',dict(valid=False,kind='bounded-command-buffer-timing',trace=str(trace),
                entries=len(rows),cohort_fingerprint=manifest['cohort_fingerprint'],variants=variants,
                renderer_sha256=digest(engine/'bin/gpu-metal/libbbgpu.dylib'),
                notes=['2048 CPU-enqueue/GPU start/end records; <=128 KiB CPU metadata, no GPU readback or counters.',
                       'Intervals include queue dependencies and are not shader execution time or GPU utilization.',
                       'Offline union must sort GPU intervals; callback order need not match timestamp order.',
                       'Diagnostic run only; file completion is outside a scored FPS result.']))
            completed_ok=True
            return
        if args.capture_native:
            print(f'Recording {args.capture_count} decoded GPU frame(s): bounded attachment metadata'+(' and one display image' if args.capture_display else ' only'),flush=True)
            members=[]
            if getattr(args,'capture_consecutive',False):
                previous=set(folder.glob('frame_*.txt'))
                (folder/'capture.trigger').write_text('1')
                capture_deadline=time.monotonic()+10
                while time.monotonic()<capture_deadline:
                    guard()
                    fresh=set(folder.glob('frame_*.txt'))-previous
                    if len(fresh)>args.capture_count:raise ValueError('Unexpected extra chain capture')
                    log=log_path.read_text(errors='replace')
                    complete=[p for p in fresh if f'passes written to {p}' in log]
                    if len(complete)==args.capture_count:
                        paths=sorted(complete,key=lambda p:int(p.stem.rsplit('_',1)[1]))
                        summaries=validate_consecutive_captures([p.read_text() for p in paths])
                        members=[dict(path=str(p),frame=s['frame'],bytes=s['metadata_bytes']) for p,s in zip(paths,summaries)]
                        break
                    time.sleep(.1)
                else:raise RuntimeError('Consecutive native attachment capture timed out')
            for capture_index in range(0 if getattr(args,'capture_consecutive',False) else args.capture_count):
                previous=set(folder.glob('frame_*.txt'))
                (folder/'capture.trigger').write_text('1')
                if args.capture_display:(folder/'display.trigger').write_text('1')
                capture_deadline=time.monotonic()+10
                while time.monotonic()<capture_deadline:
                    guard()
                    fresh=set(folder.glob('frame_*.txt'))-previous
                    log=log_path.read_text(errors='replace')
                    complete=[p for p in fresh if f'passes written to {p}' in log]
                    if len(complete)==1 and (not args.capture_display or 'display buffer dumped to' in log):
                        captured_path=complete[0];break
                    time.sleep(.1)
                else:raise RuntimeError('Native attachment capture timed out')
                summary=validate_native_capture(captured_path.read_text())
                members.append(dict(path=str(captured_path),frame=summary['frame'],bytes=summary['metadata_bytes']))
                check_gpu_faults(log_path.read_text(errors="replace"))
                print(f'Attachment metadata captured: {captured_path}',flush=True)
            snapshot=display_snapshot(folder) if args.capture_display else None
            write_json(folder/'capture.json',dict(valid=False,kind='single-guest-frame-attachment-metadata' if len(members)==1 else 'bounded-native-attachment-series',consecutive=getattr(args,'capture_consecutive',False),watch_buffer=getattr(args,'watch_buffer',None),capture_phase='scene-entry' if args.capture_on_entry else 'warmed',warmup_completed=not args.capture_on_entry,display=snapshot,trace=members[0]['path'],traces=members,scene_profile=manifest['profile'],cohort_fingerprint=manifest['cohort_fingerprint'],renderer_sha256=digest(engine/'bin/gpu-metal/libbbgpu.dylib'),variants=variants,telemetry_mode='minimal' if args.minimal_stats else 'full',audio_muted=args.mute,notes=['Decoded guest GPU frame(s), not a replayable GPUTools trace.','At most three metadata frames; each bounded to 8192 records / 2 MiB.','Attachment metadata has no resource contents; optional adjacent display snapshot is bounded to 32 MiB and only checks the final image.', 'Audio driver: '+env.get('SDL_AUDIO_DRIVER','default'),'Not a performance result.']))
            completed_ok=True
            return
        if args.capture_frame:
            print('Capturing one layer frame; diagnostic FPS is not scored',flush=True)
            captured=capture_frame(process,folder,guard,args.max_swap_growth_mib,args.capture_shared_memory)
            if args.capture_shared_memory:
                captured['notes'].append('Forced shared-memory optimization: API/attachment metadata audit only; syscall-written resource contents may be incomplete. Do not treat replay pixels or profiling as validated.')
            captured.update(scene_profile=manifest['profile'],cohort_fingerprint=manifest['cohort_fingerprint'],
                            renderer_sha256=digest(engine/'bin/gpu-metal/libbbgpu.dylib'),variants=variants,telemetry_mode='minimal' if args.minimal_stats else 'full',audio_muted=args.mute)
            write_json(folder/'capture.json',captured)
            print(f"Single frame captured: {captured['trace']}",flush=True)
            completed_ok=True
            return
        measuring=True;last_guard=0;guard()
        first_t=float(frame_rows(frames)[-1]['t_s']);measure_start=time.monotonic()
        write_json(folder/'measurement-window.json',dict(first_t=first_t,seconds=args.seconds,elapsed_start=measure_start-started,normal_pressure_required=False,guard_policy=GUARD_POLICY,visual_observation=visual_observation,visual_observation_source='operator-declaration'))
        print(f'Measuring {args.seconds}s; GPU stage timing={args.profile_gpu}',flush=True)
        while time.monotonic()-measure_start<args.seconds:
            guard();time.sleep(.5)
        rr=frame_rows(frames);selected=[r for r in rr if float(r['t_s'])>first_t]
        stats=metrics(selected,manifest['expected_camera'])
        if stats['compiles']['max']>0:
            raise ValueError('Shader compilation occurred during measurement; not a warm-cache result')
        camera=[stats[k]['mean'] for k in ('cam_x','cam_y','cam_z')]
        expected=manifest['expected_camera']
        if expected and math.dist(camera,expected)>.15:
            raise ValueError(f'Loaded a different location: {camera}, expected {expected}')
        if any(stats[k]['max']-stats[k]['min']>.05 for k in ('cam_x','cam_y','cam_z')):
            raise ValueError('Player moved during static-scene measurement')
        log=log_path.read_text(errors='replace')
        check_gpu_faults(log)
        result=dict(schema=1,valid=False,name=args.name,scene_profile=manifest['profile'],cohort_fingerprint=manifest['cohort_fingerprint'],
                    visual_observation=visual_observation,visual_observation_source='operator-declaration',
                    profile_gpu=args.profile_gpu,telemetry_mode='minimal' if args.minimal_stats else 'full',audio_driver=env.get('SDL_AUDIO_DRIVER','default'),audio_muted=args.mute,metrics=stats,app_version=plistlib.loads((app/'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'],
                    renderer_sha256=digest(engine/'bin/gpu-metal/libbbgpu.dylib'),probe_sha256=digest(engine/'bin/bb-probe-metal'),executed_probe_sha256=digest(executed_game_probe(engine)),
                    variants=variants,settings_sha256=manifest['ini_sha256'],camera=camera,samples=samples,guard_policy=GUARD_POLICY,max_swap_growth_mib=args.max_swap_growth_mib,max_swap_total_mib=total_limit,swap_rapid_window_seconds=rapid_window,stop_at=stop_at,shared_swap_budget_baseline_mib=shared_baseline,
                    system=dict(model=command(['sysctl','-n','hw.model']).strip(),os=command(['sw_vers','-productVersion']).strip()),
                    duration=time.monotonic()-started,notes=['Static gameplay benchmark, not whole-game average.','1% low = reciprocal of mean of slowest ceil(1% of frames).','GPU timing is opt-in; compare only matching instrumentation modes.', 'gpu_ms is legacy callback-order interval coverage and may undercount across queues; it is not shader time or GPU utilization. Use bounded submit timing for sorted interval analysis.'])
        write_json(folder/'result.json',result)
        print(json.dumps(dict(result=str(folder/'result.json'),fps=stats['fps'],one_percent_low_fps=stats['one_percent_low_fps'],frame_ms_p95=stats['frame_ms_p95'])),flush=True)
        completed_ok=True
    except BaseException as e:
        write_json(folder/'failure.json',dict(error=str(e),seconds=time.monotonic()-started))
        raise
    finally:
        pad.write_text(NEUTRAL)
        stop_error=None
        try:
            if process is not None: stop_owned(process)
        except Exception as error:
            stop_error=str(error)
        finally:
            try:
                if awake_process is not None:stop_owned(awake_process)
            except Exception as error:
                stop_error=f'{stop_error or ""} Idle assertion cleanup: {error}'
            log_file.close()
            for sig, handler in old_signals.items(): signal.signal(sig,handler)
        unchanged=inventory(manifest['source_save'])==source_before
        write_json(folder/'cleanup.json',dict(own_process_stopped=stop_error is None,stop_error=stop_error,original_saves_unchanged=unchanged,launcher_preferences_written=False))
        if not unchanged:
            (folder/'result.json').unlink(missing_ok=True)
            (folder/'capture.json').unlink(missing_ok=True)
            raise RuntimeError('Original save changed externally; result invalidated')
        if stop_error:
            raise RuntimeError(f'Process cleanup incomplete; results invalid: {stop_error}')
        for name in ('result.json','capture.json','route.json','route-result.json','control-result.json'):
            if completed_ok and (folder/name).exists():
                completed=json.loads((folder/name).read_text());completed['valid']=True;write_json(folder/name,completed)
        print('Own process stopped; original save hashes unchanged; this tool did not write launcher preferences',flush=True)


def compare(args):
    a=json.loads(args.before.read_text());b=json.loads(args.after.read_text())
    if 'metrics' not in a or 'metrics' not in b:
        raise ValueError('Diagnostic captures are not performance results')
    visual_modes=[r.get('visual_observation','unverified') for r in (a,b)]
    if 'present' in visual_modes:
        raise ValueError('Visual observation/recording contaminated a scored result')
    if any(mode not in ('absent','unverified') for mode in visual_modes) or visual_modes[0]!=visual_modes[1]:
        raise ValueError('Different or unknown visual observation policies; refusing comparison')
    if any(result.get('variants',{}).get(key)=='1' for result in (a,b) for key in DIAGNOSTIC_OVERRIDES):
        raise ValueError('Audit overhead is diagnostic only; refusing FPS comparison')
    if a.get('variants',{}).get('BB_FRAME_TIMING','0')!=b.get('variants',{}).get('BB_FRAME_TIMING','0'):
        raise ValueError('Different CPU timing instrumentation; refusing comparison')
    if not a['valid'] or not b['valid'] or a['cohort_fingerprint']!=b['cohort_fingerprint'] or a['profile_gpu']!=b['profile_gpu'] or a['system']!=b['system']:
        raise ValueError('Unmatched workloads, instrumentation or host; refusing comparison')
    if a.get('telemetry_mode','full')!=b.get('telemetry_mode','full'):
        raise ValueError('Different frame telemetry overhead; refusing comparison')
    guard_keys=('guard_policy','max_swap_growth_mib','max_swap_total_mib','shared_swap_budget_baseline_mib','swap_rapid_window_seconds')
    if tuple(a.get(k) for k in guard_keys)!=tuple(b.get(k) for k in guard_keys):
        raise ValueError('Different memory/session guard policies; refusing comparison')
    if a.get('audio_driver','default')!=b.get('audio_driver','default') or a.get('audio_muted',False)!=b.get('audio_muted',False):
        raise ValueError('Different audio backends; refusing unlike process workloads')
    if math.dist(a['camera'],b['camera'])>.05:
        raise ValueError('Different player positions')
    draw_delta=(b['metrics']['draws']['mean']/a['metrics']['draws']['mean']-1)*100
    if abs(draw_delta)>2:
        raise ValueError('Draw count changed by over 2%; inspect camera/workload first')
    print(json.dumps(dict(before=a['metrics'],after=b['metrics'],fps_delta_percent=(b['metrics']['fps']/a['metrics']['fps']-1)*100,draw_count_delta_percent=draw_delta,
                         visual_observation=visual_modes[0],visual_observation_source='operator-declaration',
                         notes='Use repeated interleaved runs; one pair does not establish statistical significance. '+
                         ('Visual capture state was not recorded; this comparison is not certified free of visual observation overhead.' if visual_modes[0]=='unverified' else
                          'Operator declares no visual observation/recording in either scoring window; external capture is not automatically detected.')),indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('init');p.add_argument('--workspace',type=Path,required=True);p.add_argument('--launcher-data',type=Path,default=DEFAULT_DATA);p.add_argument('--expected-camera',nargs=3,type=float);p.set_defaults(func=init)
    p=sub.add_parser('run');p.add_argument('--workspace',type=Path,required=True);p.add_argument('--app',type=Path,default=DEFAULT_APP);p.add_argument('--name',required=True);p.add_argument('--warmup',type=int,default=45);p.add_argument('--seconds',type=int,default=45);p.add_argument('--startup-timeout',type=int,default=300);p.add_argument('--max-swap-growth-mib',type=int,default=DEFAULT_SWAP_GROWTH_MIB);p.add_argument('--profile-gpu',action='store_true');p.add_argument('--mute',action='store_true');p.add_argument('--minimal-stats',action='store_true');p.add_argument('--capture-frame',action='store_true');p.add_argument('--capture-native',action='store_true');p.add_argument('--capture-count',type=int,default=1);p.add_argument('--watch-buffer',type=watch_buffer_range);p.add_argument('--capture-on-entry',action='store_true');p.add_argument('--capture-display',action='store_true');p.add_argument('--capture-shared-memory',action='store_true');p.add_argument('--env',action='append',default=[]);p.set_defaults(func=run)
    p.add_argument('--capture-submit-timing',action='store_true',help='Bounded CPU-enqueue/GPU interval metadata; never an FPS score')
    p.add_argument('--max-swap-total-mib',type=int,default=DEFAULT_SWAP_TOTAL_MIB,help='Absolute system swap ceiling (default 24 GiB); critical pressure and rapid-growth stops remain active')
    p.add_argument('--swap-rapid-window-seconds',type=int,choices=(6,30),default=6,help='30s requires continuous >=32MiB/s plus 1GiB; retains emergency >=128MiB/s for 6s plus 1GiB')
    p.add_argument('--stop-at',type=authorization_deadline,help='Stop owned test at this ISO-8601 timestamp, including timezone')
    p.add_argument('--swap-budget-baseline-mib',type=float,help='Keep a shared original baseline across a test batch instead of resetting growth allowance each run')
    p.add_argument('--visual-observation',choices=('absent','present','unverified'),default='unverified',help='Operator declaration for the full scoring window, not automatic recorder detection; use absent only with screenshots, screen recording and remote visual observation stopped')
    p.add_argument('--capture-consecutive',action='store_true',help='One trigger for 2..3 consecutive native encoding frames; diagnostic only')
    route_group=p.add_mutually_exclusive_group()
    route_group.add_argument('--record-route',action='store_true',help='Private save only: F9 starts/stops live input recording after warmup')
    route_group.add_argument('--route-json',type=Path,help='Replay a validated route recorded from this cohort')
    route_group.add_argument('--control-session',action='store_true',help='Private bounded pad-input session for lamp travel diagnostics')
    p.add_argument('--world-warper',action='store_true',help='Development app only: native lamp travel in a private control session')
    p.add_argument('--texture-audit',action='store_true',help='Development control session only: on-demand texture metadata, no pixels or FPS score')
    p.add_argument('--control-seconds',type=int,default=1200)
    p.add_argument('--route-repeats',type=int,default=1)
    p.add_argument('--route-settle',type=int,default=60,help='Neutral seconds after each lap, with memory observations')
    p=sub.add_parser('compare');p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.set_defaults(func=compare)
    args=parser.parse_args()
    if args.command=='run' and args.world_warper and not args.control_session:
        parser.error('--world-warper requires --control-session')
    if args.command=='run' and args.texture_audit and not args.control_session:
        parser.error('--texture-audit requires --control-session')
    if args.command=='run' and not (15<=args.warmup<=180 and 15<=args.seconds<=180 and 30<=args.startup_timeout<=600 and 256<=args.max_swap_growth_mib<=DEFAULT_SWAP_GROWTH_MIB and 10240<=args.max_swap_total_mib<=32768):
        parser.error('Use bounded warmup/sample (15..180s), startup (30..600s), swap growth (256..12288 MiB) and system swap ceiling (10240..32768 MiB)')
    if args.command=='run' and args.swap_budget_baseline_mib is not None and not (math.isfinite(args.swap_budget_baseline_mib) and 0<=args.swap_budget_baseline_mib<args.max_swap_total_mib):
        parser.error('Shared swap baseline must be finite, nonnegative and below the absolute ceiling')
    if args.command=='run' and (args.capture_frame and args.capture_native):
        parser.error('Choose either resource capture or native attachment metadata')
    if args.command=='run' and args.capture_submit_timing and (not args.minimal_stats or args.profile_gpu or args.capture_native or args.capture_frame or args.capture_on_entry or args.capture_display or args.capture_count!=1 or args.record_route or args.route_json or args.control_session):
        parser.error('Submit timing requires minimal telemetry and no other capture/profiling/route mode')
    if args.command=='run' and (not 1<=args.capture_count<=3 or (args.capture_count!=1 and (not args.capture_native or args.capture_display))):
        parser.error('--capture-count is 1..3; multiple frames require native metadata only, without display readback')
    if args.command=='run' and args.capture_consecutive and (not args.capture_native or args.capture_count<2 or args.capture_display):
        parser.error('--capture-consecutive requires 2..3 native metadata frames without display readback')
    if args.command=='run' and args.capture_display and not args.capture_native:
        parser.error('--capture-display requires --capture-native and never produces an FPS score')
    if args.command=='run' and args.capture_on_entry and not args.capture_native:
        parser.error('--capture-on-entry is native diagnostic metadata only; never an FPS score')
    if args.command=='run' and args.capture_shared_memory and not args.capture_frame:
        parser.error('--capture-shared-memory requires --capture-frame')
    if args.command=='run' and (args.record_route or args.route_json or args.control_session):
        if args.capture_native or args.capture_frame or args.profile_gpu:
            parser.error('Route memory diagnostics cannot enable capture or GPU stage timing')
        if not args.minimal_stats or not 1<=args.route_repeats<=6 or not 15<=args.route_settle<=120:
            parser.error('Routes require --minimal-stats, 1..6 repeats and 15..120s settling')
        if not 60<=args.control_seconds<=3600:
            parser.error('Control sessions must be bounded to 60..3600 seconds')
    try: args.func(args)
    except Exception as e:
        print(f'Benchmark failed: {e}',file=sys.stderr);return 1
    return 0


if __name__=='__main__':
    raise SystemExit(main())
