# SPDX-License-Identifier: GPL-2.0-or-later
import json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_texture_bindings import analyze

def fixture(*,truncated=False,tag_state=1,known=True):
 return [{'op':'frame','truncated':truncated},{'op':'texture_capture','schema':1,'keep_set_memo':True},
 {'op':'texture_set','core':10,'shader':123,'stage':3,'slot_base':2,'count':1,'source':'set_memo'},
 {'op':'texture_binding','core':10,'shader':123,'stage':3,'slot':2,'source':'set_memo','image_known':known,'uid':9,'guest':100,'native_view':20,'tag_state':tag_state,'tag_generation':1,'current_owner':1},
 {'op':'texture_set','core':10,'shader':456,'stage':0,'slot_base':3,'count':0,'source':'resolve'},
 {'op':'texture_draw','core':10,'vs':123,'ps':456,'indices':6,'instances':1}]
def run(events):return analyze('\n'.join('NATIVE '+json.dumps(x)for x in events))
class CaptureAnalysis(unittest.TestCase):
 def test_memo_path_join(self):
  r=run(fixture());self.assertTrue(r['metadata_complete']);self.assertEqual(r['draws'][0]['bindings'][0]['uid'],9);self.assertFalse(r['render_correctness_proven'])
 def test_truncation_is_not_success(self):self.assertFalse(run(fixture(truncated=True))['metadata_complete'])
 def test_malformed_is_not_success(self):self.assertFalse(analyze('NATIVE nope')['metadata_complete'])
 def test_old_capture_does_not_claim_memo(self):self.assertFalse(run([x for x in fixture()if x['op']!='texture_capture'])['metadata_complete'])
 def test_missing_binding_detected(self):self.assertFalse(run([x for x in fixture()if x['op']!='texture_binding'])['metadata_complete'])
 def test_other_shader_cannot_fill_binding(self):
  f=fixture();f[3]['shader']=999;self.assertFalse(run(f)['metadata_complete'])
 def test_released_owner_is_review_not_pixel_fault(self):
  r=run(fixture(tag_state=2));self.assertTrue(r['metadata_complete']);self.assertEqual(len(r['review_observations']),1);self.assertFalse(r['render_correctness_proven'])
 def test_no_draw_is_not_acceptance(self):self.assertFalse(run(fixture()[:-1])['metadata_complete'])
 def test_binding_block_reference(self):
  f=fixture();f+=f[2:3]+[{'op':'texture_bindings_ref','start':2,'count':1}]+f[4:]
  r=run(f);self.assertTrue(r['metadata_complete']);self.assertEqual(r['memo_binding_observations'],2)
 def test_bad_binding_reference(self):
  for ref in ({'start':99,'count':1},{'start':0,'count':1},{'start':2,'count':65},{'start':-1,'count':1}):
   f=fixture()+[dict(op='texture_bindings_ref',**ref)];self.assertFalse(run(f)['metadata_complete'])
 def test_packed_binding_reference(self):
  f=fixture();binding=f[3];f[3]={'op':'texture_binding_batch','bindings':[binding]}
  f+=f[2:3]+[{'op':'texture_binding_batch','bindings':[{'record':2,'element':0}]}]+f[4:]
  r=run(f);self.assertTrue(r['metadata_complete']);self.assertEqual(r['memo_binding_observations'],2)
 def test_invalid_packed_reference(self):
  for ref in ({'record':99,'element':0},{'record':0,'element':0},{'record':2,'element':65},{'record':-1,'element':0},{'record':2,'element':0}):
   f=fixture()+[{'op':'texture_binding_batch','bindings':[ref]}];self.assertFalse(run(f)['metadata_complete'])
 def test_packed_reference_cannot_chain(self):
  f=fixture();f[3]={'op':'texture_binding_batch','bindings':[f[3]]}
  f+=[{'op':'texture_binding_batch','bindings':[{'record':2,'element':0}]},{'op':'texture_binding_batch','bindings':[{'record':5,'element':0}]}]
  self.assertFalse(run(f)['metadata_complete'])
 def test_compact_binding_identity_is_preserved(self):
  f=fixture();f[3]['nv']=f[3].pop('native_view');f[3]['tg']=f[3].pop('tag_generation');f[3]['co']=f[3].pop('current_owner');f[3]['ts']=2;f[3].pop('tag_state')
  r=run(f);self.assertTrue(r['metadata_complete']);self.assertEqual(len(r['review_observations']),1);self.assertEqual(r['draws'][0]['bindings'][0]['native_view'],20)
if __name__=='__main__':unittest.main()
