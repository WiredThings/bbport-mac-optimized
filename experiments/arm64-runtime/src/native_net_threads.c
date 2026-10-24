#include "runtime_exit_codes.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// Native bbnet workers are not guest entry points. Their callbacks enter FEX
// explicitly; IDs are never reused, so stale joins cannot target a new worker.
#define _DARWIN_C_SOURCE
#include "native_net_threads.h"
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
extern void runtime_thread_attach_host(const char*);
extern void runtime_thread_detach_host(void);
extern void bb_arm_set_stack_size(uint64_t);
extern int bb_arm_guest_active(void);
typedef struct Worker {
 uintptr_t id;pthread_t thread;int joining;
 void*(*entry)(void*);void*argument;uint64_t stack;char name[32];
 struct Worker*next;
} Worker;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static Worker* workers;
static uintptr_t next_id=1;
static _Thread_local Worker* current_worker;
static int native_entry(void*(*entry)(void*)) {
 Dl_info image={0};mach_vm_address_t at=(uintptr_t)entry;mach_vm_size_t bytes=0;
 vm_region_basic_info_data_64_t info={0};mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=0;
 if(!entry||!dladdr((void*)entry,&image)||!image.dli_fbase)return 0;
 kern_return_t r=mach_vm_region(mach_task_self(),&at,&bytes,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
 if(object)mach_port_deallocate(mach_task_self(),object);
 return r==KERN_SUCCESS&&at<=(uintptr_t)entry&&(uintptr_t)entry-at<bytes&&(info.protection&VM_PROT_EXECUTE);
}
static void cleanup(void*unused) {
 (void)unused;runtime_thread_detach_host();current_worker=NULL;
}
static void* start(void*value) {
 Worker*w=value;current_worker=w;runtime_thread_attach_host(w->name);
 pthread_setname_np(w->name);bb_arm_set_stack_size(w->stack);
 void*result=NULL;
 pthread_cleanup_push(cleanup,NULL);
 result=w->entry(w->argument);
 pthread_cleanup_pop(1);
 return result;
}
int32_t bb_native_worker_spawn(void**out,void*(*entry)(void*),void*argument,uint64_t stack,const char*name) {
 if(!out||!native_entry(entry)||stack>(64u<<20))return EINVAL;
 Worker*w=calloc(1,sizeof(*w));if(!w)return ENOMEM;
 w->entry=entry;w->argument=argument;w->stack=stack?stack:(1u<<20);
 snprintf(w->name,sizeof(w->name),"%s",name?name:"bbnet");
 pthread_attr_t attr;int error=pthread_attr_init(&attr);
 if(error){free(w);return error;}
 size_t host_size=(size_t)w->stack+256*1024;if(host_size<PTHREAD_STACK_MIN)host_size=PTHREAD_STACK_MIN;
 error=pthread_attr_setstacksize(&attr,host_size);
 if(error){pthread_attr_destroy(&attr);free(w);return error;}
 pthread_mutex_lock(&mutex);
 if(next_id>UINTPTR_MAX/16){pthread_mutex_unlock(&mutex);pthread_attr_destroy(&attr);free(w);return EAGAIN;}
 w->id=next_id++*16+1;
 error=pthread_create(&w->thread,&attr,start,w);
 if(!error){w->next=workers;workers=w;*out=(void*)w->id;}
 pthread_mutex_unlock(&mutex);pthread_attr_destroy(&attr);
 if(error)free(w);
 return error;
}
int32_t bb_native_worker_join(void*handle,void**result) {
 pthread_mutex_lock(&mutex);Worker*w=workers;
 while(w&&w->id!=(uintptr_t)handle)w=w->next;
 if(!w){pthread_mutex_unlock(&mutex);return ESRCH;}
 if(pthread_equal(w->thread,pthread_self())){pthread_mutex_unlock(&mutex);return EDEADLK;}
 if(w->joining){pthread_mutex_unlock(&mutex);return EINVAL;}
 w->joining=1;pthread_mutex_unlock(&mutex);
 void*value=NULL;int error=pthread_join(w->thread,&value);
 pthread_mutex_lock(&mutex);
 if(!error){Worker**link=&workers;while(*link!=w)link=&(*link)->next;*link=w->next;}
 else w->joining=0;
 pthread_mutex_unlock(&mutex);
 if(!error){if(result)*result=value;free(w);}
 return error;
}
__attribute__((noreturn)) void bb_native_worker_exit(void*value) {
 if(!current_worker||bb_arm_guest_active()){
  fputs("ARM: refused native worker exit outside an unwound native worker\n",stderr);_Exit(BB_ARM_EXIT_HLE_FAULT);
 }
 pthread_exit(value);
}
size_t bb_native_worker_count(void) {
 size_t count=0;pthread_mutex_lock(&mutex);for(Worker*w=workers;w;w=w->next)++count;pthread_mutex_unlock(&mutex);return count;
}
