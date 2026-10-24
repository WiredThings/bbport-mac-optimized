// Authored test: real Darwin protection and alignment faults.
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <stdio.h>
#include <stdint.h>
#include <cassert>
#include "darwin_fault.h"
static sigjmp_buf jump;
static volatile int caught,code;static volatile unsigned esr;
static void fault(int sig,siginfo_t* info,void* c){caught=sig;code=info->si_code;esr=((ucontext_t*)c)->uc_mcontext->__es.__esr;siglongjmp(jump,1);}
int main(){struct sigaction a={};a.sa_sigaction=fault;a.sa_flags=SA_SIGINFO;sigaction(SIGBUS,&a,nullptr);sigaction(SIGSEGV,&a,nullptr);void* p=mmap(nullptr,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);mprotect(p,16384,PROT_READ);if(!sigsetjmp(jump,1))__asm__ volatile("str q0,[%0]"::"r"(p):"memory");printf("write-protect: sig=%d code=%d esr=%#x EC=%#x DFSC=%#x WnR=%d\n",caught,code,esr,esr>>26,esr&63,(esr>>6)&1);assert(caught==SIGBUS && code==BUS_ADRALN && !AetherPS4::Fex::IsDarwinDataAlignmentEsr(esr));mprotect(p,16384,PROT_READ|PROT_WRITE);caught=0;code=0;esr=0;if(!sigsetjmp(jump,1))__asm__ volatile("ldar x0,[%0]"::"r"((char*)p+15):"x0","memory");printf("unaligned: sig=%d code=%d esr=%#x EC=%#x DFSC=%#x WnR=%d\n",caught,code,esr,esr>>26,esr&63,(esr>>6)&1);assert(caught==SIGBUS && code==BUS_ADRALN && AetherPS4::Fex::IsDarwinDataAlignmentEsr(esr));munmap(p,16384);}
