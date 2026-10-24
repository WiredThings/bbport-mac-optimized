// Generated from Clang AST.
#include <stdbool.h>
#include "runtime_exit_codes.h"
 #include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/bb_runtime/src/runtime_sema.c"
int bb_native_runtime_sema_sem_create(void* a0,const void* a1,unsigned int a2,int a3,int a4,const void* a5) { if(a0)bb_arm_require_range("runtime_sema.sem_create.arg0",(uintptr_t)a0,sizeof(*((uint32_t *)0)),1);if(a1)bb_arm_require_string("runtime_sema.sem_create.arg1",(const char*)a1,4096);return (int)sem_create((uint32_t *)a0,(const char *)a1,(uint32_t)a2,(int32_t)a3,(int32_t)a4,(const void *)a5); }
int bb_native_runtime_sema_sem_wait(unsigned int a0,int a1,void* a2) { if(a2)bb_arm_require_range("runtime_sema.sem_wait.arg2",(uintptr_t)a2,sizeof(*((uint32_t *)0)),1);return (int)sem_wait((uint32_t)a0,(int32_t)a1,(uint32_t *)a2); }
int bb_native_runtime_sema_sem_poll(unsigned int a0,int a1) { return (int)sem_poll((uint32_t)a0,(int32_t)a1); }
int bb_native_runtime_sema_sem_signal(unsigned int a0,int a1) { return (int)sem_signal((uint32_t)a0,(int32_t)a1); }
int bb_native_runtime_sema_sem_cancel(unsigned int a0,int a1,void* a2) { if(a2)bb_arm_require_range("runtime_sema.sem_cancel.arg2",(uintptr_t)a2,sizeof(*((int32_t *)0)),1);return (int)sem_cancel((uint32_t)a0,(int32_t)a1,(int32_t *)a2); }
int bb_native_runtime_sema_sem_delete(unsigned int a0) { return (int)sem_delete((uint32_t)a0); }
void bb_arm_register_runtime_sema(void) { bb_arm_add_native("sem_create",(void*)sem_create,(void*)bb_native_runtime_sema_sem_create,"iPRIiiR"); bb_arm_add_native("sem_wait",(void*)sem_wait,(void*)bb_native_runtime_sema_sem_wait,"iIiP"); bb_arm_add_native("sem_poll",(void*)sem_poll,(void*)bb_native_runtime_sema_sem_poll,"iIi"); bb_arm_add_native("sem_signal",(void*)sem_signal,(void*)bb_native_runtime_sema_sem_signal,"iIi"); bb_arm_add_native("sem_cancel",(void*)sem_cancel,(void*)bb_native_runtime_sema_sem_cancel,"iIiP"); bb_arm_add_native("sem_delete",(void*)sem_delete,(void*)bb_native_runtime_sema_sem_delete,"iI"); }
