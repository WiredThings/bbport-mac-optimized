#pragma once
// Guest x86 bytes are data to the ARM host. Separate aliases let us append new
// immutable stubs without ever making an already-published guest page writable.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <variant>
namespace BbDarwinVeneer {
class Slab final {
public:
 using Remap=kern_return_t(*)(uintptr_t,size_t,uintptr_t*);
 using Protect=kern_return_t(*)(uintptr_t,size_t,bool,vm_prot_t);
 // Injection points cover partial allocation failures in the exact production
 // cleanup path; no game environment option changes these functions.
 static kern_return_t NativeRemap(uintptr_t source,size_t size,uintptr_t* target){
  mach_vm_address_t alias=0;vm_prot_t current=0,maximum=0;
  auto status=mach_vm_remap(mach_task_self(),&alias,size,0,VM_FLAGS_ANYWHERE,
      mach_task_self(),source,false,&current,&maximum,VM_INHERIT_NONE);
  if(status==KERN_SUCCESS)*target=alias;
  return status;
 }
 static kern_return_t NativeProtect(uintptr_t address,size_t size,bool maximum,vm_prot_t protection){
  return mach_vm_protect(mach_task_self(),address,size,maximum,protection);
 }
 static std::variant<std::unique_ptr<Slab>,int> Create(size_t size,Remap remap=NativeRemap,Protect protect=NativeProtect){
  const auto page=sysconf(_SC_PAGESIZE);
  if(page<=0||size!=static_cast<size_t>(page)||size!=16384||!remap||!protect)return EINVAL;
  auto slab=std::unique_ptr<Slab>(new Slab);slab->size_=size;
  slab->writer_=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
  if(slab->writer_==MAP_FAILED)return errno?errno:ENOMEM;
  uintptr_t alias=0;
  if(remap(reinterpret_cast<uintptr_t>(slab->writer_),size,&alias)!=KERN_SUCCESS)return ENOMEM;
  slab->reader_=reinterpret_cast<void*>(alias);
  if(!alias||slab->reader_==slab->writer_)return EIO;
  // Lower the guest mapping's maximum rights too. The writer's separate VM
  // entry stays RW; neither alias is ever native executable.
  if(protect(alias,size,true,VM_PROT_READ)!=KERN_SUCCESS||
     protect(alias,size,false,VM_PROT_READ)!=KERN_SUCCESS)return EACCES;
  return slab;
 }
 ~Slab(){
  if(reader_&&reader_!=MAP_FAILED&&reader_!=writer_&&munmap(reader_,size_))std::abort();
  if(writer_!=MAP_FAILED&&munmap(writer_,size_))std::abort();
 }
 Slab(const Slab&)=delete;Slab& operator=(const Slab&)=delete;
 unsigned char* Writer()const{return static_cast<unsigned char*>(writer_);}
 const unsigned char* Reader()const{return static_cast<const unsigned char*>(reader_);}
 size_t Size()const{return size_;}
private:
 Slab()=default;
 void* writer_=MAP_FAILED;void* reader_=nullptr;size_t size_=0;
};
}
#endif
