"""Invalid clock data cannot masquerade as an enormous real HLE wait."""
from pathlib import Path
import struct,sys,tempfile
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from analyze_stutter import read,publication_range
with tempfile.TemporaryDirectory(prefix='bb-trace-clock-')as root:
    path=Path(root)/'trace.bin';data=bytearray(64+16384*64)
    struct.pack_into('<8s7Q',data,0,b'BBAUDIT1',1,123,1000,2000,16384,3,0)
    for i,(start,wall)in enumerate([(1000000,50000),(1000000,(1<<64)-23000),(2000000,100000)],1):
        struct.pack_into('<QII6Q',data,64+(i-1)*64,i,0,30,7,start,wall,333,130,0)
    path.write_bytes(data);result=read(path)
    assert result['clock_rejected_rows']==1 and result['valid_rows']==2 and not result['all_published']
    assert all(r['wall']<=100000 for r in result['rows'])
    assert publication_range(result,1,1)['valid']and publication_range(result,3,3)['valid']
    assert not publication_range(result,1,3)['valid']
print('PASS unsigned underflow rejected and reported; valid neighboring publication ranges retained')
