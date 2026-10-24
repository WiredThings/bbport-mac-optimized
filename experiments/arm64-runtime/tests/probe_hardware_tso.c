/* Isolated capability probe only. Never changes FEX or game state. */
#include <mach/mach.h>
#include <mach/mach_traps.h>
#include <os/arch/arm64.h>
#include <stdio.h>
int main(void){
 int supported=os_cross_arch_is_supported(OS_CROSS_ARCH_X86_64);
 kern_return_t enable=thread_set_x86_64_compat(1);
 kern_return_t disable=enable==KERN_SUCCESS?thread_set_x86_64_compat(0):KERN_SUCCESS;
 printf("kernel_cross_arch_supported=%d enable=%d disable=%d\n",supported,enable,disable);
 return enable==KERN_SUCCESS&&disable!=KERN_SUCCESS?2:0;
}
