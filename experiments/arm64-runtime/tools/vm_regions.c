#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
int main(void){mach_vm_address_t a=0;while(a<(1ull<<40)){mach_vm_size_t n=0;vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object;int r=mach_vm_region(mach_task_self(),&a,&n,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);if(r)break;if(object)mach_port_deallocate(mach_task_self(),object);if(n>=(16ull<<20))printf("%#llx-%#llx %llu MiB prot=%d max=%d\n",a,a+n,n>>20,info.protection,info.max_protection);if(a+n<a)break;a+=n;}return 0;}
