/* SPDX-License-Identifier: GPL-2.0-or-later
 * Private native-test bridge to the actual runtime; no guest executable. */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "runtime.h"
uint64_t runtime_disabled_optimizations;
__thread sigjmp_buf* runtime_fault_recover;
int32_t* runtime_errno(void){static __thread int32_t value;return &value;}
void runtime_guest_call_sites(uint64_t sites[3]){sites[0]=sites[1]=sites[2]=0;}
uintptr_t runtime_lookup(const RuntimeExport* t,size_t n,const char* s){for(size_t i=0;i<n;++i)if(!strcmp(t[i].name,s))return (uintptr_t)t[i].function;return 0;}
#include "../src/runtime_memory.c"
static void* primary;
static void* alias;
void* bb_test_mapping_init(unsigned mode){
 assert(mode<=4);
 setenv("BB_TEXTURE_RELEASED_STORAGE",mode==0 || mode==4?"0":"1",1);
 int64_t phys;assert(!direct_allocate(0,POOL_SIZE,0x800000,PAGE,3,&phys));
 primary=(void*)UINT64_C(0x7000000000);
 assert(!direct_map(&primary,0x800000,0x33,MAP_FIXED_FLAG,phys,0));
 if(mode==2 || mode==3){
  alias=(void*)UINT64_C(0x7100000000);
  assert(!direct_map(&alias,PAGE,0x33,MAP_FIXED_FLAG,phys+0x100000,0));
  ((volatile unsigned char*)primary)[0x100000]=0xa5;
  assert(*(volatile unsigned char*)alias==0xa5);
  if(mode==3)assert(!direct_unmap(alias,PAGE));
 }
 if(mode==4)setenv("BB_TEXTURE_RELEASED_STORAGE","1",1);
 return primary;
}
uintptr_t bb_test_mapping_alias(void){return (uintptr_t)alias;}
unsigned bb_test_mapping_lock_depth(void){return exclusive_depth;}
uint64_t bb_test_mapping_history_bytes(void){return reclaim_mapping_origins ? POOL_SIZE/PAGE*sizeof(*reclaim_mapping_origins) : 0;}
void bb_test_mapping_finish(void){
 assert(!direct_unmap(primary,0x800000));
 if(alias){uint64_t physical;uintptr_t end;if(runtime_memory_direct_phys((uintptr_t)alias,&physical,&end))assert(!direct_unmap(alias,PAGE));}
}
