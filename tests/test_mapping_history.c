/* SPDX-License-Identifier: GPL-2.0-or-later
 * Historical guest alias admission against the actual runtime. No GPU or game. */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include "runtime.h"
uint64_t runtime_disabled_optimizations;
__thread sigjmp_buf* runtime_fault_recover;
int32_t* runtime_errno(void){static __thread int32_t value;return &value;}
void runtime_guest_call_sites(uint64_t sites[3]){sites[0]=sites[1]=sites[2]=0;}
uintptr_t runtime_lookup(const RuntimeExport* t,size_t n,const char* s){for(size_t i=0;i<n;++i)if(!strcmp(t[i].name,s))return (uintptr_t)t[i].function;return 0;}
static int fail_history_allocation;
static void* test_calloc(size_t n,size_t size){return fail_history_allocation ? NULL : calloc(n,size);}
#ifndef MEMORY_SOURCE
#define MEMORY_SOURCE "../src/runtime_memory.c"
#endif
#undef calloc
#define calloc test_calloc
#include MEMORY_SOURCE
#undef calloc
#ifndef RECLAIM
#define RECLAIM runtime_memory_reclaim_if_unaliased_v2
#endif
#ifndef EXPECT_HISTORY
#define EXPECT_HISTORY 1
#endif
static unsigned callbacks;
static int count(void* ignored){(void)ignored;++callbacks;return 1;}
static int permits(void* address){unsigned before=callbacks;int ok=RECLAIM((uintptr_t)address,PAGE,count,NULL);assert(callbacks-before==(unsigned)!!ok);return ok;}
int main(int argc,char** argv){
 alarm(10);assert(argc==2);
 int enabled=strcmp(argv[1],"off") && strcmp(argv[1],"late");
 fail_history_allocation=!strcmp(argv[1],"oom");
 setenv("BB_TEXTURE_RELEASED_STORAGE",enabled?"1":"0",1);
 int64_t phys;assert(!direct_allocate(0,POOL_SIZE,PAGE*4,PAGE,3,&phys));
 void* a=(void*)UINT64_C(0x7000000000);
 assert(!direct_map(&a,PAGE*2,0x33,MAP_FIXED_FLAG,phys,0));
 if(!strcmp(argv[1],"late"))setenv("BB_TEXTURE_RELEASED_STORAGE","1",1);
 if(!enabled || fail_history_allocation){
  assert(permits(a)==!EXPECT_HISTORY);
#if EXPECT_HISTORY
  assert(!reclaim_mapping_origins && reclaim_mapping_history_state==(fail_history_allocation?-1:1));
#endif
  printf("HISTORY {\"mode\":\"%s\",\"no_history_refused\":%s,\"metadata_bytes\":0}\n",argv[1],EXPECT_HISTORY?"true":"false");return 0;
 }
 assert(permits(a));
 // Never simultaneous: B owns physical page2, unmaps, then C maps it at a new VA.
 void* b=(void*)UINT64_C(0x7100000000);void* c=(void*)UINT64_C(0x7200000000);
 assert(!direct_map(&b,PAGE,0x33,MAP_FIXED_FLAG,phys+PAGE*2,0));
 assert(permits(b));assert(!direct_unmap(b,PAGE));
 assert(!direct_map(&c,PAGE,0x33,MAP_FIXED_FLAG,phys+PAGE*2,0));
 const int historical_permitted=permits(c);assert(historical_permitted==!EXPECT_HISTORY);
 assert(!direct_unmap(c,PAGE));
 // Returning to B, or releasing/reallocating physical memory, cannot erase history.
 assert(!direct_map(&b,PAGE,0x33,MAP_FIXED_FLAG,phys+PAGE*2,0));
 assert(permits(b)==!EXPECT_HISTORY);assert(!direct_release(phys+PAGE*2,PAGE));
 int64_t reused;assert(!direct_allocate(phys+PAGE*2,phys+PAGE*3,PAGE,PAGE,3,&reused));assert(reused==phys+PAGE*2);
 assert(!direct_map(&c,PAGE,0x33,MAP_FIXED_FLAG,reused,0));assert(permits(c)==!EXPECT_HISTORY);
 // Affected pages only: disjoint A remains eligible; same-VA remaps and VMA splits are safe.
 assert(permits(a));assert(!kernel_mprotect((char*)a+PAGE,PAGE,1));
 assert(permits(a));assert(!kernel_mprotect((char*)a+PAGE,PAGE,0x33));
 assert(!direct_map(&a,PAGE*2,0x33,MAP_FIXED_FLAG,phys,0));assert(permits(a));
 // Partial overlap maps only page1 of A elsewhere; page0 remains untainted.
 b=(void*)UINT64_C(0x7100000000);assert(!direct_map(&b,PAGE,0x33,MAP_FIXED_FLAG,phys+PAGE,0));
 assert(!permits((char*)a+PAGE));assert(!direct_unmap(b,PAGE));
 assert(permits((char*)a+PAGE)==!EXPECT_HISTORY);assert(permits(a));
 size_t bytes=0;
#if EXPECT_HISTORY
 bytes=(size_t)(POOL_SIZE/PAGE)*sizeof(*reclaim_mapping_origins);
 assert(reclaim_mapping_history_state==2 && bytes<=4*1024*1024);
#endif
 printf("HISTORY {\"mode\":\"on\",\"historical_unmapped_alias_permitted\":%s,\"same_va_and_splits_preserved\":true,\"disjoint_mapping_preserved\":true,\"physical_release_reuse_checked\":true,\"metadata_bytes\":%zu,\"game_or_gpu\":false}\n",historical_permitted?"true":"false",bytes);
 return 0;
}
