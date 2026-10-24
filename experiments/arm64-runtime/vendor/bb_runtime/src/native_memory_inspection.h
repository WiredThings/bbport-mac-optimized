/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 uintptr_t begin,end; uint64_t physical,generation;
 int kind,prot,type,private_mem,host_owned;
} RuntimeMappingSnapshot;
typedef struct { uintptr_t address;uint64_t size,physical; } RuntimeMappingAlias;
/* Read-only metadata. No mmap, allocation, permission changes or GPU synchronization. */
int runtime_memory_inspect_mapping(uintptr_t,RuntimeMappingSnapshot*);
/* Requires the entire source span in one shared direct VMA. Reports truncation;
 * a missing/partial/private mapping is not proof of an absent alias. */
size_t runtime_memory_inspect_aliases(uintptr_t,uint64_t,RuntimeMappingAlias*,size_t,int*,uint64_t*);
#ifdef __cplusplus
}
#endif
