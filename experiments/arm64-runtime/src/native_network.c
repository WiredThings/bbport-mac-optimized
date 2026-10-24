#include "runtime_exit_codes.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit private ABI3 loader. Offline never dlopens or allocates this module.
#define BB_LOW_HEAP_IMPL
#include "runtime.h"
#include "bbnet.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <limits.h>
extern void*bb_low_aligned_alloc(size_t,size_t);
extern void bb_low_free(void*);
extern uint64_t bb_arm_net_call(uintptr_t,const uint64_t*,size_t);
static BbNetHost host;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static BbNetResolveFn resolve;
static void*(*adapter_factory)(uintptr_t);
int runtime_net_enabled(void) {
 const char*a=getenv("BB_ONLINE"),*b=getenv("BB_ARM_ALLOW_NETWORK");
 return a&&!strcmp(a,"1")&&b&&!strcmp(b,"1");
}
int runtime_net_symbol(const char*name) {
 return name&&(!strncmp(name,"sceNet",6)||!strncmp(name,"sceNp",5)||!strncmp(name,"sceHttp",7)||!strncmp(name,"sceSsl",6))&&strncmp(name,"sceNpTrophy",11)&&strncmp(name,"sceNpCommerce",13)&&strncmp(name,"sceNpProfileDialog",18);
}
static void*allocate(uint64_t size,uint64_t alignment) {
 if(!size)size=1;
 if(alignment<16||alignment>65536||(alignment&(alignment-1))||size>SIZE_MAX-(alignment-1))return NULL;
 return bb_low_aligned_alloc(alignment,(size+alignment-1)&~(alignment-1));
}
static int mapped(uintptr_t address,uint64_t size){return bb_arm_range(address,size,0);}
void runtime_net_configure(const char*app0,const char*serial,const char*title,const char*version,unsigned char*image,uint64_t size,uint32_t sdk) {
 host=(BbNetHost){.abi=BBNET_ABI_VERSION,.app0=app0,.serial=serial,.title=title,.app_ver=version,.thread_spawn=runtime_thread_spawn,.thread_join=runtime_thread_join_spawned,.thread_exit=runtime_thread_exit,.memory_is_mapped=mapped,.image=image,.image_size=size,.sdk_version=sdk,.errno_location=runtime_errno,.guest_call=bb_arm_net_call,.guest_alloc=allocate,.guest_free=bb_low_free};
}
static void load(void) {
 const char*path=getenv("BB_NET_MODULE");const char*backend=getenv("BB_NET_BACKEND");
 char canonical[PATH_MAX];const char*root=BB_PROTOTYPE_ROOT "/build/";
 if(!path||!realpath(path,canonical)||strncmp(canonical,root,strlen(root))||!backend||strcmp(backend,"thd")){fputs("ARM NET refused non-private module or unsupported backend\n",stderr);_Exit(BB_ARM_EXIT_NETWORK_INIT);}
 void*module=dlopen(canonical,RTLD_NOW|RTLD_LOCAL);if(!module){fprintf(stderr,"ARM NET module unavailable: %s\n",dlerror());_Exit(BB_ARM_EXIT_NETWORK_INIT);}
 BbNetInitFn init=(BbNetInitFn)dlsym(module,"bbnet_init");
 BbNetResolveFn found=(BbNetResolveFn)dlsym(module,"bbnet_resolve");
 void*(*factory)(uintptr_t)=(void*(*)(uintptr_t))dlsym(module,"bbnet_arm_adapter");
 if(!init||!found||!factory||init(&host)){fputs("ARM NET private ABI3 initialization failed\n",stderr);_Exit(BB_ARM_EXIT_NETWORK_INIT);}
 resolve=found;adapter_factory=factory;puts("ARM NET private ARM64 module and typed guest bridge ready");
}
uintptr_t runtime_net_resolve(const char*name) {
 if(!runtime_net_enabled()||!runtime_net_symbol(runtime_symbol(name)))return 0;
 pthread_once(&once,load);return resolve(name);
}
void*bb_arm_net_adapter(uintptr_t address) {return adapter_factory?adapter_factory(address):NULL;}
