// Authored test of the actual native worker bridge and actual runtime TCB list.
#include <assert.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#define BB_LOW_HEAP_IMPL /* Unit-only host allocation; real FEX fixture covers low TCB pointers. */
#include "../vendor/bb_runtime/src/runtime_thread.c"
static _Atomic unsigned key_cleaned,tls_cleaned,entered;
void runtime_thread_keys_cleanup(void){assert(bb_arm_tcb());atomic_fetch_add(&key_cleaned,1);}
void runtime_thread_tls_cleanup(void){assert(bb_arm_tcb());atomic_fetch_add(&tls_cleaned,1);}
void bb_arm_set_stack_size(uint64_t n){assert(n>=65536&&n<=64u<<20);}
int bb_arm_guest_active(void){return 0;}
uint64_t bb_arm_call(uintptr_t e,const uint64_t*a,size_t n){(void)e;(void)a;(void)n;assert(!"Native entry must never enter FEX");return 0;}
static void*entry(void*arg){
 assert(current&&current->host_owned&&bb_arm_tcb());
 assert(((uintptr_t*)bb_arm_tcb())[0]==(uintptr_t)bb_arm_tcb());
 assert(runtime_thread_current()==current);
 atomic_fetch_add(&entered,1);
 if((uintptr_t)arg%3==1)runtime_thread_exit(arg);
 if((uintptr_t)arg%3==2)pthread_exit(arg);
 return arg;
}
static _Atomic int self_ready;
static void*self_join(void*arg){
 while(!atomic_load(&self_ready))sched_yield();
 assert(runtime_thread_join_spawned(*(void**)arg,NULL)==EDEADLK);return (void*)123;
}
int main(void){
 int data=0;void*h=NULL;
 assert(runtime_thread_spawn(NULL,entry,NULL,65536,"bad")==EINVAL);
 assert(runtime_thread_spawn(&h,NULL,NULL,65536,"bad")==EINVAL);
 assert(runtime_thread_spawn(&h,(void*(*)(void*))&data,NULL,65536,"data")==EINVAL);
 assert(runtime_thread_spawn(&h,entry,NULL,65u<<20,"huge")==EINVAL);
 for(unsigned batch=0;batch<128;++batch){
  void*ids[8];
  for(unsigned i=0;i<8;++i)assert(!runtime_thread_spawn(&ids[i],entry,(void*)(uintptr_t)(batch*8+i),65536,"net-regression"));
  assert(bb_native_worker_count()==8);
  for(unsigned i=0;i<8;++i){void*v=(void*)-1;assert(!runtime_thread_join_spawned(ids[i],&v));assert(v==(void*)(uintptr_t)(batch*8+i));assert(runtime_thread_join_spawned(ids[i],NULL)==ESRCH);}
  assert(bb_native_worker_count()==0&&threads==NULL);
 }
 assert(entered==1024&&key_cleaned==1024&&tls_cleaned==1024);
 assert(!runtime_thread_spawn(&h,self_join,&h,65536,"self-join"));atomic_store(&self_ready,1);void*v=NULL;assert(!runtime_thread_join_spawned(h,&v)&&v==(void*)123);
 assert(bb_native_worker_count()==0&&threads==NULL&&key_cleaned==1025&&tls_cleaned==1025);
 assert(runtime_thread_join_spawned((void*)0x12345,NULL)==ESRCH);
 pid_t child=fork();assert(child>=0);if(!child)runtime_thread_exit(NULL);int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==74);
 puts("PASS 1025 native worker lifecycles, return/explicit exit, TCB/TLS cleanup, stale/self/bad joins and non-native entries");
}
