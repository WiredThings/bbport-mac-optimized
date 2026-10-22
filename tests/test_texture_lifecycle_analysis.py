import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest
s=importlib.util.spec_from_file_location('lifecycle',Path(__file__).resolve().parents[1]/'tools/macos/analyze_texture_lifecycle.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

class LifecycleTests(unittest.TestCase):
    def write(self,root,name,rows,content=True):
        p=Path(root)/name;fields=list(m.REQUIRED)+(list(m.CONTENT) if content else [])
        with p.open('w',newline='') as f:
            w=csv.DictWriter(f,fields);w.writeheader();w.writerows(rows)
        return p
    def row(self,uid,content=True):
        r=dict(uid=uid,host_bytes=4096,flags=74,tag_state=3,lru_tick=1,gc_tick=900,gpu_complete=1)
        if content:r.update({k:0 for k in m.CONTENT});r['draws']=1
        return r
    def test_same_address_new_uid_is_not_resource_survival(self):
        with tempfile.TemporaryDirectory() as d:
            a=self.write(d,'1.csv',[self.row(1),self.row(2)])
            b=self.write(d,'2.csv',[self.row(2),self.row(3)])
            r=m.analyze([a,b]);self.assertEqual(r['initial_uids_still_present'],1)
            self.assertEqual(r['points'][1]['since_previous'],dict(added_uids=1,removed_uids=1,retained_uids=1,host_byte_delta=0))
            self.assertEqual(r['points'][1]['content']['encoded_nonupload_write_bytes'],8192)
            self.assertNotIn('safe_to_release',r)
    def test_zero_uid_descriptors_have_no_lifetime_identity(self):
        with tempfile.TemporaryDirectory() as d:
            placeholder=dict(self.row(0),host_bytes=0)
            a=self.write(d,'1.csv',[placeholder,placeholder,self.row(1)])
            b=self.write(d,'2.csv',[placeholder,self.row(1)])
            r=m.analyze([a,b]);self.assertEqual(r['initial_uids_still_present'],1)
            self.assertEqual(r['points'][0]['unidentified_zero_backing_objects'],2)
            self.assertEqual(r['points'][1]['since_previous']['removed_uids'],0)
    def test_legacy_snapshot_has_missing_provenance(self):
        with tempfile.TemporaryDirectory() as d:
            p=self.write(d,'1.csv',[self.row(1,False)],False)
            r=m.summarize(m.read_snapshot(p))
            self.assertEqual(r['content']['missing_objects'],1)
            self.assertEqual(r['content']['observed_objects'],0)
    def test_cross_process_uid_comparison_and_duplicate_rows_rejected(self):
        with tempfile.TemporaryDirectory() as a,tempfile.TemporaryDirectory() as b:
            p=self.write(a,'1.csv',[self.row(1)]);q=self.write(b,'2.csv',[self.row(1)])
            with self.assertRaises(ValueError):m.analyze([p,q])
            p=self.write(a,'duplicate.csv',[self.row(1),self.row(1)])
            with self.assertRaises(ValueError):m.read_snapshot(p)
