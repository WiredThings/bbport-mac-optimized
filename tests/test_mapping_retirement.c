/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real-runtime mapping retirement admission, contention and reentrancy. No GPU. */
/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compile this fixture with src/low_heap.c. It exercises the real runtime memory
 * implementation without a guest executable, Metal device, settings or saves.
 */
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
#ifndef MEMORY_SOURCE
#define MEMORY_SOURCE "../src/runtime_memory.c"
#endif
#include MEMORY_SOURCE

static unsigned cases,calls;static uintptr_t base;static uint64_t phys_start;
static _Atomic unsigned ready,release_holder,mapper_started,mapper_finished;
static void* alias_address;
static int count(void* ignored){(void)ignored;++calls;return 1;}
static void check(int want,uintptr_t a,uint64_t bytes){unsigned before=calls;int got=runtime_memory_reclaim_if_unaliased_v2(a,bytes,count,NULL);assert(got==want);assert(calls-before==(unsigned)want);assert(!(table_generation&1));++cases;}
static void* hold_read(void* p){(void)p;read_lock();atomic_store(&ready,1);while(!atomic_load(&release_holder))usleep(100);read_unlock();return NULL;}
static void* hold_write(void* p){(void)p;write_lock();atomic_store(&ready,1);while(!atomic_load(&release_holder))usleep(100);write_unlock();return NULL;}
static int nested(void* p){(void)p;uint64_t phys;uintptr_t end;assert(runtime_memory_direct_phys(base,&phys,&end));assert(phys==phys_start);runtime_memory_gpu_protect(base,PAGE,1,1);uint32_t protection;assert(!query_protection((void*)base,NULL,NULL,&protection));assert(protection==0x33);assert(read_depth==0 && exclusive_depth==1);assert(!runtime_memory_reclaim_if_unaliased_v2(base,PAGE,count,NULL));return 1;}
static void* map_alias(void* p){(void)p;assert(pthread_rwlock_trywrlock(&lock)!=0);atomic_store(&mapper_started,1);assert(!direct_map(&alias_address,PAGE,0x33,0,phys_start,0));atomic_store(&mapper_finished,1);return NULL;}
static pthread_t mapper;
static int held_callback(void* p){(void)p;assert(!pthread_create(&mapper,NULL,map_alias,NULL));while(!atomic_load(&mapper_started))usleep(100);usleep(50000);assert(!atomic_load(&mapper_finished));assert(reclaim_mapping_unique_locked(base,PAGE));return 1;}
int main(){alarm(10);setenv("BB_TEXTURE_RELEASED_STORAGE","1",1);int64_t phys;assert(!direct_allocate(0,POOL_SIZE,PAGE*4,PAGE,3,&phys));phys_start=phys;void* a=NULL;assert(!direct_map(&a,PAGE*2,0x33,0,phys,0));base=(uintptr_t)a;
check(1,base,PAGE);check(1,base,PAGE*2);check(0,base,0);check(0,base,UINT64_MAX);check(0,UINT64_MAX,PAGE);check(0,base+PAGE*2,PAGE);
void* b=NULL;assert(!direct_map(&b,PAGE,0x33,0,phys+PAGE,0));check(0,base,PAGE*2);check(1,base,PAGE);assert(!direct_unmap(b,PAGE));
assert(!direct_map(&b,PAGE,0x33,0,phys,0));check(0,base,PAGE);assert(!direct_unmap(b,PAGE));
assert(!direct_map(&b,PAGE,0x33,0,phys+PAGE*3,0));check(0,base,PAGE*2);assert(!direct_unmap(b,PAGE));
void* clean=NULL;assert(!direct_map(&clean,PAGE,0x33,0,phys+PAGE*2,0));base=(uintptr_t)clean;phys_start=phys+PAGE*2; /* Earlier aliased pages are permanently tainted. */
read_lock();check(0,base,PAGE);read_unlock();write_lock();unsigned before=calls;assert(!runtime_memory_reclaim_if_unaliased_v2(base,PAGE,count,NULL));assert(calls==before);++cases;write_unlock();
for(unsigned mode=0;mode<2;++mode){atomic_store(&ready,0);atomic_store(&release_holder,0);pthread_t thread;assert(!pthread_create(&thread,NULL,mode?hold_write:hold_read,NULL));while(!atomic_load(&ready))usleep(100);before=calls;assert(!runtime_memory_reclaim_if_unaliased_v2(base,PAGE,count,NULL));assert(calls==before);atomic_store(&release_holder,1);assert(!pthread_join(thread,NULL));++cases;}
assert(runtime_memory_reclaim_if_unaliased_v2(base,PAGE,nested,NULL));assert(!read_depth && !exclusive_depth && !(table_generation&1));++cases;
assert(runtime_memory_reclaim_if_unaliased_v2(base,PAGE,held_callback,NULL));assert(!pthread_join(mapper,NULL));assert(atomic_load(&mapper_finished));check(0,base,PAGE);assert(!direct_unmap(alias_address,PAGE));++cases;
printf("MAPPING_RETIREMENT {\"checks\":%u,\"concurrent_alias_blocked_until_callback_end\":true,\"read_and_write_contention_skipped\":true,\"nested_protection_queries_passed\":true,\"game_integration\":false,\"actual_texture_freed\":false}\n",cases);return 0;}
