// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int32_t bb_native_worker_spawn(void**,void*(*)(void*),void*,uint64_t,const char*);
int32_t bb_native_worker_join(void*,void**);
__attribute__((noreturn)) void bb_native_worker_exit(void*);
size_t bb_native_worker_count(void);
#ifdef __cplusplus
}
#endif
