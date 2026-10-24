from pathlib import Path
import sys,tempfile,unittest
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from diagnostic_frame_tail import read_tail
class Tail(unittest.TestCase):
    def test_full_and_partial_last_line(self):
        with tempfile.TemporaryDirectory()as root:
            p=Path(root)/'frames.csv';p.write_text('t_s,frame_ms\n1,20\n2,130\n3,')
            self.assertEqual(read_tail(p),[{'t_s':'1','frame_ms':'20'},{'t_s':'2','frame_ms':'130'}])
    def test_fixed_tail_discards_first_partial_row(self):
        with tempfile.TemporaryDirectory()as root:
            p=Path(root)/'frames.csv';p.write_text('t_s,frame_ms\n'+''.join(f'{i},20.00\n'for i in range(10000)))
            rows=read_tail(p,256);self.assertLess(len(rows),30);self.assertEqual(rows[-1]['t_s'],'9999')
            self.assertTrue(all(float(r['frame_ms'])==20 for r in rows))
    def test_header_and_budget_rejected(self):
        with tempfile.TemporaryDirectory()as root:
            p=Path(root)/'frames.csv';p.write_bytes(b'not-a-complete-header')
            with self.assertRaises(ValueError):read_tail(p)
            with self.assertRaises(ValueError):read_tail(p,1000000)
if __name__=='__main__':unittest.main()
