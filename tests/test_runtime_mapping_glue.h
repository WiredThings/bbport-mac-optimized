// SPDX-License-Identifier: GPL-2.0-or-later
// Actual runtime functions loaded locally to avoid replacing shared test stubs.
// No modeled mapping admission, backing, generation or protection in this variant.
#include <dlfcn.h>
static void* mapping_library;
template<class F> static F MappingSymbol(const char* name){
    void* symbol=dlsym(mapping_library,name);assert(symbol);return reinterpret_cast<F>(symbol);
}
static bool test_mapping_alias;
static unsigned test_mapping_attempts,test_mapping_callbacks,test_nested_protects;
static unsigned (*mapping_lock_depth)();
static uintptr_t (*mapping_alias_address)();
static uint64_t (*mapping_history_bytes)();
static void* TestMappingInitialize(const char* mode){
    const char* path=std::getenv("BB_TEST_MAPPING_LIBRARY");assert(path);
    mapping_library=dlopen(path,RTLD_NOW|RTLD_LOCAL);assert(mapping_library);
    mapping_lock_depth=MappingSymbol<unsigned(*)()>("bb_test_mapping_lock_depth");
    mapping_alias_address=MappingSymbol<uintptr_t(*)()>("bb_test_mapping_alias");
    mapping_history_bytes=MappingSymbol<uint64_t(*)()>("bb_test_mapping_history_bytes");
    const unsigned which=std::strcmp(mode,"released-storage-off")==0?0:
        std::strcmp(mode,"released-storage-alias")==0?2:
        std::strcmp(mode,"released-storage-historical")==0?3:
        std::strcmp(mode,"released-storage-no-history")==0?4:1;
    test_mapping_alias=which>=2;
    void* result=MappingSymbol<void*(*)(unsigned)>("bb_test_mapping_init")(which);
    test_base=reinterpret_cast<uintptr_t>(result);return result;
}
static void TestMappingFinish(){MappingSymbol<void(*)()>("bb_test_mapping_finish")();}
extern "C" int runtime_memory_reclaim_if_unaliased_v2(uintptr_t a,uint64_t n,int(*callback)(void*),void* context){
    ++test_mapping_attempts;
    struct Call{int(*callback)(void*);void* context;} call{callback,context};
    return MappingSymbol<int(*)(uintptr_t,uint64_t,int(*)(void*),void*)>("runtime_memory_reclaim_if_unaliased_v2")(
        a,n,[](void* opaque){
            assert(mapping_lock_depth()==1);++test_mapping_callbacks;
            auto& c=*static_cast<Call*>(opaque);return c.callback(c.context);
        },&call);
}
extern "C" {
int runtime_memory_region(uintptr_t a,uintptr_t* start,uintptr_t* end,int* mapped){
    return MappingSymbol<int(*)(uintptr_t,uintptr_t*,uintptr_t*,int*)>("runtime_memory_region")(a,start,end,mapped);
}
uint64_t runtime_memory_clamp(uintptr_t a,uint64_t n){return MappingSymbol<uint64_t(*)(uintptr_t,uint64_t)>("runtime_memory_clamp")(a,n);}
void runtime_memory_read_backing(uintptr_t a,void* out,uint64_t n){MappingSymbol<void(*)(uintptr_t,void*,uint64_t)>("runtime_memory_read_backing")(a,out,n);}
void runtime_memory_gpu_protect(uintptr_t a,uint64_t n,int read,int write){
    if(mapping_lock_depth())++test_nested_protects;
    MappingSymbol<void(*)(uintptr_t,uint64_t,int,int)>("runtime_memory_gpu_protect")(a,n,read,write);
}
void runtime_memory_set_write_watch(uintptr_t a,uint64_t n,int watch){MappingSymbol<void(*)(uintptr_t,uint64_t,int)>("runtime_memory_set_write_watch")(a,n,watch);}
const uint64_t* runtime_memory_generation(){return MappingSymbol<const uint64_t*(*)()>("runtime_memory_generation")();}
}
