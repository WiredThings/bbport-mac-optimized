/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production runtime memory API: partial release, retained imported chunks, aliases and reuse.
 * Uses 16 MiB physical data, no game or Metal device. */
#include "runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
void runtime_restart(void) { abort(); }
extern int runtime_memory_snapshot_v1(uint64_t*, uint64_t);
extern void runtime_memory_set_guest_chunk_allocator(int (*)(uint64_t,uint64_t));
extern void runtime_memory_set_guest_chunk_whole(int);
typedef int64_t (ABI *Size)(void);
typedef int32_t (ABI *Allocate)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t*);
typedef int32_t (ABI *Map)(void**,uint64_t,int,int,int64_t,uint64_t);
typedef int32_t (ABI *Release)(uint64_t,uint64_t);
static int imported(uint64_t phys,uint64_t size){(void)phys;(void)size;return -3;}
int main(void){
    runtime_memory_set_guest_chunk_whole(1);runtime_memory_set_guest_chunk_allocator(imported);
    uint64_t s[16];memset(s,0xA5,sizeof(s));assert(!runtime_memory_snapshot_v1(s,15));assert(s[0]==UINT64_C(0xA5A5A5A5A5A5A5A5));
    Allocate alloc=(Allocate)runtime_memory_resolve("rTXw65xmLIA#p#J");
    Map map=(Map)runtime_memory_resolve("L-Q3LEjIbgA#p#J");
    Release release=(Release)runtime_memory_resolve("MBuItvba6z8#p#J");
    Size size=(Size)runtime_memory_resolve("pO96TwzOm5E#p#J");
    const uint64_t n=16ul<<20,half=n/2;int64_t p;void* ptr=0;
    assert(alloc(0,size(),n,0,3,&p)==0);assert(map(&ptr,n,0x33,0,p,0)==0);
    memset(ptr,0x3C,n);
    assert(runtime_memory_snapshot_v1(s,16));assert(s[0]==n&&s[2]==n&&s[5]==n&&s[6]==n&&s[7]==0&&s[11]==n);
    assert(release(p,half)==0);
    assert(runtime_memory_snapshot_v1(s,16));assert(s[0]==half&&s[5]==n&&s[6]==half&&s[7]==half&&s[8]==1&&s[9]==half&&s[10]==half&&s[12]==0);
    assert(((unsigned char*)ptr)[half]==0x3C); // neighboring live range untouched
    void* again=0;int64_t reused;assert(alloc(0,size(),half,0,0,&reused)==0&&reused==p);
    assert(map(&again,half,3,0,reused,0)==0);assert(((unsigned char*)again)[0]==0&&((unsigned char*)again)[half-1]==0);
    assert(release(reused,n)==0);
    assert(runtime_memory_snapshot_v1(s,16));assert(s[0]==0&&s[5]==n&&s[6]==0&&s[7]==n&&s[12]==1&&s[9]==n+half);
    printf("PASS: partial release, neighboring allocation, zero-on-reuse and retained imported capacity\n");return 0;
}
