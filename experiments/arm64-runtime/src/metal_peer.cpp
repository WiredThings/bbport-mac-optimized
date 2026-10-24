#include "runtime_exit_codes.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "metal_peer.h"
#include "remote/bb_control.h"
#include "remote/bb_protocol.h"
#include <array>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
using namespace BbRemote;
namespace {
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
struct MetalPeer::Impl {
 int fd=-1;pid_t pid=-1;ControlBlock* shared=nullptr;
 std::unique_ptr<Channel> channel,protect;
 std::atomic<bool> stop{false},bad{false},watch_stop{false};
 std::atomic<uint64_t> vblanks{0};
 std::thread reader,protection,watchdog;
 // A prototype without full guest allocation hooks cannot grant a VM reclaim lease.
 void incoming(const Message& m,auto&& reply){
  if(m.type==MsgMemorySnapshot){std::array<uint64_t,16> unavailable{};unavailable[15]=1;reply(unavailable.data(),sizeof(unavailable));return;}
  // No VideoOut ports or guest event queues exist in this fixture. Consume the
  // backend's periodic display heartbeat; do not fabricate guest interrupt delivery.
  if(m.type==MsgVoVblank&&m.size==sizeof(VoVblankArgs)){vblanks.fetch_add(1);return;}
  std::fprintf(stderr,"PROTOTYPE unsupported GPU callback type=%u bytes=%u\n",m.type,m.size);
  bad=true;ResultReply denied{-ENOTSUP,0};reply(&denied,sizeof(denied));
 }
 void shutdown(){
  watch_stop=true;if(watchdog.joinable())watchdog.join();
  if(pid>0){
   // Only this owned child is affected, including when setup failed before handshake.
   kill(pid,SIGTERM);int status{};bool reaped=false;
   for(int i=0;i<100;++i){if(waitpid(pid,&status,WNOHANG)==pid){reaped=true;break;}usleep(10000);}
   if(!reaped){kill(pid,SIGKILL);waitpid(pid,&status,0);}pid=-1;
  }
  stop=true;
  if(shared){for(auto* c:{&shared->channel,&shared->protect})for(auto& r:c->control)Signal(r.data_word,r.data_sleepers);}
  if(reader.joinable())reader.join();if(protection.joinable())protection.join();
  channel.reset();protect.reset();if(shared){munmap(shared,ControlBytes);shared=nullptr;}if(fd>=0){close(fd);fd=-1;}
 }
 ~Impl(){shutdown();}
};
MetalPeer::MetalPeer(const std::filesystem::path& executable,const std::filesystem::path& state):impl(std::make_unique<Impl>()){
 auto& x=*impl;constexpr uint64_t pool=32ull<<20;
 auto file=(state/"metal-pool-XXXXXX").string();std::vector<char> temp(file.begin(),file.end());temp.push_back(0);
 x.fd=mkstemp(temp.data());check(x.fd>=0,"private pool create failed");unlink(temp.data());check(ftruncate(x.fd,pool)==0,"pool resize failed");
 void* memory=mmap(reinterpret_cast<void*>(ControlAddress),ControlBytes,PROT_READ|PROT_WRITE,MAP_SHARED,x.fd,0);
 if(memory!=reinterpret_cast<void*>(ControlAddress)){if(memory!=MAP_FAILED)munmap(memory,ControlBytes);throw std::runtime_error("control address occupied; no mappings replaced");}
 x.shared=new(memory) ControlBlock{};Channel::Init(&x.shared->channel);Channel::Init(&x.shared->protect);
 x.channel=std::make_unique<Channel>(&x.shared->channel,Side::Frontend);x.protect=std::make_unique<Channel>(&x.shared->protect,Side::Frontend);
 posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
 posix_spawnattr_t attr;posix_spawnattr_init(&attr);posix_spawnattr_setflags(&attr,POSIX_SPAWN_CLOEXEC_DEFAULT);
 for(int fd:{0,1,2,x.fd})posix_spawn_file_actions_addinherit_np(&actions,fd);
 auto exe=executable.string(),fd=std::to_string(x.fd),bytes=std::to_string(pool);
 std::array<char*,8> args{exe.data(),const_cast<char*>("--pool-fd"),fd.data(),const_cast<char*>("--pool-bytes"),bytes.data(),const_cast<char*>("--control-offset"),const_cast<char*>("0"),nullptr};
 // Explicit environment: no inherited online tokens, config, caches, QA routes or experiments.
 std::vector<std::string> env={"PATH=/usr/bin:/bin:/usr/sbin:/sbin","HOME="+state.string(),"TMPDIR="+state.string()+"/","BB_RENDERER=metal","BB_NATIVE_GPU=0","BB_ONLINE=0","BB_TEXTURE_RELEASED_STORAGE=0","BB_GPU_USER_DIR="+state.string(),"BB_CONFIG="+(state/"bbport.ini").string(),"BB_UPSCALER=none","BB_PIPELINE_ARCHIVE=0"};
 std::vector<char*> ep;for(auto& e:env)ep.push_back(e.data());ep.push_back(nullptr);
 const int error=posix_spawn(&x.pid,exe.c_str(),&actions,&attr,args.data(),ep.data());
 posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attr);check(error==0,"native Metal child spawn failed");
 x.watchdog=std::thread([&x]{for(int i=0;i<300&&!x.watch_stop;++i)usleep(100000);if(!x.watch_stop){kill(x.pid,SIGKILL);std::fprintf(stderr,"PROTOTYPE timeout: owned Metal child killed\n");std::_Exit(BB_ARM_EXIT_WATCHDOG);}});
 x.reader=std::thread([&x]{while(x.channel->ReceiveOne([&](auto& m,auto&& reply){x.incoming(m,reply);},[&]{return x.stop.load();})){};});
 x.protection=std::thread([&x]{while(x.protect->ReceiveOne([&](auto& m,auto&& reply){x.incoming(m,reply);},[&]{return x.stop.load();})){};});
 HelloArgs hello{ProtocolVersion,uint32_t(sysconf(_SC_PAGESIZE)),pool,GuestBegin,GuestEnd,LowBegin,LowEnd,now(),now(),now(),1000000000};HelloReply hr{};
 check(x.channel->Call(MsgHello,&hello,sizeof(hello),&hr,sizeof(hr))==sizeof(hr)&&hr.error==0&&hr.protocol==ProtocolVersion,"Metal hello failed");
 InitArgs init{};init.width=1920;init.height=1080;std::strcpy(init.title,"Blood Echoes ARM64 runtime prototype");std::strcpy(init.serial,"PROTO0001");
 check(state.string().size()<sizeof(init.user_dir),"state path too long");std::strcpy(init.user_dir,state.c_str());ResultReply ir{};
 check(x.channel->Call(MsgInit,&init,sizeof(init),&ir,sizeof(ir))==sizeof(ir)&&ir.result==0&&x.shared->state.gpu_ready.load(),"Metal initialization failed");
 std::printf("METAL native_device_ready=1 protocol=%u page_size=%u private_state=1\n",hr.protocol,hr.page_size);
}
bool MetalPeer::Idle(){ResultReply r{};auto& x=*impl;check(x.channel->Call(MsgIsGpuIdle,nullptr,0,&r,sizeof(r))==sizeof(r)&&!x.bad,"Metal idle failed or unsupported callback");return r.result==1;}
MetalPeer::~MetalPeer(){if(impl&&impl->channel&&impl->shared->state.gpu_ready.load()&&!impl->bad){ResultReply r{};impl->channel->Call(MsgShutdown,nullptr,0,&r,sizeof(r));} }
