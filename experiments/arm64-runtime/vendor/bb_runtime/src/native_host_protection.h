/* Native Darwin ARM64: preserve the 4 KiB GPU watch vocabulary while applying
 * the intersection of its permissions to each real 16 KiB host page.
 * Included after the runtime VMA/trap definitions; runtime lock, then trap_mutex.
 * The sparse byte table costs one resident byte per touched 4 KiB guest page.
 */
#if defined(__APPLE__) && defined(__aarch64__)
#define BB_NATIVE_HOST_PROTECTION 1
#include <mach/mach.h>
#include <mach/mach_vm.h>
static uint8_t *native_gpu_deny;
static void native_protect_fail(uintptr_t address,uint64_t size) {
    fprintf(stderr,"ARM host-page protection failed: %#llx+%#llx errno=%d\n",
            (unsigned long long)address,(unsigned long long)size,errno);_Exit(79);
}
static void native_protect_commit(uintptr_t address,uint64_t size,int prot) {
    if(size && mprotect((void*)address,size,prot))native_protect_fail(address,size);
}
static unsigned native_page_denial(uintptr_t page) {
    const unsigned reason=trap_reasons ? trap_reasons[page>>12] : 0;
    return (native_gpu_deny ? native_gpu_deny[page>>12] : 0) |
           (reason ? (reason&TRAP_NO_ACCESS ? 3 : 2) : 0);
}
static void native_protect_apply(uintptr_t address,uintptr_t end,int override_prot) {
    const uintptr_t first=address&~(uintptr_t)(PAGE-1),last=align_up(end,PAGE);
    uintptr_t run=0,run_end=0;int run_prot=0;
    for(uintptr_t page=first;page<last;page+=PAGE) {
        const size_t i=vma_index(page);
        if(i>=vma_count || vmas[i].kind==KIND_RESERVED || vmas[i].start>page || vmas[i].end<page+PAGE) {
            native_protect_commit(run,run_end-run,run_prot);run=run_end=0;continue;
        }
        unsigned deny=0;for(unsigned j=0;j<PAGE/4096;++j)deny|=native_page_denial(page+j*4096);
        int prot=override_prot>=0 ? override_prot : host_prot(vmas[i].prot);
        if(deny&1)prot&=~PROT_READ;if(deny&2)prot&=~PROT_WRITE;
        if(run && prot!=run_prot){native_protect_commit(run,run_end-run,run_prot);run=0;}
        if(!run){run=page;run_prot=prot;}run_end=page+PAGE;
    }
    native_protect_commit(run,run_end-run,run_prot);
}
/* Resolve every watch responsible for an aggregate host-page fault. Only mapped,
 * guest-permitted accesses qualify; the backend keeps each owner's existing
 * invalidate/readback rules. Actual fault address is processed first.
 */
unsigned runtime_memory_host_fault_pages(uintptr_t address,int write,uintptr_t* pages,unsigned capacity) {
    if(!pages || capacity<PAGE/4096 || address>=TRAP_LIMIT)return 0;
    read_lock();pthread_mutex_lock(&trap_mutex);
    const size_t i=vma_index(address);unsigned count=0;
    if(i<vma_count && vmas[i].kind!=KIND_RESERVED && vmas[i].start<=address &&
       (host_prot(vmas[i].prot)&(write?PROT_WRITE:PROT_READ))) {
        const uintptr_t first=address&~(uintptr_t)(PAGE-1),actual=address&~(uintptr_t)4095;
        if(native_page_denial(actual)&(write?2:1))pages[count++]=address;
        for(uintptr_t p=first;p<first+PAGE;p+=4096)
            if(p!=actual && (native_page_denial(p)&(write?2:1)))pages[count++]=p;
    }
    pthread_mutex_unlock(&trap_mutex);read_unlock();return count;
}

/* A fault may be delivered after another thread has already lifted its GPU
 * watch. Retrying is safe only if BOTH the current guest policy/owners and the
 * actual Darwin VM mapping permit this access. Never changes permissions.
 * The caller separately restricts this to data-abort translation/permission
 * faults: instruction, alignment and external-abort faults are not stale GPU
 * watch faults. Kernel region objects are released on every path.
 */
int runtime_memory_host_access_now_allowed(uintptr_t address,int write) {
    if(address>=TRAP_LIMIT)return 0;
    read_lock();pthread_mutex_lock(&trap_mutex);
    const size_t i=vma_index(address);int allowed=0;
    const int needed=write?PROT_WRITE:PROT_READ;
    if(i<vma_count && vmas[i].kind!=KIND_RESERVED && vmas[i].start<=address &&
       (host_prot(vmas[i].prot)&needed)) {
        const uintptr_t first=address&~(uintptr_t)(PAGE-1);unsigned deny=0;
        for(unsigned j=0;j<PAGE/4096;++j)deny|=native_page_denial(first+j*4096);
        if(!(deny&(write?2:1))) {
            mach_vm_address_t at=address;mach_vm_size_t size=0;
            vm_region_basic_info_data_64_t info={0};mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=0;
            kern_return_t result=mach_vm_region(mach_task_self(),&at,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
            if(object)mach_port_deallocate(mach_task_self(),object);
            allowed=result==KERN_SUCCESS && at<=address && size>address-at && (info.protection&needed);
        }
    }
    pthread_mutex_unlock(&trap_mutex);read_unlock();return allowed;
}
#endif
