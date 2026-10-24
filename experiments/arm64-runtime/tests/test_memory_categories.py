#!/usr/bin/env python3
from pathlib import Path
import copy,json,subprocess,sys,time
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'));from memory_categories import parse
cat=dict(dirty=100,swapped=40,clean=20,reclaimable=0,wired=0,regions=2)
d=dict(unit='byte',processes=[dict(pid=123,name='authored',translated=False,**{'page size':16384},footprint=140,categories={'JIT':cat})],errors=[],warnings=[],shared=[],**{'bytes per unit':1,'total footprint':140})
s=parse(d,{123:'game'});assert s['processes'][0]['categories']['JIT']['swapped']==40 and s['dirty_includes_swapped'] and s['deduplicated_footprint_bytes']==140
for change in ['units','missing','duplicate','pid','negative','bool','error','architecture','page','category','double_swapped','boolean_unit']:
 b=copy.deepcopy(d)
 if change=='units':b['unit']='page'
 if change=='missing':b['processes']=[]
 if change=='duplicate':b['processes']*=2
 if change=='pid':b['processes'][0]['pid']=124
 if change=='negative':b['processes'][0]['categories']['JIT']['dirty']=-1
 if change=='bool':b['processes'][0]['footprint']=True
 if change=='error':b['errors']=['denied']
 if change=='architecture':del b['processes'][0]['translated']
 if change=='page':b['processes'][0]['page size']=123
 if change=='double_swapped':b['processes'][0]['categories']['JIT']['swapped']=101
 if change=='boolean_unit':b['bytes per unit']=True
 if change=='category':b['processes'][0]['categories']['JIT'].pop('dirty')
 try:parse(b,{123:'game'})
 except ValueError:pass
 else:raise AssertionError(change)
e=P/'evidence'/f'authored-memory-categories-{time.time_ns()}';e.mkdir();children=[subprocess.Popen(['/bin/sleep','20'])for _ in range(2)]
try:
 command=['/usr/bin/footprint','-j',str(e/'footprint.json'),'-f','bytes']
 for c in children:command+=['-p',str(c.pid)]
 r=subprocess.run(command,capture_output=True,text=True,timeout=10);(e/'console.log').write_text(r.stdout+r.stderr);assert r.returncode==0
 parsed=parse(json.loads((e/'footprint.json').read_text()),{c.pid:f'owned-sleep-{i}'for i,c in enumerate(children)});assert len(parsed['processes'])==2 and all(p['footprint_bytes']>0 for p in parsed['processes']);(e/'parsed.json').write_text(json.dumps(parsed,indent=2)+'\n')
finally:
 for c in children:c.terminate();c.wait()
result=dict(passed=True,scope='parser negative cases plus own two sleep children, not game memory evidence');(e/'test-result.json').write_text(json.dumps(result,indent=2)+'\n');print(dict(evidence=str(e),**result))
