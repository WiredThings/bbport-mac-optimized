from pathlib import Path
import sys,unittest
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from host_cpu_audit import cpu_seconds,parse
class HostClock(unittest.TestCase):
    def test_formats(self):
        for value,expected in [('0:00.00',0),('1:02.03',62.03),('12:34:56.78',45296.78),('2-01:02:03.04',176523.04)]:self.assertAlmostEqual(cpu_seconds(value),expected)
    def test_reject(self):
        for value in ['1:99.1','-1','text','1:61:00']:
            with self.assertRaises(ValueError):cpu_seconds(value)
    def test_paths_and_non_process_rows(self):
        value=parse(' 123 0:01.50 /Applications/App With Spaces.app/MacOS/App With Spaces\n 0 0:00.00 kernel_task\n')
        self.assertEqual(value,{123:('App With Spaces',1.5)})
if __name__=='__main__':unittest.main()
