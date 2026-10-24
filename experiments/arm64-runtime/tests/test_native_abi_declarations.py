"""Real Clang macro expansion coverage; unrelated annotations/host helpers excluded."""
from pathlib import Path
import json,subprocess,sys
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from native_abi_declarations import is_guest_definition
source=r'''
#define ABI __attribute__((annotate("bb_guest_abi",0x42424755455354ULL)))
#define UNSEEN_FACTORY(name) static ABI int name(int x){return x;}
#define OUTER(name) UNSEEN_FACTORY(name)
static ABI int ordinary(int x){return x;}
UNSEEN_FACTORY(factory)
OUTER(nested)
static int host_helper(int x){return x;}
static __attribute__((annotate("other",7))) int unrelated(int x){return x;}
static ABI int prototype_only(int);
'''
r=subprocess.run(['clang','-x','c','-Xclang','-ast-dump=json','-fsyntax-only','-'],input=source,capture_output=True,text=True,check=True)
nodes=json.loads(r.stdout)['inner'];selected=[x['name']for x in nodes if is_guest_definition(x)]
assert selected==['ordinary','factory','nested'],selected
print('PASS Clang ordinary/unknown macro/nested macro ABI; host/unrelated/prototype rejected')
