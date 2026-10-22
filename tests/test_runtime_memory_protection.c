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
static void handler(int sig,siginfo_t* info,void* context){
 if(sig==SIGALRM)_exit(124);
 if(runtime_fault_recover){sigjmp_buf* saved=runtime_fault_recover;runtime_fault_recover=NULL;siglongjmp(*saved,1);}
 _exit(99);
}
static int allowed(unsigned char* p,int write){
 sigjmp_buf recover;runtime_fault_recover=&recover;
 if(sigsetjmp(recover,1)){runtime_fault_recover=NULL;return 0;}
 if(write)*(volatile unsigned char*)p=0x6b;else{volatile unsigned char value=*(volatile unsigned char*)p;(void)value;}
 runtime_fault_recover=NULL;return 1;
}
int main(int argc,char**argv){
 assert(argc==4);int old=strtoul(argv[1],NULL,0),next=strtoul(argv[2],NULL,0);unsigned reason=strtoul(argv[3],NULL,0);alarm(8);
 int64_t phys;assert(!direct_allocate(0,POOL_SIZE,PAGE*2,PAGE,3,&phys));void* out=NULL;assert(!direct_map(&out,PAGE*2,old,0,phys,0));unsigned char* p=out;
 struct sigaction action={0};action.sa_sigaction=handler;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);assert(!sigaction(SIGBUS,&action,NULL));assert(!sigaction(SIGSEGV,&action,NULL));assert(!sigaction(SIGALRM,&action,NULL));
 runtime_memory_trap((uintptr_t)p,PAGE,reason,1);int protect_result=kernel_mprotect(p,PAGE,next);
 int rejected_exec=next==5 && !(old&4) && protect_result==INVALID;
 assert(!protect_result || rejected_exec);
 int effective=rejected_exec?old:next;
 void *start,*end;uint32_t reported;assert(!query_protection(p,&start,&end,&reported));assert(reported==(unsigned)effective);
 int read=allowed(p+128,0),write=allowed(p+128,1);
 int hp=host_prot(effective),level=trap_level(reason),expected=level==2?0:level?(hp&~PROT_WRITE):hp;
 int during_ok=read==!!(expected&PROT_READ) && write==!!(expected&PROT_WRITE);
 unsigned first_reason=reason&(~reason+1u),remaining=reason&~first_reason;
 runtime_memory_trap((uintptr_t)p,PAGE,first_reason,0);
 int remaining_level=trap_level(remaining),remaining_prot=remaining_level==2?0:remaining_level?(hp&~PROT_WRITE):hp;
 int partial_ok=allowed(p+128,0)==!!(remaining_prot&PROT_READ)&&allowed(p+128,1)==!!(remaining_prot&PROT_WRITE);
 assert(runtime_memory_trap_reasons((uintptr_t)p)==remaining);
 runtime_memory_trap((uintptr_t)p,PAGE,remaining,0);
 int after_read=allowed(p+128,0),after_write=allowed(p+128,1);
 int after_ok=after_read==!!(hp&PROT_READ)&&after_write==!!(hp&PROT_WRITE);
 int neighbor_read=allowed(p+PAGE+128,0),neighbor_write=allowed(p+PAGE+128,1),oldhp=host_prot(old);
 int neighbor_ok=neighbor_read==!!(oldhp&PROT_READ)&&neighbor_write==!!(oldhp&PROT_WRITE);
 printf("RESULT old=%d next=%d reason=%u rejected_exec=%d read=%d write=%d expected=%d during_ok=%d partial_ok=%d after_ok=%d neighbor_ok=%d\n",old,next,reason,rejected_exec,read,write,expected,during_ok,partial_ok,after_ok,neighbor_ok);
 return !(during_ok&&partial_ok&&after_ok&&neighbor_ok);
}
