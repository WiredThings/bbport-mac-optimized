// Generated from Clang AST.
#include <stdbool.h>
#include "runtime_exit_codes.h"
 #include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/bb_runtime/src/runtime_pad.c"
int bb_native_runtime_pad_pad_init(void) { return (int)pad_init(); }
int bb_native_runtime_pad_pad_open(int a0,int a1,int a2,const void* a3) { return (int)pad_open((int32_t)a0,(int32_t)a1,(int32_t)a2,(const void *)a3); }
int bb_native_runtime_pad_pad_close(int a0) { return (int)pad_close((int32_t)a0); }
int bb_native_runtime_pad_pad_read_state(int a0,void* a1) { if(a1)bb_arm_require_range("runtime_pad.pad_read_state.arg1",(uintptr_t)a1,sizeof(*((PadData *)0)),1);return (int)pad_read_state((int32_t)a0,(PadData *)a1); }
int bb_native_runtime_pad_pad_read(int a0,void* a1,int a2) { if(a1)bb_arm_require_range("runtime_pad.pad_read.arg1",(uintptr_t)a1,sizeof(*((PadData *)0)),1);return (int)pad_read((int32_t)a0,(PadData *)a1,(int32_t)a2); }
int bb_native_runtime_pad_pad_info(int a0,void* a1) { if(a1)bb_arm_require_range("runtime_pad.pad_info.arg1",(uintptr_t)a1,sizeof(*((ControllerInfo *)0)),1);return (int)pad_info((int32_t)a0,(ControllerInfo *)a1); }
int bb_native_runtime_pad_pad_vibration(int a0,const void* a1) { if(a1)bb_arm_require_range("runtime_pad.pad_vibration.arg1",(uintptr_t)a1,sizeof(*((const uint8_t *)0)),0);return (int)pad_vibration((int32_t)a0,(const uint8_t *)a1); }
int bb_native_runtime_pad_pad_ok_handle(int a0) { return (int)pad_ok_handle((int32_t)a0); }
int bb_native_runtime_pad_pad_ok_handle_flag(int a0,unsigned char a1) { return (int)pad_ok_handle_flag((int32_t)a0,(uint8_t)a1); }
void bb_arm_register_runtime_pad(void) { bb_arm_add_native("pad_init",(void*)pad_init,(void*)bb_native_runtime_pad_pad_init,"i"); bb_arm_add_native("pad_open",(void*)pad_open,(void*)bb_native_runtime_pad_pad_open,"iiiiR"); bb_arm_add_native("pad_close",(void*)pad_close,(void*)bb_native_runtime_pad_pad_close,"ii"); bb_arm_add_native("pad_read_state",(void*)pad_read_state,(void*)bb_native_runtime_pad_pad_read_state,"iiP"); bb_arm_add_native("pad_read",(void*)pad_read,(void*)bb_native_runtime_pad_pad_read,"iiPi"); bb_arm_add_native("pad_info",(void*)pad_info,(void*)bb_native_runtime_pad_pad_info,"iiP"); bb_arm_add_native("pad_vibration",(void*)pad_vibration,(void*)bb_native_runtime_pad_pad_vibration,"iiR"); bb_arm_add_native("pad_ok_handle",(void*)pad_ok_handle,(void*)bb_native_runtime_pad_pad_ok_handle,"ii"); bb_arm_add_native("pad_ok_handle_flag",(void*)pad_ok_handle_flag,(void*)bb_native_runtime_pad_pad_ok_handle_flag,"iiC"); }
