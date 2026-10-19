"""Execute the real launch entry with a stub runtime, without starting a game/GPU."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).parents[1]
spec=importlib.util.spec_from_file_location('candidates',ROOT/'tools/macos/benchmark_candidates.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)

class MetalLaunchProfileTests(unittest.TestCase):
    def launch(self,benchmark=False,overrides=None):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'tools/macos').mkdir(parents=True)
            shutil.copy2(ROOT/'tools/macos/run_optimized.sh',root/'tools/macos/run_optimized.sh')
            shutil.copy2(ROOT/'tools/macos/supervise_restart.sh',root/'tools/macos/supervise_restart.sh')
            (root/'.packaged').touch();(root/'bin/gpu-metal').mkdir(parents=True)
            (root/'bin/bb-probe-metal').touch();(root/'bin/bb-probe-metal').chmod(0o755)
            (root/'bin/gpu-metal/libbbgpu.dylib').touch()
            (root/'run.sh').write_text('#!/bin/bash\n/usr/bin/env > "$BB_DATA_DIR/environment.txt"\n')
            pad=root/'pad';pad.touch()
            env={k:v for k,v in os.environ.items() if not k.startswith(('BB_','MTLCAPTURE_')) and k!='MTL_CAPTURE_ENABLED'}
            env.update(BB_GAME_DIR=str(root/'game'),BB_DATA_DIR=str(root/'data'),BB_USER_DIR=str(root/'user'),
                       BB_PAD_FILE=str(pad),BB_PAD_FILE_ONLY='1',BB_BENCHMARK_MUTE='1',BB_FRAME_LOG_ONLY='1',
                       BB_TEXTURE_AUDIT_DIR=str(root/'audit'),BB_DEV_WORLD_WARPER_DIR=str(root/'warper'),
                       BB_MTL_COPY_AUDIT='1',BB_MTL_COMPILE_AUDIT='1',BB_CAPTURE_NATIVE='1',MTL_CAPTURE_ENABLED='1')
            env.update(overrides or {})
            args=['bash',str(root/'tools/macos/run_optimized.sh')]+(['--benchmark'] if benchmark else [])
            result=subprocess.run(args,env=env,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            return dict(line.split('=',1) for line in (root/'data/environment.txt').read_text().splitlines() if '=' in line)
    def test_normal_play_enables_measured_profile_without_diagnostic_input(self):
        env=self.launch()
        for key in (*c.CANDIDATES,'BB_MTL_FOLD_CLEARS'):self.assertEqual(env[key],'1')
        self.assertEqual(env['BB_MTL_RELEASE_WARM_ARCHIVE'],'1')
        self.assertEqual(env['BB_MTL_PERSIST_WARM_ARCHIVE'],'0')
        self.assertEqual(env['BB_TEXTURE_INVALID_RECLAIM'],'1')
        for key in ('BB_TEXTURE_AUDIT_DIR','BB_DEV_WORLD_WARPER_DIR','BB_PAD_FILE','BB_PAD_FILE_ONLY','BB_BENCHMARK_MUTE','BB_FRAME_LOG_ONLY','BB_MTL_COPY_AUDIT','BB_MTL_COMPILE_AUDIT','BB_CAPTURE_NATIVE','MTL_CAPTURE_ENABLED'):
            self.assertNotIn(key,env)
    def test_benchmark_can_disable_all_defaults_and_keeps_isolated_controls(self):
        env=self.launch(True,{key:'0' for key in (*c.CANDIDATES,'BB_MTL_FOLD_CLEARS','BB_MTL_RELEASE_WARM_ARCHIVE','BB_TEXTURE_INVALID_RECLAIM')})
        for key in (*c.CANDIDATES,'BB_MTL_FOLD_CLEARS'):self.assertEqual(env[key],'0')
        self.assertEqual(env['BB_MTL_RELEASE_WARM_ARCHIVE'],'0')
        self.assertEqual(env['BB_TEXTURE_INVALID_RECLAIM'],'0')
        for key in ('BB_PAD_FILE_ONLY','BB_BENCHMARK_MUTE','BB_FRAME_LOG_ONLY','BB_MTL_COMPILE_AUDIT'):self.assertEqual(env[key],'1')

if __name__=='__main__':unittest.main()
