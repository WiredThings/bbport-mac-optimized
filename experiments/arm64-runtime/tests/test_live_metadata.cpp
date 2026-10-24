// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_resource_lifetime.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
using namespace BbResourceLifetime;
int main(int argc,char**){setenv("BB_GPU_LIFETIME","1",1);const bool extra=argc>1;if(extra)setenv("BB_ARM_EXTRA_HEAPS","1",1);else unsetenv("BB_ARM_EXTRA_HEAPS");auto& tracker=Get();
 if(!extra){for(unsigned i=0;i<32769;++i)tracker.Allocate(Domain::Heap,0x7000000000ull+32ull*i,16);auto c=tracker.Read().domains[1];assert(c.live_ranges==32768&&c.dropped==1);puts("LIVE METADATA default bound unchanged");return 0;}
 Tracker local(2);for(unsigned i=0;i<3;++i)local.Allocate(Domain::Heap,0x800000+32*i,16);assert(local.Read().domains[1].dropped==1);
 Tag first,last;for(unsigned i=0;i<40000;++i){auto t=tracker.Allocate(Domain::Heap,0x7000000000ull+32ull*i,16);if(!i)first=t;last=t;}assert(tracker.Read().domains[1].live_ranges==40000&&tracker.Read().domains[1].dropped==0);
 for(unsigned i=0;i<40000;++i){auto t=tracker.Find(Domain::Heap,0x7000000000ull+32ull*i);assert(tracker.Release(t));}assert(tracker.Status(first)==State::Unknown&&tracker.Status(last)==State::Released); // history stayed 32768
 Tag oldest;for(unsigned i=0;i<262145;++i){auto t=tracker.Allocate(Domain::Heap,0x7100000000ull+32ull*i,16);if(!i)oldest=t;}auto c=tracker.Read().domains[1];assert(c.live_ranges==262144&&c.dropped==1);assert(tracker.Release(oldest));assert(!tracker.ReleasedRangeUnused(oldest,oldest.address,oldest.size,0));
 puts("LIVE METADATA private Get-only lazy live bound; history/default/local limits unchanged; hard overflow still denies reclaim");}
