// Generated from Clang AST.
#include <stdbool.h>
#include "runtime_exit_codes.h"
 #include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/bb_runtime/src/runtime_time.c"
int bb_native_runtime_time_guest_gettimeofday(void* a0,void* a1) { if(a0)bb_arm_require_range("runtime_time.guest_gettimeofday.arg0",(uintptr_t)a0,sizeof(*((GuestTimeval *)0)),1);if(a1)bb_arm_require_range("runtime_time.guest_gettimeofday.arg1",(uintptr_t)a1,sizeof(*((GuestTimezone *)0)),1);return (int)guest_gettimeofday((GuestTimeval *)a0,(GuestTimezone *)a1); }
void bb_arm_register_runtime_time(void) { bb_arm_add_native("guest_gettimeofday",(void*)guest_gettimeofday,(void*)bb_native_runtime_time_guest_gettimeofday,"iPP"); }
