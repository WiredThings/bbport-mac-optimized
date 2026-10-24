import sys, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from qa_memory_policy import MemoryPolicy

def s(swap=6000, pressure=1, locked=False):
    return dict(swap_mib=swap, pressure=pressure, locked=locked)

class PolicyTests(unittest.TestCase):
    def test_warning_continues(self):
        p=MemoryPolicy(s())
        for t in range(601): self.assertIsNone(p.observe(t,s(pressure=2)))
    def test_severe_grace(self):
        p=MemoryPolicy(s())
        self.assertIsNone(p.observe(1,s(pressure=4)))
        self.assertIsNone(p.observe(5.999,s(pressure=4)))
        self.assertEqual(p.observe(6,s(pressure=4)), 'severe pressure sustained5s')
    def test_severe_reset(self):
        p=MemoryPolicy(s())
        for t, level in [(1,4),(5,2),(6,4),(10.999,4)]:
            self.assertIsNone(p.observe(t,s(pressure=level)))
        self.assertEqual(p.observe(11,s(pressure=4)), 'severe pressure sustained5s')
    def test_total_growth(self):
        p=MemoryPolicy(s())
        for t in range(1,9): self.assertIsNone(p.observe(t*6,s(6000+256*t)))
        self.assertEqual(p.observe(54,s(8048.01)), 'unexpected >2GiB swap growth')
    def test_burst_boundary(self):
        p=MemoryPolicy(s())
        self.assertIsNone(p.observe(4,s(7024)))
        self.assertEqual(p.observe(5,s(7024.01)), 'unexpected >1GiB swap growth within5s')
    def test_expired_history(self):
        p=MemoryPolicy(s())
        self.assertIsNone(p.observe(5.001,s(6600)))
    def test_growth_after_reclaim(self):
        p=MemoryPolicy(s())
        self.assertIsNone(p.observe(1,s(5000)))
        self.assertEqual(p.observe(2,s(6025)), 'unexpected >1GiB swap growth within5s')
    def test_lock_immediate(self):
        p=MemoryPolicy(s())
        self.assertEqual(p.observe(.01,s(locked=True)), 'screen locked')
    def test_invalid_samples_fail_closed(self):
        for bad in [s(float('nan')), s(-1), s(pressure=0), s(pressure=True),s(locked=1)]:
            with self.assertRaises(ValueError): MemoryPolicy(bad)
            with self.assertRaises(ValueError): MemoryPolicy(s()).observe(1,bad)
    def test_invalid_time(self):
        for t in [-1,float('nan'),float('inf'),True]:
            with self.assertRaises(ValueError): MemoryPolicy(s()).observe(t,s())
        p=MemoryPolicy(s());p.observe(2,s())
        with self.assertRaises(ValueError): p.observe(1,s())

if __name__=='__main__': unittest.main()
