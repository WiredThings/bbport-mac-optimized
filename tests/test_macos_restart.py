"""Exercise process death, one-shot quickload forwarding and nested bundle identity."""
from paths import ROOT
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(ROOT / 'tools/macos'))
from game_app import create_game_app, GAME_BUNDLE_ID, GAME_ICON


class MacOSRestartTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, sequence):
        tools = self.root / 'tools/macos'
        tools.mkdir(parents=True)
        supervisor = tools / 'supervise_restart.sh'
        shutil.copy2(ROOT / 'tools/macos/supervise_restart.sh', supervisor)
        (self.root / 'run.sh').write_text('exec "$TEST_PYTHON" worker.py\n')
        (self.root / 'worker.py').write_text('''import json,os,sys
from pathlib import Path
p=Path('state.json'); d=json.loads(p.read_text()) if p.exists() else []
if d:
    try: os.kill(d[-1]['pid'],0)
    except ProcessLookupError: pass
    else: raise RuntimeError('old game is still alive')
d.append(dict(pid=os.getpid(),quickload=os.environ.get('BB_QUICKLOAD_PENDING'),
              supervised=os.environ.get('BB_RESTART_SUPERVISED'),online=os.environ.get('BB_ONLINE'),
              token=os.environ.get('BB_THD_TOKEN'),module=os.environ.get('BB_NET_MODULE'),
              overrides=os.environ.get('SHADPS4_HTTP_HOST_OVERRIDES_JSON'),
              choice=os.environ.get('BB_SKIP_NETWORK_CHOICE')))
p.write_text(json.dumps(d))
sys.exit(json.loads(os.environ['TEST_SEQUENCE'])[len(d)-1])
''')
        env = {k:v for k,v in os.environ.items() if not k.startswith('BB_')}
        env.update(TEST_PYTHON=sys.executable, TEST_SEQUENCE=json.dumps(sequence),
                   BB_QUICKLOAD_PENDING='stale')
        return supervisor, env

    def test_restart_waits_for_old_process_and_forwards_quickload_once(self):
        supervisor, env = self.fixture([76, 75, 76, 0])
        result = subprocess.run(['/bin/bash', str(supervisor)], env=env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        state = json.loads((self.root / 'state.json').read_text())
        self.assertEqual([x['quickload'] for x in state], [None, '1', None, '1'])
        self.assertEqual(len({x['pid'] for x in state}), 4)
        self.assertTrue(all(x['supervised']=='1' for x in state))

    def test_online_quickload_and_later_restarts_remain_offline_without_credentials(self):
        supervisor, env = self.fixture([76, 75, 76, 0])
        env.update(BB_ONLINE='1', BB_THD_TOKEN='fixture-token', BB_NET_MODULE='/fixture/module',
                   SHADPS4_HTTP_HOST_OVERRIDES_JSON='/fixture/routes', BB_SKIP_NETWORK_CHOICE='0')
        result = subprocess.run(['/bin/bash', str(supervisor)], env=env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        state = json.loads((self.root / 'state.json').read_text())
        self.assertEqual(state[0]['online'], '1')
        for row in state[1:]:
            self.assertEqual(row['online'], '0')
            self.assertEqual(row['choice'], '1')
            for key in ['token', 'module', 'overrides']: self.assertIsNone(row[key])
        self.assertEqual(result.stdout.count('Runtime: quickload switched to offline'), 2)

    def test_online_to_repeated_offline_restore_with_real_checkpoint_helper(self):
        supervisor, env = self.fixture([76, 76, 0])
        user = self.root / 'isolated-user'
        live = user / 'savedata/1/CUSA00207/SPRJ0005'
        live.mkdir(parents=True)
        (live / 'userdata0000').write_bytes(b'checkpoint')
        (live / 'userdata0010').write_bytes(b'profile')
        worker = self.root / 'worker.py'
        code = worker.read_text().replace("sys.exit(json.loads(os.environ['TEST_SEQUENCE'])[len(d)-1])", """
import importlib.util
spec=importlib.util.spec_from_file_location('checkpoint_helper',os.environ['TEST_HELPER'])
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
saves=q.Checkpoints(os.environ['TEST_USER'],'CUSA00207')
with saves.locked():
    if len(d)==1:
        saves.save()
        (saves.live/'SPRJ0005/userdata0000').write_bytes(b'online progress')
    else:
        saves.load()
        assert (saves.live/'SPRJ0005/userdata0000').read_bytes()==b'checkpoint'
sys.exit(json.loads(os.environ['TEST_SEQUENCE'])[len(d)-1])
""")
        worker.write_text(code)
        env.update(BB_ONLINE='1',BB_THD_TOKEN='fixture-token',BB_NET_MODULE='/fixture/module',
                   TEST_HELPER=str(ROOT/'tools/macos/quicksave.py'),TEST_USER=str(user))
        result=subprocess.run(['/bin/bash',str(supervisor)],env=env,capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        rows=json.loads((self.root/'state.json').read_text())
        self.assertEqual([row['online'] for row in rows],['1','0','0'])
        self.assertEqual((live/'userdata0000').read_bytes(),b'checkpoint')
        self.assertEqual(json.loads((user/'.quickload-offline.json').read_text()),{'version':1})
        backups=list((user/'quicksaves/CUSA00207').glob('before-load-*/data/SPRJ0005/userdata0000'))
        self.assertEqual(len(backups),2)
        self.assertIn(b'online progress',[path.read_bytes() for path in backups])

    def test_normal_failure_does_not_restart(self):
        supervisor, env = self.fixture([23])
        result = subprocess.run(['/bin/bash', str(supervisor)], env=env, timeout=15)
        self.assertEqual(result.returncode, 23)
        self.assertEqual(len(json.loads((self.root / 'state.json').read_text())), 1)

    def test_termination_stops_and_reaps_child_without_restarting(self):
        supervisor, env = self.fixture([0])
        (self.root / 'worker.py').write_text('''import os,time
from pathlib import Path
Path('pid').write_text(str(os.getpid()))
time.sleep(30)
''')
        p = subprocess.Popen(['/bin/bash', str(supervisor)], env=env)
        try:
            for _ in range(100):
                if (self.root / 'pid').exists(): break
                time.sleep(.02)
            else: self.fail('worker never started')
            child = int((self.root / 'pid').read_text())
            p.terminate()
            self.assertEqual(p.wait(timeout=5), 143)
            with self.assertRaises(ProcessLookupError): os.kill(child, 0)
        finally:
            if p.poll() is None: p.kill(); p.wait()

    @unittest.skipUnless(shutil.which('clang'), 'C compiler required')
    def test_runtime_handshake_reports_real_exit_status(self):
        source = self.root / 'probe.c'
        source.write_text('#include "runtime_restart_protocol.h"\n'
                          'int main(void) { return runtime_supervised_restart_status(); }\n')
        binary = self.root / 'probe'
        subprocess.run(['clang', '-I'+str(ROOT/'src'), str(source), '-o', str(binary)], check=True)
        for supervised, pending, expected in [('0','1',0), ('1','0',75), ('1','1',76)]:
            env = dict(os.environ, BB_RESTART_SUPERVISED=supervised, BB_QUICKLOAD_PENDING=pending)
            self.assertEqual(subprocess.run([str(binary)], env=env).returncode, expected)

    @unittest.skipUnless(sys.platform=='darwin', 'macOS bundle test')
    def test_game_bundle_identity_is_stable_and_code_is_relocatable(self):
        app = self.root / 'Launcher.app'
        binary = app / 'Contents/Resources/bbport/bin/bb-probe-metal'
        binary.parent.mkdir(parents=True)
        resources = app / 'Contents/Resources'
        (resources / 'AppIcon.icns').write_bytes((ROOT/'launcher-macos/Sources/BBLauncher/Resources/AppIcon.icns').read_bytes())
        (app/'Contents/Info.plist').write_bytes(plistlib.dumps(dict(CFBundleShortVersionString='0.2.32', CFBundleVersion='0232')))
        source = self.root / 'identity.m'
        source.write_text('#import <Foundation/Foundation.h>\n'
                          'int main(void) { @autoreleasepool { puts(NSBundle.mainBundle.bundleIdentifier.UTF8String); } }\n')
        subprocess.run(['clang', '-framework', 'Foundation', str(source), '-o', str(binary)], check=True)
        original = binary.read_bytes()
        for _ in range(2):
            helper = create_game_app(app)
            result = subprocess.check_output([str(helper)], text=True).strip()
            self.assertEqual(result, GAME_BUNDLE_ID)
            self.assertEqual(binary.read_bytes(), original)
            game_icon=helper.parents[1]/'Resources/AppIcon.icns'
            self.assertEqual(game_icon.read_bytes(),GAME_ICON.read_bytes())
            self.assertNotEqual(game_icon.read_bytes(),(resources/'AppIcon.icns').read_bytes())
            self.assertEqual((resources/'AppIcon.icns').read_bytes(),
                             (ROOT/'launcher-macos/Sources/BBLauncher/Resources/AppIcon.icns').read_bytes())
            subprocess.run(['codesign','--verify','--strict',str(helper.parents[2])],check=True)
