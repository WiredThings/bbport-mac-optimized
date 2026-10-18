import importlib.util,json,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('transfers',Path(__file__).parents[1]/'tools/macos/analyze_metal_transfers.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

class TransferReportTests(unittest.TestCase):
 def test_repeats_are_not_called_redundant(self):
  copy=dict(op='buffer_copy',queue=1,src=10,dst=20,src_offset=0,dst_offset=4,bytes=8,src_length=8,dst_length=12,flush=False)
  rows=[dict(op='frame',frame=2,truncated=False),dict(op='open',queue=1,kind=3,guest=False),dict(op='label',queue=1,text='Buffer copy source: BDA leaf'),copy,copy,dict(op='close',queue=1,kind=3)]
  r=m.analyze('\n'.join('NATIVE '+json.dumps(x) for x in rows))
  self.assertEqual(r['copy_totals']['bytes'],16);self.assertEqual(r['repeated_exact_range_occurrences'],1)
  self.assertNotIn('redundant_bytes',r)
 def test_compile_cache_layers_stay_separate(self):
  text='Metal compile audit: kind=shader id=0001 start-ns=10 elapsed-us=90 ok=1 cached=1\nMetal compile audit: kind=library id=0000 start-ns=20 elapsed-us=70 ok=1 cached=-1'
  r=m.compiles(text);self.assertEqual(len(r['groups']),2)
  self.assertIsNone(r['groups'][0]['cache_hits'])
  with self.assertRaises(ValueError):m.compiles(text+'\nMetal compile audit: truncated=1')
