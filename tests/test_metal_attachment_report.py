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

 def test_barrier_requests_are_not_inferred_as_redundant(self):
  rows=self.rows();rows[3:3]=[dict(op='barrier'),dict(op='barrier',kind=2,holding=True,hoisting=True),dict(op='render_barriers',queue=1,requested=7,dropped=2)]
  r=a.analyze(self.text(rows));self.assertEqual(r['render_barriers'],dict(lists=1,requested=7,dropped=2));self.assertEqual(len(r['scheduler_barrier_requests_by_path']),2)
  self.assertIsNone(a.analyze(self.text(self.rows()))['render_barriers']['dropped'])
 def test_invalid_barrier_summary_rejected(self):
  for summary in [dict(queue=7,requested=1,dropped=0),dict(queue=1,requested=1,dropped=2),dict(queue=1,requested=-1,dropped=0)]:
   rows=self.rows();rows[3:3]=[dict(op='render_barriers',**summary)]
   with self.assertRaises(ValueError):a.analyze(self.text(rows))

 def test_occlusion_events_are_separate_from_physical_encoders(self):
  rows=self.rows();rows[4:4]=[dict(op='open',queue=1,kind=2,guest=False),dict(op='occlusion_snapshot',queue=1,count=3,pairs=1,end=0,poison=0,chained=True),dict(op='occlusion_snapshot',queue=1,count=0,pairs=1,end=1,poison=0,chained=True),dict(op='close',queue=1,kind=2)]
  r=a.analyze(self.text(rows));self.assertEqual(r['compute'],1);self.assertEqual(r['occlusion_snapshots'],dict(events=2,chained_events=2,counter_slots=3))
  self.assertIsNone(a.analyze(self.text(self.rows()))['occlusion_snapshots'])
  rows[5]['count']=4097
  with self.assertRaises(ValueError):a.analyze(self.text(rows))
