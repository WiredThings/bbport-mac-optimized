#include "bb_stutter_audit.h"
#include <cassert>
#include <thread>
#include <vector>
#include <chrono>
#include <string>
int main(int argc,char**){
 const bool coarse=argc>1;if(coarse)setenv("BB_STUTTER_COARSE","1",1);else unsetenv("BB_STUTTER_COARSE");
 std::string path="/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/state/audit-unit-"+std::to_string(getpid());
 std::string control=path+"-gate";int fd=open(control.c_str(),O_RDWR|O_CREAT|O_EXCL,0600);assert(fd>=0);assert(ftruncate(fd,4096)==0);
 auto* gate=static_cast<std::atomic<uint32_t>*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));close(fd);assert(gate!=MAP_FAILED);
 new(gate)std::atomic<uint32_t>{0};setenv("BB_STUTTER_GATE",control.c_str(),1);
 setenv("BB_STUTTER_AUDIT",path.c_str(),1);assert(!BbAudit::Enabled());gate->store(1);assert(BbAudit::Enabled());
 assert(BbAudit::EnabledFor(BbAudit::FexCodegen) && BbAudit::EnabledFor(BbAudit::FrameInterval) && BbAudit::EnabledFor(BbAudit::LifetimeCollect));
 if(coarse){for(auto kind:{31u,32u,33u,21u,24u,26u}){BbAudit::Scope excluded{kind,0,0};assert(!excluded.start);BbAudit::Record(kind,1,2,3);}assert(BbAudit::Get().data->h.issued==0);}
 BbAudit::Store other;assert(other.data==nullptr || other.data->h.pid==uint64_t(getpid())); // same trace, no truncation
 {BbAudit::Scope slow{BbAudit::FexCodegen,42,100000};std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 auto* b=BbAudit::Get().data;assert(b->rows[0].sequence==1&&b->rows[0].arg==42);
 assert(b->rows[0].wall>=4000000&&b->rows[0].cpu<b->rows[0].wall);
 // Simulate a writer delayed for a full wrap: it must not overwrite a newer publication.
 b->rows[1].sequence.store(BbAudit::Capacity+2);
 BbAudit::Record(BbAudit::FexPublish,1,2,3,0);
 assert(b->h.issued==2&&b->h.dropped>=1&&b->rows[1].sequence==BbAudit::Capacity+2);b->rows[1].sequence.store(0);
 std::vector<std::thread> workers;
 for(unsigned t=0;t<8;++t)workers.emplace_back([t]{for(unsigned i=0;i<8192;++i)BbAudit::Record(BbAudit::FexPublish,1,2,3,t);});
 for(auto& t:workers)t.join();
 assert(b->h.issued==65538);unsigned valid=0;for(const auto& r:b->rows){assert(!r.busy);if(r.sequence&&r.kind==BbAudit::FexPublish){assert(r.start==1&&r.wall==2&&r.cpu==3&&r.arg<8);++valid;}}
 assert(valid>1000&&sizeof(BbAudit::Buffer)==1048640);
 printf("audit concurrency passed; issued=%llu dropped=%llu bytes=%zu\n",(unsigned long long)b->h.issued.load(),(unsigned long long)b->h.dropped.load(),sizeof(BbAudit::Buffer));
 gate->store(0);assert(!BbAudit::Enabled());unlink(control.c_str());
 unlink((path+"-"+std::to_string(getpid())+".bin").c_str());
}
