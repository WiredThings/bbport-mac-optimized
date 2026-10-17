// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "video_core/renderer_metal/mtl_memory_budget.h"
void snap(id<MTLDevice> d,const char* label) {
 task_vm_info_data_t i{};mach_msg_type_number_t n=TASK_VM_INFO_COUNT;
 task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&i,&n);
 printf("%s: allocated=%llu footprint=%llu graphics=%lld\n",label,(unsigned long long)d.currentAllocatedSize,(unsigned long long)i.phys_footprint,(long long)(i.ledger_tag_graphics_footprint+i.ledger_tag_graphics_footprint_compressed));
}
int main(){@autoreleasepool{
 id<MTLDevice> d=MTLCreateSystemDefaultDevice();snap(d,"start");
 const size_t size=64ul<<20;void* p=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);assert(p!=MAP_FAILED);memset(p,17,size);snap(d,"cpu pages");
 const auto before=d.currentAllocatedSize;
 id<MTLBuffer> b=[d newBufferWithBytesNoCopy:p length:size options:MTLResourceStorageModeShared deallocator:nil];assert(b);snap(d,"no-copy wrapper");
 assert(d.currentAllocatedSize >= before+size);
 const Metal::MemoryBudget::ProcessMemory memory{size,0,true};
 assert(Metal::MemoryBudget::GraphicsUsage(d.currentAllocatedSize,size,memory)==before);
 printf("no-copy alias deduplication passed\n");
 b=nil;snap(d,"wrapper released");munmap(p,size);
}}
