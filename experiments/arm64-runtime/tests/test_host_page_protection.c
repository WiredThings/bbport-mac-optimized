// Authored regression: call the actual runtime protection functions on two native pages.
#include <assert.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../src/bb_darwin_fault_retry.h"
#ifdef BB_OLD_HOST_PROTECTION
#include "runtime_memory_before_host_pages.c"
#else
#include "../vendor/bb_runtime/src/runtime_memory.c"
#endif
#include <mach/mach.h>
#include <mach/mach_vm.h>
static int writable(void* p){
 mach_vm_address_t a=(uintptr_t)p;mach_vm_size_t n=0;vm_region_basic_info_data_64_t info={0};mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t obj=0;
 kern_return_t r=mach_vm_region(mach_task_self(),&a,&n,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&obj);if(obj)mach_port_deallocate(mach_task_self(),obj);assert(r==KERN_SUCCESS&&a<=(uintptr_t)p&&a+n>(uintptr_t)p);return !!(info.protection&VM_PROT_WRITE);
}
#ifndef BB_OLD_HOST_PROTECTION
static volatile sig_atomic_t stale_faults;
static void stale_fault(int signal,siginfo_t* info,void* context){
 (void)signal;(void)context;uintptr_t address=(uintptr_t)info->si_addr,pages[4];
 assert(stale_faults==0);assert(runtime_memory_host_fault_pages(address,1,pages,4)==1);
 // Deterministically emulate another thread consuming the asynchronous
 // invalidation between hardware fault and owner lookup.
 runtime_memory_gpu_protect(address&~(uintptr_t)4095,4096,1,1);
 assert(runtime_memory_host_fault_pages(address,1,pages,4)==0);
 assert(runtime_memory_host_access_now_allowed(address,1));++stale_faults;
}
#endif
int main(void){
 for(unsigned ec=0;ec<64;++ec)for(unsigned fsc=0;fsc<64;++fsc){
  int expected=(ec==0x24||ec==0x25)&&((fsc>=4&&fsc<=7)||(fsc>=12&&fsc<=15));
  assert(bb_arm_is_data_protection_fault((ec<<26)|fsc)==expected);
  assert(bb_arm_is_data_protection_fault((ec<<26)|fsc|64)==expected);
 }
 assert(sysconf(_SC_PAGESIZE)==16384);
 void* p=mmap(NULL,32768,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);assert(p!=MAP_FAILED);
 static Vma test_vmas[1];vmas=test_vmas;
 vmas[0]=(Vma){.start=(uintptr_t)p,.end=(uintptr_t)p+32768,.kind=KIND_FLEXIBLE,.prot=3};vma_count=1;
 // First subpage is aligned and Darwin rounds its protection across the host page.
 runtime_memory_gpu_protect((uintptr_t)p,4096,1,0);assert(!writable(p));
 // A watcher on subpage 1 must survive dropping subpage 0, then release successfully.
 runtime_memory_gpu_protect((uintptr_t)p+4096,4096,1,0);
 runtime_memory_gpu_protect((uintptr_t)p,4096,1,1);
 int preserved=!writable((char*)p+4096);
 runtime_memory_gpu_protect((uintptr_t)p+4096,4096,1,1);
 int released=writable((char*)p+4096),neighbor=writable((char*)p+16384);
 printf("HOST-PAGE sibling-protection=%d subpage-release=%d adjacent-host-page=%d\n",preserved,released,neighbor);
 if(!(preserved&&released&&neighbor)){munmap(p,32768);return 7;}
#ifndef BB_OLD_HOST_PROTECTION
 uintptr_t at=(uintptr_t)p,pages[4];
 runtime_memory_gpu_protect(at,16384,1,0);
 runtime_memory_gpu_protect(at,4096,1,1);
 unsigned n=runtime_memory_host_fault_pages(at+17,1,pages,4);assert(n==3);
 for(unsigned j=0;j<n;++j){assert(pages[j]==at+4096*(j+1));runtime_memory_gpu_protect(pages[j],4096,1,1);}
 assert(writable(p));
 runtime_memory_gpu_protect(at+4096,4096,1,0);runtime_memory_trap(at+8192,4096,2,1);
 runtime_memory_gpu_protect(at+4096,4096,1,1);assert(!writable(p));
 assert(runtime_memory_host_fault_pages(at+5,0,pages,4)==0);
 n=runtime_memory_host_fault_pages(at+5,1,pages,4);assert(n==1&&pages[0]==at+8192);
 runtime_memory_trap(at+8192,4096,2,0);assert(writable(p));
 runtime_memory_trap(at+12288,4096,32,1);
 n=runtime_memory_host_fault_pages(at+12290,0,pages,4);assert(n==1&&pages[0]==at+12290);
 runtime_memory_trap(at+12288,4096,32,0);assert(writable(p));
 runtime_memory_gpu_protect(at+12288,8192,1,0);assert(!writable(p)&&!writable((char*)p+16384));
 runtime_memory_gpu_protect(at+12288,4096,1,1);assert(writable(p)&&!writable((char*)p+16384));
 runtime_memory_gpu_protect(at+16384,4096,1,1);assert(writable((char*)p+16384));
 vmas[0].prot=1;runtime_memory_gpu_protect(at,4096,1,1);assert(!writable(p));
 assert(runtime_memory_host_fault_pages(at+4,1,pages,4)==0);vmas[0].prot=3;
 runtime_memory_gpu_protect(at+4096,4096,1,0);runtime_memory_trap(at+8192,4096,2,1);
 trap_forget(at,at+16384);native_protect_apply(at,at+16384,-1);assert(writable(p));
 assert(runtime_memory_host_access_now_allowed(at+8,1));
 runtime_memory_gpu_protect(at+4096,4096,1,0);assert(!runtime_memory_host_access_now_allowed(at+8,1));assert(runtime_memory_host_access_now_allowed(at+8,0));
 runtime_memory_gpu_protect(at+4096,4096,1,1);
 vmas[0].prot=1;assert(!runtime_memory_host_access_now_allowed(at+8,1));vmas[0].prot=3;
 assert(mprotect(p,16384,PROT_READ)==0);assert(!runtime_memory_host_access_now_allowed(at+8,1));assert(mprotect(p,16384,PROT_READ|PROT_WRITE)==0);
 assert(!runtime_memory_host_access_now_allowed(at+32768,1));
 runtime_memory_gpu_protect(at,4096,1,0);
 struct sigaction action={0},before_bus={0},before_segv={0};action.sa_sigaction=stale_fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGBUS,&action,&before_bus);sigaction(SIGSEGV,&action,&before_segv);
 *(volatile uint64_t*)(at+8)=UINT64_C(0x1122334455667788);assert(stale_faults==1&&*(volatile uint64_t*)(at+8)==UINT64_C(0x1122334455667788));
 sigaction(SIGBUS,&before_bus,NULL);sigaction(SIGSEGV,&before_segv,NULL);
 puts("HOST-PAGE stale real SIGBUS retries only after guest, owners and actual VM permissions allow; readonly/foreign protection/unmapped denied");
 puts("HOST-PAGE independent GPU/trap owners, read traps, real guest permissions, cross-page boundaries, forgotten mappings PASS");
 munmap(p,32768);return 0;
#else
 return 7;
#endif
}
