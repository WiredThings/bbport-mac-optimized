#include "bb_sparse_packet.h"
#include <cassert>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>
#include "../vendor/metal_api/shim/bbport_cpu.h"
using namespace BbSparsePacket;
struct Clock {U mono=100000000000,cpu=5000000000,ticks=1000000000,reads{};U Mono(){++reads;return mono;}U Cpu(){++reads;return cpu;}U Ticks(){++reads;return ticks;}};
int main(){
 Clock c;Probe off(1000000000);for(U i=0;i<10000;++i){off.Before(1000000000+i,false,c);assert(!off.After(1000000001+i,false,c));}assert(!c.reads);
 for(U used:{150000000ull,100000ull,30000000ull}){Clock x;Probe p(1000000000);p.Before(x.ticks,true,x);x.mono+=150000000;x.cpu+=used;x.ticks+=150000000;auto r=p.After(x.ticks,true,x);assert(r&&r->packet_wall==150000000&&r->cpu_upper==used&&r->cpu_slack==0);assert(p.anchors==1&&p.slow_packets==1&&!p.rejected);}
 Clock x;Probe sparse(1000000000);U start=x.ticks;sparse.Before(start,true,x);assert(x.reads==3);
 for(U i=1;i<=4000;++i){x.ticks=start+i*1000;x.mono+=1000;x.cpu+=1000;sparse.Before(x.ticks,true,x);assert(!sparse.After(x.ticks+500,true,x));}assert(x.reads==3); // 4000 packets without CPU-clock reads
 x.ticks=start+5000000;x.mono=100005000000;x.cpu=5005000000;sparse.Before(x.ticks,true,x);assert(x.reads==6&&sparse.anchors==2);
 x.ticks+=4000000;x.mono+=4000000;x.cpu+=2000000;sparse.Before(x.ticks,true,x);x.ticks+=100000000;x.mono+=100000000;x.cpu+=30000000;auto r=sparse.After(x.ticks,true,x);assert(r&&r->cpu_upper==32000000&&r->cpu_slack==4000000); // 28..32ms CPU bound for handler
 x.ticks+=1000000000;x.mono+=1000000000;x.cpu+=1000;sparse.Before(x.ticks,true,x);assert(sparse.anchors==3);assert(!sparse.After(x.ticks+1,false,x));auto reads=x.reads;sparse.Before(x.ticks+2,false,x);assert(x.reads==reads);x.ticks+=3;x.mono+=3;x.cpu+=3;sparse.Before(x.ticks,true,x);assert(sparse.anchors==4); // observed gate close/reopen refreshes
 Clock bad;Probe p(1000000000);p.Before(bad.ticks,true,bad);bad.ticks-=1;assert(!p.After(bad.ticks,true,bad)&&p.rejected==1);
 Clock zero;zero.mono=0;Probe q(1000000000);q.Before(zero.ticks,true,zero);assert(!q.After(zero.ticks+100000000,true,zero)&&q.rejected==1);
 Clock delay;delay.ticks+=2000000;Probe a(1000000000);a.Before(1000000000,true,delay);assert(a.rejected==1&&!a.After(delay.ticks+100000000,true,delay));
 Clock reverse;Probe b(1000000000);b.Before(reverse.ticks,true,reverse);reverse.ticks+=100000000;reverse.mono+=100000000;--reverse.cpu;assert(!b.After(reverse.ticks,true,reverse)&&b.rejected==1);
 Clock late;Probe d(1000000000);d.Before(late.ticks,true,late);late.mono+=100000000;late.cpu+=1000000;late.ticks+=102000000;assert(!d.After(1100000000,true,late)&&d.rejected==1); // end bracket delayed >1ms
 Clock hz;hz.ticks=24000000;Probe arm(24000000);arm.Before(hz.ticks,true,hz);hz.ticks+=2400000;hz.mono+=100000000;hz.cpu+=1000000;auto z=arm.After(hz.ticks,true,hz);assert(z&&z->packet_wall==100000000);
 Clock jump;Probe over(1000000000);over.Before(jump.ticks,true,jump);jump.ticks+=100000000;jump.mono+=100000000;jump.cpu+=1000000000;assert(!over.After(jump.ticks,true,jump)&&over.rejected==1);
 Clock max;max.mono=UINT64_MAX-100;Probe wrap(1000000000);wrap.Before(max.ticks,true,max);assert(!wrap.After(max.ticks+100000000,true,max)&&wrap.rejected==1);
 Clock delayedStart;Probe delayed(1000000000);delayed.Before(delayedStart.ticks,true,delayedStart);delayed.Start(delayedStart.ticks+10000000);delayedStart.ticks+=110000000;delayedStart.mono+=110000000;delayedStart.cpu+=30000000;assert(!delayed.After(delayedStart.ticks,true,delayedStart)&&delayed.rejected==1);
 Clock actual;Probe exact(1000000000);exact.Before(actual.ticks,true,actual);exact.Start(actual.ticks+500000);actual.ticks+=100500000;actual.mono+=100500000;actual.cpu+=30200000;auto actualResult=exact.After(actual.ticks,true,actual);assert(actualResult&&actualResult->packet_wall==100000000&&actualResult->cpu_slack==500000);
 Clock untouched;Probe nofreq(0);nofreq.Before(1,true,untouched);assert(!untouched.reads&&!nofreq.After(2,true,untouched));
 struct RealClock {U Mono(){timespec t{};assert(!clock_gettime(CLOCK_MONOTONIC,&t));return U(t.tv_sec)*1000000000+t.tv_nsec;}U Cpu(){timespec t{};assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));return U(t.tv_sec)*1000000000+t.tv_nsec;}U Ticks(){return BbCpu::Ticks();}} real;
 Probe sleeping(BbCpu::TicksFromUs(1000000));auto first=real.Ticks();sleeping.Before(first,true,real);sleeping.Start(real.Ticks());std::this_thread::sleep_for(std::chrono::milliseconds(50));auto slept=sleeping.After(real.Ticks(),true,real);assert(slept&&slept->packet_wall>=40000000&&slept->cpu_upper<10000000);
 Probe burning(BbCpu::TicksFromUs(1000000));burning.Before(real.Ticks(),true,real);burning.Start(real.Ticks());auto used=real.Cpu();while(real.Cpu()-used<30000000){}auto burned=burning.After(real.Ticks(),true,real);assert(burned&&burned->cpu_upper>=30000000&&burned->cpu_upper<=burned->window_wall+burned->cpu_slack);
 puts("sparse packets: disabled/4000-hot-packets no clocks; compute/sleep/mixed CPU bounds; idle/gates; backward/zero/delayed clocks;24MHz conversion");
}
