/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#define BB_LOW_HEAP_IMPL /* Test owns only authored metadata; no guest heap allocation. */
#include "../vendor/bb_runtime/src/runtime_memory.c"
static const uintptr_t base=UINT64_C(0x7000000000),alias=UINT64_C(0x7100000000);
static void* writer(void* ignored){(void)ignored;for(unsigned i=0;i<10000;++i){write_lock();vmas[1].phys=(i&1)?0x1000:0x800000;write_unlock();}return NULL;}
int main(void){
 static Vma records[5];vmas=records;vma_count=vma_capacity=5;
 records[0]=(Vma){.start=base,.end=base+0x10000,.kind=KIND_DIRECT,.prot=0x33,.type=3,.phys=0x1000};
 records[1]=(Vma){.start=alias,.end=alias+0x8000,.kind=KIND_DIRECT,.prot=1,.type=3,.phys=0x3000};
 records[2]=(Vma){.start=alias+0x10000,.end=alias+0x18000,.kind=KIND_FLEXIBLE,.prot=3};
 records[3]=(Vma){.start=alias+0x20000,.end=alias+0x28000,.kind=KIND_RESERVED,.prot=0};
 records[4]=(Vma){.start=alias+0x30000,.end=alias+0x38000,.kind=KIND_DIRECT,.prot=0x33,.type=3,.phys=0x1000,.private_mem=1};
 RuntimeMappingSnapshot map;assert(runtime_memory_inspect_mapping(base+0x2800,&map)&&map.begin==base&&map.physical==0x1000&&map.prot==0x33&&map.type==3);
 assert(!runtime_memory_inspect_mapping(base-1,&map)&&!map.begin);assert(!runtime_memory_inspect_mapping(alias+0x20000,&map));
 RuntimeMappingAlias out[8];int truncated;uint64_t generation;
 size_t n=runtime_memory_inspect_aliases(base+0x2800,0x2000,out,8,&truncated,&generation);assert(n==2&&!truncated&&out[0].address==base+0x2800&&out[1].address==alias+0x800&&out[1].physical==0x3800&&out[1].size==0x2000);
 n=runtime_memory_inspect_aliases(base+0x2800,0x2000,out,1,&truncated,&generation);assert(n==1&&truncated);
 assert(!runtime_memory_inspect_aliases(base+0xf000,0x2000,out,8,&truncated,&generation));
 assert(!runtime_memory_inspect_aliases(alias+0x10000,1,out,8,&truncated,&generation));
 assert(!runtime_memory_inspect_aliases(alias+0x30000,1,out,8,&truncated,&generation));
 assert(!runtime_memory_inspect_aliases(UINTPTR_MAX-8,32,out,8,&truncated,&generation));
 assert(!runtime_memory_inspect_aliases(base,0,out,8,&truncated,&generation));
 records[0].host_owned=1;assert(!runtime_memory_inspect_mapping(base,&map));records[0].host_owned=0;
 pthread_t thread;assert(!pthread_create(&thread,NULL,writer,NULL));
 for(unsigned i=0;i<20000;++i){n=runtime_memory_inspect_aliases(base+0x2800,0x2000,out,8,&truncated,&generation);assert((n==1||n==2)&&!truncated&&!(generation&1));for(size_t j=0;j<n;++j)assert(out[j].physical==0x3800&&out[j].size==0x2000);}
 assert(!pthread_join(thread,NULL));puts("MEMORY INSPECTION real table queries/physical aliases/partial/private/host-owned/truncation and concurrent remaps passed; no mappings or permissions changed");
}
