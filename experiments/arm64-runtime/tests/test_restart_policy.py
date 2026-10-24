"""No game contents. Request authenticity, offline policy and save transaction tests."""
from pathlib import Path
import sys,tempfile,subprocess,os,json,importlib.util
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from restart_policy import take_request,offline_environment,OFFLINE_KEYS,clone_private_session,fresh_travel_controls
spec=importlib.util.spec_from_file_location('checkpoint_helper',P/'vendor/blood_echoes/quicksave.py');q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
with tempfile.TemporaryDirectory(prefix='restart-test-',dir=P/'state') as name:
    state=Path(name);request=state/'.arm-restart-request';nonce='d'*32
    env=dict(PATH='/usr/bin:/bin',BB_USER_DIR=str(state),BB_ARM_RESTART_REQUEST=str(request),BB_ARM_RESTART_NONCE=nonce,BB_QUICKLOAD_PENDING='1')
    run=lambda e:subprocess.run([P/'build/fex/Bin/test_native_restart'],env=e,capture_output=True,text=True,timeout=10)
    r=run(env);assert r.returncode==85 and 'backend reaped' in r.stdout and take_request(request,nonce)
    env.pop('BB_QUICKLOAD_PENDING');r=run(env);assert r.returncode==85 and not take_request(request,nonce)
    request.write_text('ARMRESTART1\n'+'c'*32+'\n1\n')
    try:take_request(request,nonce);raise AssertionError('bad nonce accepted')
    except ValueError:pass
    assert request.exists() and run(env).returncode==78;request.unlink()
    outside=state/'outside';outside.write_text('protected fixture');request.symlink_to(outside)
    assert run(env).returncode==78 and outside.read_text()=='protected fixture'
    try:take_request(request,nonce);raise AssertionError('symlink accepted')
    except ValueError:pass
    request.unlink();bad=env|dict(BB_ARM_RESTART_REQUEST=str(outside));assert run(bad).returncode==78 and outside.read_text()=='protected fixture'
    assert run(env|dict(BB_ARM_RESTART_NONCE='bad')).returncode==78
    online={k:'dummy-authored-secret' for k in OFFLINE_KEYS};online.update(BB_ONLINE='1',BB_QUICKSAVE='1',BB_GPU_USER_DIR='same-cache',BB_LAYER_MEMORY='1',BB_ARM_ALLOW_NETWORK='1',BB_BENCHMARK_MUTE='1')
    offline=offline_environment(online,True);assert all(k not in offline for k in OFFLINE_KEYS) and offline['BB_ONLINE']=='0' and offline['BB_QUICKLOAD_PENDING']=='1' and offline['BB_GPU_USER_DIR']=='same-cache' and offline['BB_LAYER_MEMORY']=='1' and offline['BB_ARM_ALLOW_NETWORK']=='0' and offline['BB_BENCHMARK_MUTE']=='1'
    # Use the unchanged production helper on authored files. Restore must retain
    # the displaced save, and no restore source is selected from any QA seed.
    live=state/'savedata/1/CUSA03173/SPRJ0005';live.mkdir(parents=True)
    (live/'userdata0000').write_bytes(b'authored character A');(live/'userdata0010').write_bytes(b'authored profile A')
    saves=q.Checkpoints(state,'CUSA03173')
    with saves.locked():checkpoint=saves.save()
    (live/'userdata0000').write_bytes(b'authored character B')
    with saves.locked():backup=saves.load()
    assert (live/'userdata0000').read_bytes()==b'authored character A'
    assert (backup/'data/SPRJ0005/userdata0000').read_bytes()==b'authored character B'
    print('PASS native supervisor request, reject forged/replayed/linked requests, offline credentials, checkpoint retained restore')

with tempfile.TemporaryDirectory(prefix='session-clone-',dir=P/'state') as name:
    root=Path(name);source=root/'source';source.mkdir();(source/'.arm-runtime-session').write_text('ARMQA1\nboot\napp0\ncontent\n'+'a'*32+'\n');(source/'progress').write_text('preserved source')
    assert clone_private_session(source,root/'new',root)=='a'*32
    (root/'new/progress').write_text('new QA writes');assert (source/'progress').read_text()=='preserved source'
    try:clone_private_session(source,root/'new',root);raise AssertionError('existing target accepted')
    except ValueError:pass
    (source/'bad-link').symlink_to(source/'progress')
    try:clone_private_session(source,root/'bad',root);raise AssertionError('linked source accepted')
    except ValueError:pass
    print('PASS private QA clone preserves prior evidence and refuses linked/existing targets')


with tempfile.TemporaryDirectory(prefix='travel-controls-',dir=P/'state') as name:
    root=Path(name);source=root/'source';source.mkdir()
    (source/'.arm-runtime-session').write_text('ARMQA1\nboot\napp0\ncontent\n'+'b'*32+'\n')
    (source/'savedata').mkdir();(source/'savedata/progress').write_bytes(b'normal-save')
    (source/'quicksaves').mkdir();(source/'quicksaves/checkpoint').write_bytes(b'checkpoint')
    (source/'world-warper').mkdir();(source/'world-warper/armed').write_text('b'*32)
    (source/'world-warper/command.txt').write_text('6 2412951\n')
    (source/'.private-world-warper').write_text('b'*32);(source/'.private-test-finished').write_text('b'*32)
    before={str(f.relative_to(source)):f.read_bytes()for f in source.rglob('*')if f.is_file()}
    clone=root/'new';clone_private_session(source,clone,root)
    travel=fresh_travel_controls(clone,root)
    assert travel==clone/'world-warper'and not list(travel.iterdir())
    assert not(clone/'.private-test-finished').exists()and not(clone/'.private-world-warper').exists()
    assert(clone/'.arm-runtime-session').read_bytes()==before['.arm-runtime-session']
    assert(clone/'savedata/progress').read_bytes()==b'normal-save'and(clone/'quicksaves/checkpoint').read_bytes()==b'checkpoint'
    archives=list(clone.glob('.qa-prior-travel-controls-*'));assert len(archives)==1
    assert(archives[0]/'world-warper/command.txt').read_text()=='6 2412951\n'
    assert(archives[0]/'.private-test-finished').read_text()=='b'*32
    assert before=={str(f.relative_to(source)):f.read_bytes()for f in source.rglob('*')if f.is_file()}
    bad=root/'bad';bad.mkdir();(bad/'world-warper').mkdir();(bad/'world-warper/link').symlink_to(source/'savedata/progress')
    try:fresh_travel_controls(bad,root);raise AssertionError('linked nested controls accepted')
    except ValueError:pass
    assert(bad/'world-warper/link').is_symlink()and not list(bad.glob('.qa-prior-travel-controls-*'))
    outside=root/'other-root';outside.mkdir()
    try:fresh_travel_controls(clone,outside);raise AssertionError('outside state accepted')
    except ValueError:pass
    print('PASS fresh private travel controls archive old instructions, preserve source/saves/identity, reject nested links/outside root')
