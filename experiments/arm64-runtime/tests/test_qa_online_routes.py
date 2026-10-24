from pathlib import Path
import json,sys,tempfile,unittest
from unittest.mock import patch
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
import qa_online_policy as q
class RouteTest(unittest.TestCase):
 def test_exact_existing_launcher_routes_no_credentials(self):
  with tempfile.TemporaryDirectory(prefix='routes-unit-',dir=P/'state')as d:
   state=Path(d);env=q.account_environment({'name':'qa-fixture','token':'not-a-real-token'})
   result=q.prepare_private_routes(state,env);f=Path(result['SHADPS4_HTTP_HOST_OVERRIDES_JSON']);self.assertTrue(f.is_relative_to(state));self.assertFalse(f.is_symlink())
   self.assertEqual(json.loads(f.read_text()),{'*.scej-network.jp':'https://thehuntersdream.com','bb-playlog-prod.s3.amazonaws.com':'https://thehuntersdream.com:18671','bb-playlog-test.s3.amazonaws.com':'https://thehuntersdream.com:18671'})
   self.assertNotIn('not-a-real-token',f.read_text());self.assertNotIn('qa-fixture',f.read_text())
   again=q.prepare_private_routes(state,env);self.assertNotEqual(result,again);self.assertTrue(f.is_file())
 def test_refuse_nonprivate_and_link(self):
  env=q.account_environment({'name':'qa-fixture','token':'not-a-real-token'})
  for value in (P,P/'evidence',P/'state/does-not-exist'):
   with self.assertRaises(ValueError):q.prepare_private_routes(value,env)
  with tempfile.TemporaryDirectory(prefix='routes-unit-',dir=P/'state')as d:
   state=Path(d);link=state/'link';link.symlink_to(state)
   with self.assertRaises(ValueError):q.prepare_private_routes(link,env)
 def test_manual_online_has_no_automatic_quickload(self):
  source=(P/'tools/run_game.py').read_text();self.assertIn("elif not a.qa_online:env['BB_QUICKLOAD_PENDING']='1'",source);self.assertIn('a.qa_online and not a.qa_online_manual',source)
if __name__=='__main__':unittest.main()
