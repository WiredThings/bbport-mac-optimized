// SPDX-License-Identifier: GPL-2.0-or-later
// Missing subsystems remain unresolved. These are loader plumbing, not success HLE stubs.
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define ABSENT(name) uintptr_t runtime_##name##_resolve(const char*n){(void)n;return 0;} void runtime_##name##_report(void){}
#ifndef BB_WITH_NATIVE_GPU
uintptr_t bbgpu_resolve(const char* name){(void)name;return 0;}
int bbgpu_text_input_begin(const char*a,const char*b){(void)a;(void)b;return 0;}
int bbgpu_text_input_poll(char*a,uint64_t b){(void)a;(void)b;return -1;}
#endif
uint64_t runtime_guest_fault_test_hook(void){return 0;}

#ifndef BB_WITH_NATIVE_GPU
const bool *bbgpu_keyboard_state(void){return NULL;}
int bbgpu_overlay_captures_input(void){return 0;}

#endif
