#!/usr/bin/env python3
"""Real ARM64 bbnet/FEX HTTP, no account or gameplay. Loopback default; public GET opt-in."""
from pathlib import Path
import argparse,json,subprocess,time,sys,threading,http.server,signal
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from run_game import system,usage
from game_fixture import make
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--public',action='store_true');ap.add_argument('--fast-hle-lookup',action='store_true');ap.add_argument('--expect',type=int,default=0);a=ap.parse_args()
 before=system();assert not before['locked'] and before['pressure']!=4
 assert not any(l.rstrip().endswith(('/bb-probe-metal','/bb-probe','/bb-gpu','/bb-game-arm64')) for l in subprocess.check_output(['ps','-axo','pid,comm'],text=True).splitlines())
 e=P/'evidence'/f'http-roundtrip-{time.time_ns()}';e.mkdir();state=P/'state'/e.name;gpu=P/'state'/(e.name+'-gpu');gpu.mkdir()
 server=None;hits=[]
 if not a.public:
  class Handler(http.server.BaseHTTPRequestHandler):
   def do_GET(self):
    assert self.path=='/about';hits.append(self.path);body=b'{"isolated_qa":true,"payload":"'+b'a'*8192+b'"}'
    self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
   def log_message(self,*args):pass
  server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();url=f'http://127.0.0.1:{server.server_port}/about'
 else:url='https://thehuntersdream.com/about'
 data=make('net-http-roundtrip');old=b'https://thehuntersdream.com/about\0';assert data.count(old)==1;at=data.index(old);data=data[:at]+url.encode().ljust(96,b'\0')+data[at+96:]
 fixture=P/'fixtures'/(e.name+'.bbprobe');fixture.write_bytes(data)
 env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(P/'state')+'/',BB_GPU_USER_DIR=str(gpu),BB_USER_DIR=str(state),BB_ONLINE='1',BB_ARM_ALLOW_NETWORK='1',BB_NET_BACKEND='thd',BB_NET_MODULE=str(P/'build/bbnet-arm64/libbbnet.dylib'),BB_AUDIO='none',BB_BENCHMARK_MUTE='1')
 if __import__('os').environ.get('BB_ARM_PACKED_VENEERS')=='1':env['BB_ARM_PACKED_VENEERS']='1'
 if a.fast_hle_lookup:env['BB_ARM_HLE_FAST']='1'
 samples=[];stop=None;start=time.monotonic()
 with (e/'runtime.log').open('w')as log:
  proc=subprocess.Popen([str(P/'build/fex/Bin/bb-game-arm64'),'--authored-fixture','--boot',str(fixture),'--state',str(state)],env=env,stdout=log,stderr=log)
  try:
   while proc.poll() is None:
    sample=system();sample['seconds']=time.monotonic()-start
    ps=subprocess.check_output(['ps','-axo','pid,ppid,rss,comm'],text=True);sample['usage']=usage(l for l in ps.splitlines()[1:] if l.strip().split(None,1)[0]==str(proc.pid));samples.append(sample)
    if sample['locked']:stop='screen locked'
    elif sample['pressure']==4:stop='severe pressure'
    elif sample['swap_mib']-before['swap_mib']>256:stop='unexpected >256MiB swap growth'
    elif sample['seconds']>25:stop='timeout'
    if stop:proc.terminate();break
    time.sleep(.1)
   proc.wait(timeout=3)
  finally:
   if proc.poll() is None:proc.kill();proc.wait()
   if server:server.shutdown();server.server_close()
 result=dict(exit=proc.returncode,expected=a.expect,stopped=stop,seconds=time.monotonic()-start,endpoint=url,loopback_requests=len(hits),before=before,after=system(),samples=samples,credentials_used=False,fast_hle_lookup=a.fast_hle_lookup,scope='authored x86 FEX fixture -> ARM64 bbnet HTTP -> complete response headers and body, no game/save/server-login')
 (e/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2));print('Evidence:',e)
 assert proc.returncode==a.expect and stop is None,(e,result['exit'],stop)
if __name__=='__main__':main()
