#include "bb_typed_adapter.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
using namespace Core::GuestCpu;
static int failed,calls;static char* mem;static size_t page;static bool write_ok=true;
static void check(bool v,const char*n){printf("%s %s\n",v?"PASS":"FAIL",n);failed+=!v;}
static HleCallFrame frame(){HleCallFrame f{};f.validate_range=[](void*,uintptr_t a,size_t n,bool w){auto b=(uintptr_t)mem;return (!w||write_ok)&&a>=b&&a<b+page&&n<=b+page-a;};return f;}
static int push(int,int,void*cb,void*arg){++calls;return (uintptr_t)cb==0x12345678&&(uintptr_t)arg==1?73:74;}
static bool ok(const HleCallResult&r){return std::holds_alternative<bool>(r)&&std::get<bool>(r);}
static bool rejected(const HleCallResult&r){auto*e=std::get_if<HleCallFailure>(&r);return e&&e->error==EFAULT;}
static int event_fake(int,int,int,void*){++calls;return 0;}
static int recv_fake(int,void*,uint64_t,int){++calls;return 0;}
static int header_fake(int64_t,const char*,char*,uint64_t){++calls;return 0;}
static int npid_fake(int,void*){++calls;return 0;}
template<BbNet::DataPolicy P,size_t N>bool valid(std::array<uint64_t,N>v){auto f=frame();return BbNet::ValidateData(f,v,P);}
int main(){
 page=getpagesize();mem=(char*)mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(mem==MAP_FAILED||mprotect(mem+page,page,PROT_NONE))return 2;
 const uintptr_t end=(uintptr_t)(mem+page),base=(uintptr_t)mem;
 BbNet::TypedAdapter<decltype(&push)> cb(push,BbNet::OpaqueMask("webapi_reg_push"));auto f=frame();f.gpr[2]=0x12345678;f.gpr[1]=1;
 check(ok(cb.Invoke(f))&&f.gpr[0]==73&&calls==1,"actual THD void* callback and opaque user token pass unchanged");
 BbNet::TypedAdapter<decltype(&push)> unknown(push,BbNet::OpaqueMask("webapi_reg_push_suffix"));f=frame();f.gpr[2]=0x12345678;int before=calls;check(rejected(unknown.Invoke(f))&&calls==before,"unknown alias does not bypass data checks");
 for(auto n:{"net_recv","net_recvfrom","webapi_read"})check(BbNet::ByteSpanRule(n)==BbNet::ByteSpan::OutputArg1Length2,"actual THD output byte-span selected");
 for(auto n:{"net_send","net_sendto","webapi_send"})check(BbNet::ByteSpanRule(n)==BbNet::ByteSpan::InputArg1Length2,"actual THD input byte-span selected");
 BbNet::TypedAdapter<decltype(&recv_fake),BbNet::ByteSpanRule("net_recv")> recv(recv_fake,0);
 f=frame();f.gpr[6]=end-8;f.gpr[2]=8;before=calls;check(ok(recv.Invoke(f))&&calls==before+1,"actual alias boundary buffer accepted");
 f.gpr[2]=9;before=calls;check(rejected(recv.Invoke(f))&&calls==before,"actual alias overrun rejected before host");
 BbNet::TypedAdapter<decltype(&header_fake),BbNet::ByteSpan::None,BbNet::DataRule("webapi_hdr_val")> header(header_fake,0);
 memcpy(mem,"Content-Type",13);f=frame();f.gpr[6]=base;f.gpr[2]=end-8;f.gpr[1]=8;check(ok(header.Invoke(f)),"header string plus exact output span accepted");
 f.gpr[1]=9;before=calls;check(rejected(header.Invoke(f))&&calls==before,"header span crossing guard rejected");
 memset(mem+page-8,'x',8);f.gpr[6]=end-8;f.gpr[1]=8;before=calls;check(rejected(header.Invoke(f))&&calls==before,"unterminated string crossing guard rejected");
 mem[page-1]=0;check(ok(header.Invoke(f)),"terminator at last mapped byte accepted");
 BbNet::TypedAdapter<decltype(&npid_fake),BbNet::ByteSpan::None,BbNet::DataRule("np_get_npid")> npid(npid_fake,0);
 f=frame();f.gpr[6]=end-36;check(ok(npid.Invoke(f)),"full 36-byte NP ID accepted");f.gpr[6]=end-35;before=calls;check(rejected(npid.Invoke(f))&&calls==before,"one-byte-minimum NP ID overrun rejected");
 using V=std::array<uint64_t,6>;V v{};
 auto indirect=[&](auto policy,unsigned ptr,unsigned len,unsigned capacity){v={};v[ptr]=end-16;v[len]=base;memcpy(mem,&capacity,4);auto f=frame();return BbNet::ValidateData(f,v,policy);};
 check(indirect(BbNet::DataRule("net_recvfrom"),4,5,16),"recvfrom indirect address length accepted");
 check(!indirect(BbNet::DataRule("net_recvfrom"),4,5,17),"recvfrom indirect overrun rejected");
 v={};v[4]=end-16;v[5]=end-2;check(!valid<BbNet::DataRule("net_recvfrom")>(v),"length slot crossing guard rejected");
 v={};v[1]=end-48;v[2]=2;check(valid<BbNet::DataRule("net_epoll_wait")>(v),"two complete epoll events accepted");v[2]=3;check(!valid<BbNet::DataRule("net_epoll_wait")>(v),"epoll array count checked");v[2]=UINT64_MAX;check(!valid<BbNet::DataRule("net_epoll_wait")>(v),"epoll count multiplication overflow rejected");
 v={};v[3]=end-24;check(valid<BbNet::DataRule("net_epoll_control")>(v),"fixed epoll event accepted");v[3]++;check(!valid<BbNet::DataRule("net_epoll_control")>(v),"fixed epoll overrun rejected");
 BbNet::TypedAdapter<decltype(&event_fake),BbNet::ByteSpan::None,BbNet::DataRule("net_epoll_control")> event(event_fake,0);
 f=frame();f.gpr[1]=end-24;write_ok=false;check(ok(event.Invoke(f)),"mutable void* declared event accepts read-only input mapping");write_ok=true;
 v={};v[1]=end-16;write_ok=false;check(!valid<BbNet::DataRule("ctl_get_info")>(v),"fixed output to read-only memory rejected");write_ok=true;
 v={};v[2]=end-128;check(valid<BbNet::DataRule("npauth_get_code")>(v),"full auth code output accepted");v[2]++;check(!valid<BbNet::DataRule("npauth_get_code")>(v),"auth code output overrun rejected");
 v={};v[1]=end-2;v[2]=base;check(valid<BbNet::DataRule("m2_create_join_room")>(v),"two-byte matching room input accepted");v[1]++;check(!valid<BbNet::DataRule("m2_create_join_room")>(v),"typed-byte request prefix must cover both bytes");
 check(BbNet::DataRule("not_registered")==BbNet::DataPolicy{},"unknown names gain no invented pointer semantics");
 munmap(mem,2*page);printf("THD pointer failures=%d\n",failed);return failed?1:0;
}
