import importlib.util,json
from pathlib import Path
import unittest
s=importlib.util.spec_from_file_location('a',Path(__file__).parents[1]/'tools/macos/analyze_metal_attachments.py');a=importlib.util.module_from_spec(s);s.loader.exec_module(a)
class AttachmentReportTests(unittest.TestCase):
 def text(self,rows):return '\n'.join('NATIVE '+json.dumps(r) for r in rows)
 def rows(self):
  r=[dict(op='frame',frame=7,truncated=False,bytes=100)]
  for i in range(2):r.extend([dict(op='open',queue=1,kind=1,guest=False),dict(op='attachment',queue=1,slot=0,id=i,w=4,h=4,load=2,store=1),dict(op='close',queue=1,kind=1)])
  return r
 def test_counts_and_adjacent_clear_candidates(self):
  r=a.analyze(self.text(self.rows()));self.assertEqual(r['encoders'],2);self.assertEqual(len(r['consecutive_clear_runs']),1)
 def test_intervening_compute_prevents_clear_run(self):
  r=self.rows();r[4:4]=[dict(op='open',queue=1,kind=2,guest=False),dict(op='close',queue=1,kind=2)];self.assertFalse(a.analyze(self.text(r))['consecutive_clear_runs'])
 def test_different_sizes_or_loading_are_not_candidates(self):
  for update in [dict(w=8),dict(load=1)]:
   r=self.rows();r[5].update(update);self.assertFalse(a.analyze(self.text(r))['consecutive_clear_runs'])
 def test_truncated_or_unbalanced_capture_rejected(self):
  for change in ['truncated','unclosed','overlap']:
   r=self.rows()
   if change=='truncated':r[0]['truncated']=True
   elif change=='unclosed':r.pop()
   else:r[2]=dict(op='open',queue=1,kind=1,guest=False)
   with self.assertRaises(ValueError):a.analyze(self.text(r))
