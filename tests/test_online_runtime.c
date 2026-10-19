#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int32_t error_value;
const char *runtime_symbol(const char *name) { return name; }
int32_t *runtime_errno(void) { return &error_value; }
int32_t runtime_thread_spawn(void **t,void *(ABI *e)(void *),void *a,uint64_t s,const char *n) {
    (void)t;(void)e;(void)a;(void)s;(void)n; return 1;
}
int32_t runtime_thread_join_spawned(void *t,void **r) { (void)t;(void)r;return 1; }
void runtime_thread_exit(void *r) { (void)r;abort(); }
int runtime_memory_is_mapped(uintptr_t a,uint64_t s) { (void)a;(void)s;return 0; }

int main(int argc,char **argv) {
    assert(argc==4);
    setenv("BB_ONLINE",argv[1],1);
    setenv("BB_NET_MODULE",argv[2],1);
    setenv("BB_TEST_NET_FILE",argv[3],1);
    assert(runtime_net_symbol("sceNetInit"));
    assert(runtime_net_symbol("sceHttpSendRequest"));
    assert(runtime_net_symbol("sceNpMatching2CreateContext"));
    assert(!runtime_net_symbol("sceNpTrophyCreateContext"));
    assert(!runtime_net_symbol("sceNpProfileDialogOpen"));
    assert(!runtime_net_symbol("sceNpCommerceInitialize"));
    assert(!runtime_net_symbol("sceAudioOutOpen"));
    assert(!runtime_net_symbol(NULL));
    unsigned char image[16]={0};
    runtime_net_configure("isolated-game","CUSA03173","Bloodborne","01.09",image,sizeof(image),0x03500000);
    const uintptr_t found=runtime_net_resolve("sceNetInit");
    if (!strcmp(argv[1],"1")) {
        assert(found==0x1234);
        assert(access(argv[3],F_OK)==0);
        assert(!runtime_net_resolve("sceNpTrophyCreateContext"));
        assert(!runtime_net_resolve("not-a-network-import"));
    } else {
        assert(found==0);
        assert(access(argv[3],F_OK)!=0);
    }
    puts("PASS: online imports isolated; offline does not initialize the network module");
    return 0;
}
