#include "remote/bb_channel.h"
#include <cassert>
#include <array>
#include <vector>
#include <string>
#include <unistd.h>
using namespace BbRemote;
int main(){
 const std::string root="/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/state/channel-audit-"+std::to_string(getpid());setenv("BB_STUTTER_AUDIT",root.c_str(),1);unsetenv("BB_STUTTER_GATE");assert(BbAudit::Enabled());
 auto* region=mmap(nullptr,ChannelBytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(region!=MAP_FAILED);Channel::Init(region);auto* layout=(ChannelLayout*)region;
 Channel front(region,Side::Frontend),back(region,Side::Backend);assert(front.Valid()&&back.Valid());
 std::array<uint8_t,MaxPayload> payload{};payload.fill(0x3c);
 constexpr unsigned prefill=RingBytes/(sizeof(MessageHeader)+MaxPayload);
 for(unsigned i=0;i<prefill;++i)front.Send(111,payload.data(),payload.size());
 std::atomic<bool> a_done{},b_started{},b_done{};
 std::thread a([&]{front.Send(777,payload.data(),payload.size());a_done=true;});
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(!layout->control[0].space_sleepers.load()){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
 std::thread b([&]{b_started=true;uint32_t value=0x12345678;front.Send(888,&value,sizeof(value));b_done=true;});
 while(!b_started.load())std::this_thread::yield();std::this_thread::sleep_for(std::chrono::milliseconds(15));
 unsigned received=0;bool seen_a=false,seen_b=false;
 while(received<prefill+2){back.ReceiveOne([&](const Message&message,auto&&){++received;if(message.type==888){assert(message.size==4&&*(const uint32_t*)message.data==0x12345678);seen_b=true;std::this_thread::sleep_for(std::chrono::milliseconds(12));}else{assert(message.type==111||message.type==777);assert(message.size==payload.size()&&!memcmp(message.data,payload.data(),payload.size()));seen_a|=message.type==777;}},[]{return false;});}
 a.join();b.join();assert(a_done&&b_done&&seen_a&&seen_b);
 uint32_t answer=0;std::thread caller([&]{uint32_t value=7;assert(front.Call(999,&value,4,&answer,4)==4);});
 back.ReceiveOne([&](const Message&message,auto&&reply){assert(message.type==999&&message.size==4);std::this_thread::sleep_for(std::chrono::milliseconds(12));const uint32_t value=0x87654321;reply(&value,4);},[]{return false;});caller.join();assert(answer==0x87654321);
 bool writer=false,space=false,handler=false,queued=false,sync=false;
 auto* trace=BbAudit::Get().data;
 for(auto&row:trace->rows){if(!row.sequence.load())continue;assert(!row.busy.load());
  writer|=row.kind==BbAudit::IPCWriterLock&&row.arg==888&&row.wall>=5000000;
  space|=row.kind==BbAudit::IPCSpaceWait&&row.arg==777&&row.wall>=5000000;
  handler|=row.kind==BbAudit::IPCHandler&&row.arg==888&&row.wall>=5000000;
  queued|=row.kind==BbAudit::IPCQueueWrite&&row.arg==777;
  sync|=row.kind==BbAudit::IPCReply&&row.arg==999&&row.wall>=5000000;
 }
 assert(writer&&space&&handler&&queued&&sync);assert(trace->h.dropped==0&&trace->h.issued<BbAudit::Capacity);munmap(region,ChannelBytes);
 printf("channel audit passed; messages=%u records=%llu bytes=%zu\n",received+1,(unsigned long long)trace->h.issued.load(),sizeof(BbAudit::Buffer));unlink((root+"-"+std::to_string(getpid())+".bin").c_str());
}
