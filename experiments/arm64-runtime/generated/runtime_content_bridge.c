// Generated from Clang AST.
#include <stdbool.h>
#include "runtime_exit_codes.h"
 #include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/bb_runtime/src/runtime_content.c"
int bb_native_runtime_content_module_load(unsigned short a0) { return (int)module_load((uint16_t)a0); }
int bb_native_runtime_content_module_loaded(unsigned short a0) { return (int)module_loaded((uint16_t)a0); }
int bb_native_runtime_content_module_unload(unsigned short a0) { return (int)module_unload((uint16_t)a0); }
int bb_native_runtime_content_content_init(const void* a0,void* a1) { if(a0)bb_arm_require_range("runtime_content.content_init.arg0",(uintptr_t)a0,sizeof(*((const unsigned char *)0)),0);if(a1)bb_arm_require_range("runtime_content.content_init.arg1",(uintptr_t)a1,sizeof(*((unsigned char *)0)),1);return (int)content_init((const unsigned char *)a0,(unsigned char *)a1); }
int bb_native_runtime_content_param_int(unsigned int a0,void* a1) { if(a1)bb_arm_require_range("runtime_content.param_int.arg1",(uintptr_t)a1,sizeof(*((int32_t *)0)),1);return (int)param_int((uint32_t)a0,(int32_t *)a1); }
int bb_native_runtime_content_addon_list(unsigned int a0,void* a1,unsigned int a2,void* a3) { if(a3)bb_arm_require_range("runtime_content.addon_list.arg3",(uintptr_t)a3,sizeof(*((uint32_t *)0)),1);return (int)addon_list((uint32_t)a0,(void *)a1,(uint32_t)a2,(uint32_t *)a3); }
void bb_arm_register_runtime_content(void) { bb_arm_add_native("module_load",(void*)module_load,(void*)bb_native_runtime_content_module_load,"iS"); bb_arm_add_native("module_loaded",(void*)module_loaded,(void*)bb_native_runtime_content_module_loaded,"iS"); bb_arm_add_native("module_unload",(void*)module_unload,(void*)bb_native_runtime_content_module_unload,"iS"); bb_arm_add_native("content_init",(void*)content_init,(void*)bb_native_runtime_content_content_init,"iRP"); bb_arm_add_native("param_int",(void*)param_int,(void*)bb_native_runtime_content_param_int,"iIP"); bb_arm_add_native("addon_list",(void*)addon_list,(void*)bb_native_runtime_content_addon_list,"iIPIP"); }
