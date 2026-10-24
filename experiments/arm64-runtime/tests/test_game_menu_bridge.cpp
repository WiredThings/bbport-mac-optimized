// Authored boundary simulator, includes the actual original menu implementation.
// This proves original menu flow and pointer lifetime, NOT actual FEX execution.
#include "bbport_game_menu.cpp"
#include "game_menu_detours.h"
#include <cassert>
#include <map>
#include <iostream>
#ifdef BB_MENU_REAL_FEX
#undef mmap
#include "core/guest_cpu/fex_guest_cpu.h"
#include "core/guest_cpu/fex_hle_bridge.h"
#include "core/fex/darwin_fault.h"
#include <signal.h>
#include <sys/ucontext.h>
#define mmap(address,size,prot,flags,fd,offset) bb_mmap(address,size,prot,flags,fd,offset)
#endif
namespace BbSettings { unsigned saves;Values& Get(){static Values v;return v;}void Save(){++saves;} }
using U=uint64_t;using B=unsigned char;
using namespace BbGameMenu;
static constexpr size_t ImageBytes=96u<<20,PoolBytes=1u<<20;
static B* image;static B* pool;static size_t used;
static std::map<U,size_t> live;
static std::vector<std::pair<U,size_t>> free_blocks;
static std::map<std::pair<void*,CallbackABI>,U> tokens;
static std::vector<GuestHook> installed;
static unsigned rows,adds,destructors,installs;
#ifdef BB_MENU_REAL_FEX
using namespace Core;using namespace Core::GuestCpu;
struct FexState{HleCallRegistry registry;HleVeneerAllocator veneers;std::unique_ptr<FexGuestCpuBackend> cpu;B*stack{};B*stubs{};unsigned depth{},calls{};}fs;
static U route_call(U,const U*,U);
static U fex_callback(void*,CallbackABI);
static void prepare_fex_code();
static void start_fex();
#endif

static void* allocate(U n){assert(n&&n<65536);const auto amount=(n+15)&~U(15);assert(amount<=PoolBytes-used);B*p=nullptr;for(auto it=free_blocks.begin();it!=free_blocks.end();++it)if(it->second==amount){p=(B*)it->first;free_blocks.erase(it);break;}if(!p){p=pool+used;used+=amount;}assert(live.emplace((U)p,n).second);memset(p,0,n);return p;}
static void release(void*p){auto it=live.find((U)p);assert(it!=live.end());free_blocks.emplace_back((U)p,(it->second+15)&~U(15));live.erase(it);}
static bool readable(const void*p,U n){auto a=(U)p;if(!a||!n||a>UINT64_MAX-n)return false;if(a>=(U)image&&a-(U)image<=ImageBytes&&n<=ImageBytes-(a-(U)image))return true;for(auto&[b,s]:live)if(a>=b&&a-b<=s&&n<=s-(a-b))return true;return false;}
static U callback(void*p,CallbackABI abi){auto key=std::pair{p,abi};auto it=tokens.find(key);if(it!=tokens.end())return it->second;U at=0;
#ifdef BB_MENU_REAL_FEX
 at=fex_callback(p,abi);
#else
 at=0x10000000+tokens.size()*16;
#endif
tokens.emplace(key,at);return at;}
static void* two(U token,void*a,void*b){
#ifdef BB_MENU_REAL_FEX
 U args[]={(U)a,(U)b};return (void*)route_call(token,args,2);
#else
for(auto&[key,val]:tokens)if(val==token){assert(key.second==CallbackABI::TwoPointers);return reinterpret_cast<void*(*)(void*,void*)>(key.first)(a,b);}assert(false);return nullptr;
#endif
}
static void call_rows(U token,void*p){
#ifdef BB_MENU_REAL_FEX
 U args[]={(U)p,0};route_call(token,args,2);
#else
for(auto&[key,val]:tokens)if(val==token){assert(key.second==CallbackABI::Rows);reinterpret_cast<void(*)(void*,void*)>(key.first)(p,nullptr);return;}assert(false);
#endif
}
static U fake_call(U target,const U*a,U count){
 auto ptr=[&](unsigned i,size_t bytes){assert(i<count&&readable((void*)a[i],bytes));return (B*)a[i];};
 if(target==0x20000000){assert(count==4);auto*p=ptr(0,16);(void)p;return (U)(image+0x9000);}
 if(target==0x20000010){assert(count==4);++adds;return a[3];}
 const U offset=target-(U)image;
 if(offset==MakeText){assert(count==3);auto*out=ptr(0,TextSize);memset(out,0,TextSize);*(U*)out=(U)TextOf(uint32_t(a[2]));*(U*)(out+0x28)=7;return a[0];}
 if(offset==FactoryToFunction){assert(count==2);auto*src=ptr(0,FunctionSize);auto*out=ptr(1,FunctionSize);memcpy(out,src,FunctionSize);return a[1];}
 if(offset==CreatePage){assert(count==3);auto*fn=ptr(2,FunctionSize);return (U)two(*(U*)(fn+8),(void*)a[0],(void*)a[1]);}
 if(offset==OpenPage){assert(count==6);assert(readable((void*)a[2],1));call_rows(a[3],(void*)a[0]);return a[0];}
 if(offset==ListPush||offset==PairPush){assert(count==2);const auto n=offset==ListPush?ListSize:PairListSize,at=offset==ListPush?ListCountOffset:PairCountOffset;auto*list=ptr(0,n);auto*value=ptr(1,ValueSize);auto&size=*(U*)(list+at);assert(size<(offset==ListPush?32:2));memcpy(list+size++*ValueSize,value,ValueSize);return 0;}
 if(offset==SliderRow||offset==PairRow||offset==ListRow){++rows;assert(count==(offset==SliderRow?4:5));ptr(0,1);ptr(1,TextSize*2);ptr(2,offset==ListRow?4:1);ptr(count-1,offset==ListRow?4:1);return 0;}
 if(offset==0x1100){assert(count==2);++destructors;return 0;}
 std::cerr<<"unexpected mock guest target "<<std::hex<<offset<<"\n";assert(false);return 0;
}
static bool install(B*base,U size,GuestHook*h,U n){
 assert(base==image&&size==ImageBytes&&n==2);++installs;
#ifdef BB_MENU_REAL_FEX
 for(U i=0;i<n;++i)assert(fs.veneers.QueryExecutableRange(h[i].target));
 auto plan=BbArmMenu::Prepare({base,size},(U)base,(U)fs.stubs,16384,{h,size_t(n)});
 memcpy(fs.stubs,plan.stubs.data(),plan.stubs.size());
 for(U i=0;i<n;++i){h[i].original=plan.originals[i];installed.push_back(h[i]);}
 for(auto&p:plan.patches)memcpy(image+p.offset,p.bytes.data(),p.bytes.size());
 assert(!mprotect(fs.stubs,16384,PROT_READ));
#else
 for(U i=0;i<n;++i){assert(h[i].target>=0x10000000&&h[i].target<0x10001000);h[i].original=0x20000000+i*16;installed.push_back(h[i]);}
#endif
 return true;
}
#ifdef BB_MENU_REAL_FEX
static bool validate(void*,uintptr_t a,size_t n,bool){return readable((void*)a,n)||(a>=(U)fs.stack&&a-(U)fs.stack<=65536&&n<=65536-(a-(U)fs.stack));}
static std::optional<GuestExecutionRange> executable(void*,uintptr_t a){
 if(auto r=fs.veneers.QueryExecutableRange(a))return r;
 for(auto [base,n]:{std::pair{image,ImageBytes},std::pair{fs.stubs,size_t(16384)}})if(a>=(U)base&&a-(U)base<n)return GuestExecutionRange{(U)base,n,true,false};
 return std::nullopt;
}
static U publish(std::shared_ptr<HleCallAdapter>a){auto value=fs.veneers.Allocate(*a);assert(std::holds_alternative<U>(value));return std::get<U>(value);}
static U fex_callback(void*host,CallbackABI abi){
 std::shared_ptr<HleCallAdapter>a;
 switch(abi){
 case CallbackABI::Rows:a=MakeHleCallAdapter(reinterpret_cast<void(*)(void*,void*)>(host));break;
 case CallbackABI::TwoPointers:a=MakeHleCallAdapter(reinterpret_cast<void*(*)(void*,void*)>(host));break;
 case CallbackABI::Lookup:a=MakeHleCallAdapter(reinterpret_cast<const char16_t*(*)(void*,uint32_t,uint32_t,uint32_t)>(host));break;
 case CallbackABI::Add:a=MakeHleCallAdapter(reinterpret_cast<void*(*)(void*,void*,void*,void*)>(host));break;
 }
 return publish(fs.registry.Register(a,"actual.menu.callback."+std::to_string(tokens.size())));
}
class MockGuest final:public HleCallAdapter {
 U target,count;
public:MockGuest(U t,U n):target(t),count(n){}
 HleCallResult Invoke(HleCallFrame&f)const override{
  const unsigned regs[]={7,6,2,1,8,9};std::array<U,6>a{};for(size_t i=0;i<count;++i)a[i]=f.gpr[regs[i]];
  f.gpr[0]=fake_call(target,a.data(),count);return true;
 }
};
static U route_call(U target,const U*args,U count){
 assert(count<=6&&fs.cpu);++fs.calls;GuestExecutionResult result;
 if(fs.depth){++fs.depth;result=fs.cpu->CallGuest(target,{args,size_t(count)});--fs.depth;}
 else{
  GuestExecutionRequest request;request.Rip=target;request.Rsp=(U)fs.stack+65536-8;request.Rflags=2;
  request.MappedRanges={{(U)image,ImageBytes,true,false},{(U)pool,PoolBytes,false,true},{(U)fs.stack,65536,false,true},{(U)fs.stubs,16384,true,false},fs.cpu->ReturnRange(),fs.cpu->CallbackReturnRange()};
  auto veneers=fs.veneers.GetExecutableRanges();request.MappedRanges.insert(request.MappedRanges.end(),veneers.begin(),veneers.end());
  auto halt=fs.cpu->ReturnAddress();memcpy((void*)request.Rsp,&halt,8);const unsigned regs[]={7,6,2,1,8,9};for(size_t i=0;i<count;++i)request.Gpr[regs[i]]=args[i];
  ++fs.depth;result=fs.cpu->Run(request);--fs.depth;
 }
 if(auto*failed=std::get_if<GuestExecutionFailure>(&result)){fprintf(stderr,"MENU FEX FAILED target=%#llx stage=%u error=%d\n",target,unsigned(failed->Stage),failed->Error);abort();}
 return std::get<GuestExecutionState>(result).Gpr[0];
}
static void prepare_fex_code(){
 fs.stack=(B*)mmap((void*)0xf700000000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED_NOREPLACE,-1,0);assert(fs.stack!=MAP_FAILED);
 fs.stubs=(B*)mmap((void*)0xf506100000,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED_NOREPLACE,-1,0);assert(fs.stubs!=MAP_FAILED);
 auto body=[](U offset,U logical,U count){auto a=std::make_shared<MockGuest>(logical,count);auto target=publish(fs.registry.Register(a,"authored.guest.body."+std::to_string(offset)));BbArmHooks::Emitter code;code.JumpAbs(target);memcpy(image+offset,code.code.data(),code.code.size());};
 body(MsgLookup+sizeof(MsgLookupPrologue),0x20000000,4);
 // Restore the two original saved registers before tailing to the authored body.
 image[AddPageItem+sizeof(AddPageItemPrologue)]=0x41;image[AddPageItem+sizeof(AddPageItemPrologue)+1]=0x5f;image[AddPageItem+sizeof(AddPageItemPrologue)+2]=0x5d;
 body(AddPageItem+sizeof(AddPageItemPrologue)+3,0x20000010,4);
 for(auto [offset,n]:std::initializer_list<std::pair<U,U>>{{MakeText,3},{FactoryToFunction,2},{CreatePage,3},{OpenPage,6},{ListPush,2},{PairPush,2},{SliderRow,4},{PairRow,5},{ListRow,5},{0x1100,2}})body(offset,(U)image+offset,n);
}
static void start_fex(){
 for(int sig:{SIGBUS,SIGSEGV}){struct sigaction action{};action.sa_flags=SA_SIGINFO;action.sa_sigaction=[](int s,siginfo_t*i,void*c){
  if(AetherPS4::Fex::HandleGuestSignal(s,i,c))return;
  auto*uc=static_cast<ucontext_t*>(c);auto pc=arm_thread_state64_get_pc(uc->uc_mcontext->__ss);uint64_t rip{};AetherPS4::Fex::PrototypeQueryLiveGuestRip(pc,&rip);
  char line[256];int n=snprintf(line,sizeof(line),"MENU TEST FAULT signal=%d address=%p pc=%#llx guest=%#llx offset=%#llx\n",s,i->si_addr,pc,rip,rip-(U)image);write(2,line,n);_Exit(74);
 };sigemptyset(&action.sa_mask);assert(!sigaction(sig,&action,nullptr));}

 assert(!mprotect(image,ImageBytes,PROT_READ));
 auto*bridge=new HleGuestBridge(fs.registry,validate,nullptr,nullptr,nullptr,executable,nullptr);
 auto made=FexGuestCpuBackend::Create(*bridge);assert(std::holds_alternative<std::unique_ptr<FexGuestCpuBackend>>(made));fs.cpu=std::move(std::get<std::unique_ptr<FexGuestCpuBackend>>(made));
}
#endif

static void plan_tests(){
 std::array<B,256> bytes{};const B a[]={0x55,0x48,0x89,0xe5,0x41,0x57},b[]={0x89,0xf0,0x48,0x8b,0x77,8,0x48,0x8b,0x34,0xc6};memcpy(bytes.data()+16,a,sizeof(a));memcpy(bytes.data()+80,b,sizeof(b));
 std::array<GuestHook,2> h{{{16,a,sizeof(a),0x400000,0},{80,b,sizeof(b),0x500000,0}}};
 const auto before=bytes;auto p=BbArmMenu::Prepare(bytes,0x100000,0x110000,16384,h);assert(p.patches.size()==2&&p.originals.size()==2&&bytes==before);
 assert(p.stubs[6]==0xff&&p.stubs[7]==0x25); // original prologue tails to guest, never host ARM.
 unsigned rejected=0;
 auto bad=[&](auto input,U base,U at,size_t cap,auto hooks){bool refused=false;try{(void)BbArmMenu::Prepare(input,base,at,cap,hooks);}catch(const std::runtime_error&){refused=true;}assert(refused&&bytes==before);++rejected;};
 auto changed=bytes;changed[80]^=1;bad(changed,0x100000,0x110000,16384,h);
 bad(bytes,0x100000,0x100010,16384,h);bad(bytes,0x100000,0x110000,16,h);bad(bytes,UINT64_MAX-16,0x110000,16384,h);bad(bytes,0x100000,0xf000000000,16384,h);
 auto invalid=h;invalid[1].target=0;bad(bytes,0x100000,0x110000,16384,invalid);invalid=h;invalid[1].offset=255;bad(bytes,0x100000,0x110000,16384,invalid);invalid=h;invalid[1]=h[0];bad(bytes,0x100000,0x110000,16384,invalid);
 printf("PASS atomic two-hook plans and %u negative cases; original image unchanged\n",rejected);
}
int main(int argc,char**argv){
 plan_tests();const std::string mode=argc>1?argv[1]:"valid";
 image=(B*)mmap((void*)0xf500000000,ImageBytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED_NOREPLACE,-1,0);assert(image!=MAP_FAILED);
 pool=(B*)mmap((void*)0xf600000000,PoolBytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED_NOREPLACE,-1,0);assert(pool!=MAP_FAILED);
 memcpy(image+MsgLookup,MsgLookupPrologue,sizeof(MsgLookupPrologue));memcpy(image+AddPageItem,AddPageItemPrologue,sizeof(AddPageItemPrologue));
 
#ifdef BB_MENU_REAL_FEX
 prepare_fex_code();GuestBridge ops{route_call,callback,allocate,release,readable,install};
#else
 GuestBridge ops{fake_call,callback,allocate,release,readable,install};
#endif
auto broken=ops;broken.call=nullptr;assert(!SetGuestBridge(broken));assert(SetGuestBridge(ops));assert(!SetGuestBridge(ops));
 if(mode=="disabled")setenv("BB_GAME_MENU","0",1);else setenv("BB_GAME_MENU","1",1);
 if(mode=="mismatch")image[AddPageItem]^=1;
 if(mode=="short"){PatchImage(image,32);assert(!installs&&live.empty()&&tokens.empty());puts("PASS short image refused before reads or allocation");return 0;}
 PatchImage(image,ImageBytes);
 if(mode=="disabled"||mode=="mismatch"){assert(!installs&&live.empty()&&tokens.empty());puts("PASS disabled/mismatched menu: no callbacks, allocation or patch");return 0;}
 assert(installs==1&&installed.size()==2&&tokens.size()==20);assert(pages.size()<=6&&!pages.empty());
 // Complete owned virtual tables for scratch std::function destruction.
 *(U*)(image+FactoryVtable+4*8)=(U)image+0x1100;*(U*)(image+ContentVtable+4*8)=(U)image+0x1100;
 memcpy(image+0x9000,u"Screen/Sound",26);auto*repository=(B*)allocate(16);auto*page=(B*)allocate(128);auto*out=(B*)allocate(64);auto*pair=(B*)allocate(128);
 const auto persistent=live.size()
#ifdef BB_MENU_REAL_FEX
+1
#endif
;const auto live_bytes=[](){size_t n=0;for(auto&[p,s]:live)n+=s;return n;};const auto persistent_bytes=live_bytes()
#ifdef BB_MENU_REAL_FEX
+16
#endif
;
 #ifdef BB_MENU_REAL_FEX
 auto*lookup_table=(B*)allocate(16);*(U*)(repository+8)=(U)lookup_table;*(U*)lookup_table=(U)image+0x9000;
 start_fex();U lookup_args[]={(U)repository,0,TextName,ScreenSoundItem};
 assert(route_call(installed[0].target,lookup_args,4)==(U)image+0x9000);
#else
 assert((U)LookupHook(repository,0,TextName,ScreenSoundItem)==(U)image+0x9000);
#endif

 *(U*)pair=(U)image+0x9000;
#ifdef BB_MENU_REAL_FEX
 U add_args[]={(U)page,(U)pair,(U)out,(U)out};route_call(installed[1].target,add_args,4);
#else
 AddHook(page,pair,out,out);
#endif
assert(adds==1+pages.size());assert(live.size()==persistent);
 for(unsigned iteration=0;iteration<20;++iteration){
  for(size_t n=0;n<pages.size();++n)two(callback(factories[n],CallbackABI::TwoPointers),page,out);
  assert(live.size()==persistent&&live_bytes()==persistent_bytes&&tokens.size()==20);
 }
 assert(rows>100&&destructors==pages.size()*21);assert(!Poll());bytes[ShowFps]=!bytes[ShowFps];assert(Poll()&&BbSettings::saves==1);assert(!Poll());
 auto invalid=[&](auto op){bool refused=false;try{op();}catch(const std::runtime_error&){refused=true;}assert(refused);};
 invalid([&]{DestroyFunction((B*)0xdead0000);});auto*f=(B*)allocate(FunctionSize);*(U*)(f+0x20)=0xdead0000;invalid([&]{DestroyFunction(f);});release(f);
 auto*list=(B*)allocate(ListSize);*(U*)(list+ListCountOffset)=33;invalid([&]{DestroyValues(list,ListCountOffset);});release(list);
 #ifdef BB_MENU_REAL_FEX
 printf("PASS actual FEX menu callbacks and nested guest calls: total=%u; no real game loaded\n",fs.calls);fs.cpu.reset();
#endif
 printf("PASS original System menu: pages=%zu callbacks=%zu rows=%u iterations=20 persistent_bytes=%zu transient_leaks=0; authored guest bodies; real game UI still unverified\n",pages.size(),tokens.size(),rows,persistent_bytes);
}
