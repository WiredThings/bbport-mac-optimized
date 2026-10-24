from pathlib import Path
import subprocess,tempfile,json,sys,unittest
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from launcher_language_policy import load
from prepare_startup import prepare,policy
class Tests(unittest.TestCase):
 def test_original_swift_profile_decisions(self):
  for language,community,expected in [('11',True,'1'),('10',True,'1'),('11',False,'11'),('1',True,'1')]:
   with self.subTest(language=language,community=community),tempfile.TemporaryDirectory(dir=P/'fixtures')as d:
    f=Path(d)/'in.json';f.write_text(json.dumps(dict(language=language,communityChinese=community,chineseSimplifiedFolder=str(P.parent/'assets/chinese-retranslation-11'),chineseTraditionalFolder='')))
    env=json.loads(subprocess.check_output([str(P/'build/launcher_language_export'),str(f)]));self.assertEqual(env['BB_LANGUAGE'],expected)
    self.assertEqual(bool(policy.language_patches(env)),language=='11'and not community)
 def test_private_profile_patch_plan(self):
  env=load(P/'fixtures/current-launcher-language.json');self.assertEqual(env['BB_CHINESE_SOURCE'],'community');self.assertEqual(env['BB_LANGUAGE'],'1')
  with tempfile.TemporaryDirectory(dir=P/'evidence')as d:
   f=Path(d)/'startup.bin';prepare(P.parent/'assets/boot-linked.bin',f,online=True,language_environment=env)
   report=json.loads(f.with_suffix('.json').read_text());self.assertEqual(report['language'],1);self.assertEqual(report['language_source'],'community');self.assertNotIn(policy.UNLOCK_GAME_REGION,report['selected_names']);self.assertNotIn(policy.SKIP_NETWORK_CHOICE,report['selected_names'])
 def test_initial_and_F3_keep_same_language_plan(self):
  import ast
  calls=[n for n in ast.walk(ast.parse((P/'tools/run_game.py').read_text()))if isinstance(n,ast.Call)and isinstance(n.func,ast.Name)and n.func.id=='prepare']
  self.assertEqual(len(calls),2)
  for call in calls:self.assertTrue(any(k.arg=='language_environment'and isinstance(k.value,ast.Name)and k.value.id=='language_env'for k in call.keywords))
  env=load(P/'fixtures/current-launcher-language.json')
  with tempfile.TemporaryDirectory(dir=P/'evidence')as d:
   a=Path(d)/'online.bin';b=Path(d)/'F3.bin'
   for path,online in [(a,True),(b,False)]:prepare(P.parent/'assets/boot-linked.bin',path,online=online,language_environment=env)
   x=json.loads(a.with_suffix('.json').read_text());y=json.loads(b.with_suffix('.json').read_text())
   self.assertEqual(x['language'],y['language']);self.assertEqual(x['language_source'],y['language_source']);self.assertNotIn(policy.UNLOCK_GAME_REGION,y['selected_names']);self.assertEqual([p for p in y['patches']if p['name']!=policy.SKIP_NETWORK_CHOICE],x['patches'])
 def test_profile_rejections(self):
  with self.assertRaises(ValueError):load(P/'tools/qa_online_policy.py')
  with tempfile.NamedTemporaryFile(mode='w',suffix='.json',dir=P/'fixtures')as f:
   json.dump({'BB_LANGUAGE':'1','BB_CHINESE_SOURCE':'official','BB_THD_TOKEN':'not-a-token'},f);f.flush()
   with self.assertRaises(ValueError):load(f.name)
 def test_no_unverified_region_fix(self):
  import qa_online_policy
  self.assertNotIn('BB_ARM_NET_REGION_FIX',qa_online_policy.account_environment({'name':'qa-fixture','token':'not-a-real-token'}))
if __name__=='__main__':unittest.main()
