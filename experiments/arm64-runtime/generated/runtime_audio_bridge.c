// Generated from Clang AST.
#include <stdbool.h>
#include "runtime_exit_codes.h"
 #include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/bb_runtime/src/runtime_audio.c"
int bb_native_runtime_audio_audio_init(void) { return (int)audio_init(); }
int bb_native_runtime_audio_audio_open(int a0,int a1,int a2,unsigned int a3,unsigned int a4,unsigned int a5) { return (int)audio_open((int32_t)a0,(int32_t)a1,(int32_t)a2,(uint32_t)a3,(uint32_t)a4,(uint32_t)a5); }
int bb_native_runtime_audio_audio_close(int a0) { return (int)audio_close((int32_t)a0); }
int bb_native_runtime_audio_audio_output(int a0,const void* a1) { return (int)audio_output((int32_t)a0,(const void *)a1); }
int bb_native_runtime_audio_audio_outputs(const void* a0,unsigned int a1) { if(a0)bb_arm_require_range("runtime_audio.audio_outputs.arg0",(uintptr_t)a0,sizeof(*((const OutputParam *)0)),0);return (int)audio_outputs((const OutputParam *)a0,(uint32_t)a1); }
int bb_native_runtime_audio_audio_volume(int a0,int a1,const void* a2) { if(a2)bb_arm_require_range("runtime_audio.audio_volume.arg2",(uintptr_t)a2,sizeof(*((const int32_t *)0)),0);return (int)audio_volume((int32_t)a0,(int32_t)a1,(const int32_t *)a2); }
int bb_native_runtime_audio_audio_state(int a0,void* a1) { if(a1)bb_arm_require_range("runtime_audio.audio_state.arg1",(uintptr_t)a1,sizeof(*((PortState *)0)),1);return (int)audio_state((int32_t)a0,(PortState *)a1); }
int bb_native_runtime_audio_audio_last_time(int a0,void* a1) { if(a1)bb_arm_require_range("runtime_audio.audio_last_time.arg1",(uintptr_t)a1,sizeof(*((uint64_t *)0)),1);return (int)audio_last_time((int32_t)a0,(uint64_t *)a1); }
void bb_arm_register_runtime_audio(void) { bb_arm_add_native("audio_init",(void*)audio_init,(void*)bb_native_runtime_audio_audio_init,"i"); bb_arm_add_native("audio_open",(void*)audio_open,(void*)bb_native_runtime_audio_audio_open,"iiiiIII"); bb_arm_add_native("audio_close",(void*)audio_close,(void*)bb_native_runtime_audio_audio_close,"ii"); bb_arm_add_native("audio_output",(void*)audio_output,(void*)bb_native_runtime_audio_audio_output,"iiR"); bb_arm_add_native("audio_outputs",(void*)audio_outputs,(void*)bb_native_runtime_audio_audio_outputs,"iRI"); bb_arm_add_native("audio_volume",(void*)audio_volume,(void*)bb_native_runtime_audio_audio_volume,"iiiR"); bb_arm_add_native("audio_state",(void*)audio_state,(void*)bb_native_runtime_audio_audio_state,"iiP"); bb_arm_add_native("audio_last_time",(void*)audio_last_time,(void*)bb_native_runtime_audio_audio_last_time,"iiP"); }
