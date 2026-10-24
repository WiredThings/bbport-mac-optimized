// SPDX-License-Identifier: MIT
#pragma once
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#ifdef __APPLE__
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace FEXCore {
// Same owner/stopped-thread contract as the original VirtualDontNeed call sites.
// This is only the thread-owned anonymous prediction stack, not its guest stack,
// executable JIT memory, or a shared/file-backed allocation.
inline void ResetCallRetStack(Core::InternalThreadState* thread) {
 auto* base=thread->CallRetStackBase;
 constexpr auto size=Core::InternalThreadState::CALLRET_STACK_SIZE;
 if(!base)return;
#ifdef __APPLE__
 const auto page=sysconf(_SC_PAGESIZE);
 if(page<=0||reinterpret_cast<uintptr_t>(base)%page||size%page)ERROR_AND_DIE_FMT("Invalid owned call-return stack reset");
 void* result=mmap(base,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
 if(result!=base)ERROR_AND_DIE_FMT("Call-return stack reset failed");
#else
 Allocator::VirtualDontNeed(base,size);
#endif
}
}
