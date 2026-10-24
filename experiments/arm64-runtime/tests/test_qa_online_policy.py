"""Isolated account, offline restart and actual 1.09 patch boundaries."""
import importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
import qa_online_policy as q
from restart_policy import offline_environment
from prepare_startup import prepare,policy
class QATest(unittest.TestCase):
 def test_no_formal_or_arbitrary_account_selection(self):
  source=(P/'tools/qa_online_account.swift').read_text()
  self.assertEqual(source.count('io.github.bbport.optimized.options-qa.online.huntersdream'),1)
  self.assertIn('context.interactionNotAllowed = !authorize',source)
  self.assertIn('SecKeychainSetUserInteractionAllowed(authorize)',source)
 def test_loader_uses_noninteractive_pipe_only(self):
  fixture={'name':'qa-fixture','token':'fixture-not-a-real-token'}
  with patch.object(q.subprocess,'run',return_value=subprocess.CompletedProcess([],0,json.dumps(fixture).encode(),b''))as run:
   env=q.load_private_environment()
  self.assertEqual(run.call_args.args[0],[str(P/'build/qa_online_account'),'--export-to-parent-pipe'])
  self.assertEqual(env['BB_ARM_NET_DIAG'],'1');self.assertTrue(run.call_args.kwargs['capture_output']);self.assertEqual(env['BB_THD_TOKEN'],fixture['token'])
  clean=offline_environment(dict(env,BB_QUICKSAVE='1'),True)
  self.assertEqual(clean['BB_ONLINE'],'0');self.assertEqual(clean['BB_QUICKLOAD_PENDING'],'1')
  self.assertFalse(any(k in clean for k in ('BB_THD_TOKEN','BB_THD_ONLINE_ID','BB_NET_MODULE','BB_THD_SERVER','BB_ARM_NET_DIAG','BB_ARM_NET_REGION_FIX')))
 def test_fail_closed_without_logging_response(self):
  for data in [b'',b'{',b'{}',b'{"available":false,"status":-25293}',b'{"name":"x","token":""}',b'{"name":"x","token":"t","other":1}']:
   with self.subTest(data=data),patch.object(q.subprocess,'run',return_value=subprocess.CompletedProcess([],0,data,b'')):
    with self.assertRaises(RuntimeError)as e:q.load_private_environment()
    self.assertNotIn('25293',str(e.exception))
  with patch.object(q.subprocess,'run',return_value=subprocess.CompletedProcess([],1,b'not-a-credential',b'')):
   with self.assertRaises(RuntimeError):q.load_private_environment()
 def test_actual_startup_only_removes_offline_choice(self):
  case=P/'evidence/game-1792825519405477000';manifest=json.loads((case/'startup.json').read_text())
  with tempfile.TemporaryDirectory(prefix='qa-online-patch-',dir=P/'evidence')as d:
   old=Path(d)/'offline.bin';online=Path(d)/'online.bin'
   prepare(Path(manifest['boot']),old,Path(manifest['config']),P.parent/'assets/CUSA03173-1.09')
   prepare(Path(manifest['boot']),online,Path(manifest['config']),P.parent/'assets/CUSA03173-1.09',online=True)
   self.assertEqual(old.read_bytes(),(case/'startup.bin').read_bytes())
   a=json.loads(old.with_suffix('.json').read_text());b=json.loads(online.with_suffix('.json').read_text())
   self.assertFalse(a['online']);self.assertTrue(b['online'])
   self.assertEqual(set(a['selected_names'])-set(b['selected_names']),{policy.SKIP_NETWORK_CHOICE})
   self.assertFalse(set(b['selected_names'])-set(a['selected_names']))
   self.assertEqual([x for x in a['patches']if x['name']!=policy.SKIP_NETWORK_CHOICE],b['patches'])
   self.assertNotIn(policy.SKIP_NETWORK_CHOICE,b['selected_names'])
if __name__=='__main__':unittest.main()
