// SPDX-License-Identifier: MIT
#include <FEXCore/Utils/AllocatorHooks.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#ifdef BB_OLD_JIT_SCOPE
struct Scope {Scope(){pthread_jit_write_protect_np(0);}~Scope(){pthread_jit_write_protect_np(1);}};
#else
using Scope=FEXCore::Allocator::ScopedJITWriteProtect;
#endif
int main(){
 const size_t page=sysconf(_SC_PAGESIZE);auto* memory=static_cast<volatile unsigned char*>(mmap(nullptr,page,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON|MAP_JIT,-1,0));if(memory==MAP_FAILED)return 2;
 {
  Scope outer;memory[0]=1;
  {Scope inner;memory[1]=2;}
  memory[2]=3; // Original scope helper faults here after the inner destructor.
 }
 auto child=fork();if(child<0)return 3;
 if(!child){struct sigaction sa{};sa.sa_handler=SIG_DFL;sigaction(SIGBUS,&sa,nullptr);sigaction(SIGSEGV,&sa,nullptr);memory[3]=4;_Exit(4);}
 int status{};waitpid(child,&status,0);
 if(!WIFSIGNALED(status)||(WTERMSIG(status)!=SIGBUS&&WTERMSIG(status)!=SIGSEGV))return 5;
 if(memory[0]!=1||memory[1]!=2||memory[2]!=3)return 6;
 munmap(const_cast<unsigned char*>(memory),page);std::puts("JIT scope nested_write=ok outer_execute_protection_restored=ok");return 0;
}
