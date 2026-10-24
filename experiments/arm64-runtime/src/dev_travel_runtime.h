// SPDX-License-Identifier: GPL-3.0-or-later
// FEX runtime adaptation of the development-only World Warper. The state machine
// and 1.09 offsets originate in vendor/dev_world_warper/WORLD-WARPER.md.
namespace ArmTravel {
using U=uint64_t;
const char marker[]="BB_DEVELOPMENT_WORLD_WARPER_FEX_V1";
WWState state{};
fs::path directory;
std::string nonce;
bool armed{};
double began{},last_tick{},last_report{};
U game{},warp_entry{},fixture_ticks{};
std::atomic_flag busy=ATOMIC_FLAG_INIT;
uint64_t owner_thread{};
unsigned read_failures{};
U NowNs(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return U(t.tv_sec)*1000000000+U(t.tv_nsec);}
// Copy transient object fields through the kernel: a concurrent unmap returns
// failure, never longjmp across C++/FEX frames. No game pointer survives a tick.
bool Read(U at,void* out,size_t n){
 if(!active||!n||!active->valid(at,n,false)){++read_failures;return false;}
 mach_vm_size_t copied{};bool ok=mach_vm_read_overwrite(mach_task_self(),at,n,reinterpret_cast<mach_vm_address_t>(out),&copied)==KERN_SUCCESS&&copied==n;if(!ok)++read_failures;return ok;
}
U Ptr(U at){U v{};return Read(at,&v,8)?v:0;}
unsigned Byte(U at){unsigned char v{};return Read(at,&v,1)?v:0;}
const char* Stage(){switch(state.stage){case WW_IDLE:return "idle";case WW_REQUESTED:return "requested";case WW_LOADING:return "loading";case WW_ARRIVED:return "arrived";default:return "failed";}}
bool File(const fs::path& path,char* data,size_t capacity,ssize_t& n){
 int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return false;
 struct stat st{};bool ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==getuid()&&st.st_size>0&&size_t(st.st_size)<capacity;
 n=ok?read(fd,data,capacity-1):-1;close(fd);if(n<=0)return false;data[n]=0;return true;
}
#include "resource_query_runtime.h"
void Report(double now,int world,int loading,uint32_t map){
 U alloc=Ptr(game+0x54b9f38),free=Ptr(game+0x54b9f40);
 const bool callbacks=alloc&&free&&executable(active,alloc)&&executable(active,free)&&alloc>=game&&free>=game&&alloc-game<active->image_size&&free-game<active->image_size;
 const U pool=Ptr(game+0x56a6d18),fallback=Ptr(game+0x5540698);
 int mode=-1;if(pool)Read(pool+0x2714,&mode,sizeof(mode));
 const U table=fallback?Ptr(fallback):0,allocate=table?Ptr(table+0x58):0,release=table?Ptr(table+0x70):0;
 const auto code_offset=[&](U p){return p>=game&&p-game<active->image_size&&executable(active,p)?p-game:0;};


 const auto pending=directory/"status.pending",final=directory/"status.json";
 int fd=open(pending.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0){state.stage=WW_FAILED;state.error="status-open";return;}
 FILE* f=fdopen(fd,"w");if(!f){close(fd);state.stage=WW_FAILED;state.error="status-stream";return;}
 fprintf(f,"{\"schema\":1,\"tool\":\"%s\",\"sequence\":%u,\"destination\":%u,\"stage\":\"%s\",\"map\":%u,\"world\":%s,\"loading\":%s,\"saw_loading\":%s,\"saw_world_down\":%s,\"stable_ticks\":%u,\"elapsed\":%.3f,\"error\":\"%s\"}\n",marker,state.sequence,state.destination,Stage(),map,world?"true":"false",loading?"true":"false",state.saw_loading?"true":"false",state.saw_world_down?"true":"false",state.stable_ticks,now-began,state.error?state.error:"");
 // Keep this development-only callback probe separate from the travel state.
 // Pointer values must be executable guest addresses; never follow or call them.
 bool ok=!ferror(f);if(fclose(f))ok=false;if(!ok||rename(pending.c_str(),final.c_str())){state.stage=WW_FAILED;state.error="status-write";}
 const auto probe=directory/"allocator-callbacks.json";
 int pfd=open(probe.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
 if(pfd>=0){char output[512];int count=snprintf(output,sizeof(output),"{\"guest_executable\":%s,\"allocate_offset\":%llu,\"free_offset\":%llu,\"pool_mode\":%d,\"fallback_allocate_offset\":%llu,\"fallback_free_offset\":%llu}\n",callbacks?"true":"false",callbacks?alloc-game:0,callbacks?free-game:0,mode,code_offset(allocate),code_offset(release));if(count>0&&count<int(sizeof(output)))write(pfd,output,count);close(pfd);}
 last_report=now;
}
void Tick(){
 if(busy.test_and_set(std::memory_order_acquire))return;
 struct Release{~Release(){busy.clear(std::memory_order_release);}}release;
 const double now=NowNs()/1e9;if(now-last_tick<.1)return;last_tick=now;
 if(!armed){char data[64]{};ssize_t n{};if(!File(directory/"armed",data,sizeof(data),n)||std::string_view(data,n)!=nonce)return;
  armed=true;pthread_threadid_np(nullptr,&owner_thread);printf("ARM TRAVEL armed after host character/frame validation\n");}
 uint64_t thread{};pthread_threadid_np(nullptr,&thread);if(thread!=owner_thread){state.stage=WW_FAILED;state.error="flipper-thread-changed";}
 read_failures=0;
 const U manager=Ptr(game+0x553e878),player=manager?Ptr(manager+0x60):0,session=Ptr(game+0x5540290);
 const int world=manager&&player&&session;
 const U helper=Ptr(game+0x553e8b8);const bool in_frame=helper&&(Byte(helper+0x50)||Byte(helper+0x51));
 const int loading=Byte(game+0x556286b)&&!in_frame;
 const U history=player?Ptr(player+0x400):0;uint32_t map=0xffffffff;if(history)Read(history+0x48,&map,4);
 // An inaccessible transient object is not evidence of world unload.
 if(read_failures)return;
 const WWStage previous=state.stage;if(now-began>600){state.stage=WW_FAILED;state.error="session-time-bound";}
 ww_observe(&state,now,world,loading,map);
 if((state.stage==WW_IDLE||state.stage==WW_ARRIVED)&&world&&!loading)InspectResources();
 if(state.stage==WW_IDLE||state.stage==WW_ARRIVED){
  char data[96]{};ssize_t n{};unsigned sequence{},row{};char extra{};
  if(File(directory/"command.txt",data,sizeof(data),n)){
   if(sscanf(data,"%u %u %c",&sequence,&row,&extra)!=2){state.stage=WW_FAILED;state.error="invalid-command";}
   else if(sequence>state.sequence){
    if(!ww_request(&state,sequence,row,now,world,loading,map)){state.stage=WW_FAILED;state.error="rejected-request";}
    else {printf("ARM TRAVEL request sequence=%u row=%u map=%#x -> %#x\n",sequence,row,map,ww_map(row));Report(now,world,loading,map);const U args[]={row};bb_arm_call(warp_entry,args,1);}
   }
  }
 }
 if(previous!=state.stage)printf("ARM TRAVEL sequence=%u stage=%s map=%#x loading=%d world=%d\n",state.sequence,Stage(),map,loading,world);
 if(previous!=state.stage||now-last_report>=.5)Report(now,world,loading,map);
}
class FixtureTick final:public HleCallAdapter {
public:HleCallResult Invoke(HleCallFrame& f)const override{
 ++fixture_ticks;const U args[]={2102950};bb_arm_call(warp_entry,args,1);
 for(unsigned i:{0,1,2,6,7,8,9,10,11})f.gpr[i]=0xfeedface;
 for(auto& x:f.xmm)x={0,0};return true;
 }
};
U FixtureCheck(){U row{};if(getenv("BB_ARM_ALLOCATION_AUDIT_FIXTURE")&&ArmAllocation::fixture_calls!=2)return 1;return fixture_ticks==1&&Read(game+0x4500,&row,8)&&row==2102950?0:1;}
std::unique_ptr<Mapping> Install(State& host,Mapping& image,const Boot& boot,const fs::path& user,bool authored){
 U tick=authored?0x2800:BbArmTravel::TickOffset,warp=authored?0x2c00:BbArmTravel::WarpOffset;
 check(boot.covered(tick,BbArmTravel::TickCode.size(),true)&&boot.covered(warp,BbArmTravel::WarpCode.size(),true),"travel sites not executable");
 if(!authored){
  const char *path=getenv("BB_DEV_WORLD_WARPER_DIR"),*only=getenv("BB_PAD_FILE_ONLY"),*quick=getenv("BB_QUICKSAVE"),*session=getenv("BB_ARM_SESSION_NONCE");
  check(path&&only&&!strcmp(only,"1")&&quick&&!strcmp(quick,"0")&&session&&strlen(session)==32,"travel requires private file-only pad, quicksave off and session nonce");
  directory=fs::canonical(path);check(!fs::is_symlink(path)&&directory==fs::canonical(user/"world-warper"),"travel commands outside current private user");
  char owner[64]{};ssize_t n{};check(File(user/".private-world-warper",owner,sizeof(owner),n)&&std::string_view(owner,n)==session,"travel owner marker mismatch");nonce=session;
 }
 game=image.at();warp_entry=game+warp;began=NowNs()/1e9;state={.stage=WW_IDLE,.error=""};
 auto mapping=std::make_unique<Mapping>(16384);
 auto adapter=authored?std::shared_ptr<HleCallAdapter>(std::make_shared<FixtureTick>()):MakeHleCallAdapter(Tick);
 const U target=veneer(host,host.registry.Register(framed(adapter),"development.world_warper_tick"));
 auto plan=BbArmTravel::Prepare(std::span(static_cast<const BbArmHooks::B*>(image.ptr),size_t(boot.size)),image.at(),mapping->at(),tick,warp,target,mapping->size);
 memcpy(mapping->ptr,plan.stub.data(),plan.stub.size());check(mprotect(mapping->ptr,mapping->size,PROT_READ)==0,"travel stub permissions failed");
 host.ranges.push_back({mapping->at(),mapping->size,true,false});memcpy(static_cast<BbArmHooks::B*>(image.ptr)+tick,plan.patch.data(),plan.patch.size());
 printf("ARM TRAVEL %s installed; stub=%zu bytes; three allowlisted maps, native loader only, private data, no GPU resources\n",marker,plan.stub.size());return mapping;
}
}
