// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-only: authored BBPROBE2 fixtures, no production save/config/cache access.
#include "core/guest_cpu/fex_guest_cpu.h"
#include "core/guest_cpu/fex_hle_bridge.h"
#include <FEXCore/Core/X86Enums.h>
#include "metal_peer.h"
#include "boot_image.h"
#include <sys/mman.h>
#include <mach/mach.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using u64=std::uint64_t;
namespace fs=std::filesystem;
using namespace Core::GuestCpu;
struct Pair{u64 x,y;};
template<> struct Core::GuestCpu::GuestAggregate<Pair>{static constexpr bool enabled=true;static constexpr auto fields=std::tuple{GuestField<u64,offsetof(Pair,x)>{},GuestField<u64,offsetof(Pair,y)>{}};};
namespace {
MetalPeer* gpu{};
u64 AddPair(Pair p){return p.x+p.y;}
u64 GpuIdle(){try{return gpu&&gpu->Idle();}catch(const std::exception& e){std::fprintf(stderr,"GPU HLE failed: %s\n",e.what());return 0;}}
u64 Offline(){return 1;}
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
struct Map {
 void* p=MAP_FAILED;size_t n{};
 explicit Map(size_t bytes):n(bytes){p=mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);check(p!=MAP_FAILED,"anonymous map failed");}
 ~Map(){if(p!=MAP_FAILED)munmap(p,n);}
 Map(const Map&)=delete;
 uintptr_t at()const{return reinterpret_cast<uintptr_t>(p);}
};
using BbBoot::Boot;
bool validate(void* ctx,uintptr_t a,size_t n,bool write){if(!a||!n||a>UINTPTR_MAX-n)return false;for(auto s:*static_cast<std::vector<Core::GuestExecutionRange>*>(ctx))if(a>=s.Begin&&n<=s.Size&&a-s.Begin<=s.Size-n&&(!write||s.Writable))return true;return false;}
void failed(void*,const HleCallFailure& f){std::fprintf(stderr,"HLE rejected name=%.*s error=%d\n",int(f.name.size()),f.name.data(),f.error);}
void memory(){task_vm_info_data_t t{};mach_msg_type_number_t n=TASK_VM_INFO_COUNT;check(task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&t,&n)==KERN_SUCCESS,"footprint query failed");std::printf("MEM runtime_footprint_bytes=%llu resident_bytes=%llu\n",t.phys_footprint,t.resident_size);}
}
int main(int argc,char** argv){
 try{
  if(argc==3&&std::string_view(argv[1])=="--inspect"){
   Boot b(argv[2]);std::printf("BOOT format=%s image_bytes=%llu imports=%zu relocations=%zu modules=%zu main_tls_bytes=%llu executed=0\n",b.format.c_str(),b.size,b.names.size(),b.relocs.size(),b.modules.size(),b.main_tls[2]);return 0;
  }
  fs::path boot,state,metal;
  for(int i=1;i<argc;++i){std::string arg=argv[i];check(i+1<argc,"missing option value");if(arg=="--boot")boot=argv[++i];else if(arg=="--state")state=argv[++i];else if(arg=="--metal")metal=argv[++i];else throw std::runtime_error("unknown option (online/restoring production data is not supported)");}
  check(!boot.empty()&&!state.empty()&&state.is_absolute(),"require --boot and a NEW absolute --state directory");
  check(fs::canonical(state.parent_path())==fs::canonical(fs::path(BB_PROTOTYPE_ROOT)/"state"),"state must be directly inside this prototype's state directory");
  // Fresh state only: no accidental reuse of production/QA/other run directories.
  check(fs::create_directory(state),"state must be a new directory");fs::permissions(state,fs::perms::owner_all,fs::perm_options::replace);
  Boot b(boot);
  check(b.format=="BBPROBE2"&&b.caps==0&&b.size<=(8u<<20),"linked module/TLS execution is not implemented; use --inspect for read-only game metadata");
  for(auto s:b.segments)check(s.address%16384==0&&s.size%16384==0&&!(s.flags&1&&s.flags&2),"unsupported execution page layout");
  HleCallRegistry registry;HleVeneerAllocator veneers;std::map<std::string,u64> imports;
  auto add=[&](auto fn,const char* name){auto a=registry.Register(MakeHleCallAdapter(fn),name);check(bool(a),"registry failed");auto v=veneers.Allocate(*a);check(std::holds_alternative<u64>(v),"veneer allocation failed");imports[name]=std::get<u64>(v);};
  add(AddPair,"bb.prototype.add_pair");add(GpuIdle,"bb.prototype.gpu_idle");add(Offline,"bb.prototype.offline");
  for(auto& n:b.names)if(!imports.contains(n))throw std::runtime_error("unsupported import: "+n);
  std::unique_ptr<MetalPeer> peer;if(!metal.empty()){peer=std::make_unique<MetalPeer>(metal,state);gpu=peer.get();}
  const size_t page=sysconf(_SC_PAGESIZE);check(page==16384,"prototype requires native 16 KiB host pages");
  Map image(b.size),stack(256u<<10),tls(page),halt(page);b.load(image.p);*static_cast<uint8_t*>(halt.p)=0xf4;
  for(auto x:b.relocs){check(x.kind<=1,"data imports not implemented");u64 value=x.kind?imports.at(b.names[x.value]):image.at()+x.value;memcpy(static_cast<char*>(image.p)+x.target,&value,8);}
  // Guest code is data to ARM64: never mark x86 bytes native-executable or patch host TLS.
  check(mprotect(image.p,b.size,PROT_NONE)==0,"image protection failed");
  std::vector<Core::GuestExecutionRange> ranges=veneers.GetExecutableRanges();
  for(auto s:b.segments){int protection=(s.flags&4?PROT_READ:0)|(s.flags&2?PROT_WRITE:0);check(mprotect(static_cast<char*>(image.p)+s.address,s.size,protection)==0,"segment protection failed");ranges.push_back({image.at()+s.address,size_t(s.size),bool(s.flags&1),bool(s.flags&2)});}
  check(mprotect(halt.p,page,PROT_READ)==0,"return protection failed");
  ranges.push_back({halt.at(),page,true,false});ranges.push_back({stack.at(),stack.n,false,true});ranges.push_back({tls.at(),tls.n,false,true});
  u64 tcb=tls.at();memcpy(tls.p,&tcb,8);const u64 sentinel=0x1234abcddcba4321;memcpy(static_cast<char*>(tls.p)+8,&sentinel,8);
  HleGuestBridge bridge(registry,validate,&ranges,failed,nullptr);
  auto result=Core::FexGuestCpuBackend::Create(bridge);check(std::holds_alternative<std::unique_ptr<Core::FexGuestCpuBackend>>(result),"FEX backend create failed");auto cpu=std::move(std::get<std::unique_ptr<Core::FexGuestCpuBackend>>(result));
  Core::GuestExecutionRequest req;req.Rip=image.at()+b.entry;req.Rsp=stack.at()+stack.n-8;req.Rflags=2;req.FsBase=tcb;req.GsBase=tcb;req.MappedRanges=ranges;
  u64 ret=halt.at();memcpy(reinterpret_cast<void*>(req.Rsp),&ret,8);
  auto run=cpu->Run(req);if(auto* f=std::get_if<Core::GuestExecutionFailure>(&run))throw std::runtime_error("guest execution failed stage="+std::to_string(int(f->Stage))+" errno="+std::to_string(f->Error));
  auto s=std::get<Core::GuestExecutionState>(run);check(s.StopReason==Core::GuestStopReason::Halted,"unexpected guest stop");
  auto value=s.Gpr[FEXCore::X86State::REG_RAX];std::printf("RUNTIME native_arm64=1 guest=x86_64 format=BBPROBE2 result=%llu offline=1\n",value);memory();
  std::ofstream(state/"result.txt")<<value<<"\n";gpu=nullptr;return value==42?0:2;
 }catch(const std::exception& e){std::fprintf(stderr,"PROTOTYPE rejected: %s\n",e.what());return 1;}
}
