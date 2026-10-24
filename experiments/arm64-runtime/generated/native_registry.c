#include "arm_bridge.h"
void bb_arm_register_runtime(void);
void bb_arm_register_runtime_thread(void);
void bb_arm_register_runtime_kernel(void);
void bb_arm_register_runtime_memory(void);
void bb_arm_register_runtime_mutex(void);
void bb_arm_register_runtime_sema(void);
void bb_arm_register_runtime_rwlock(void);
void bb_arm_register_runtime_time(void);
void bb_arm_register_runtime_content(void);
void bb_arm_register_runtime_file(void);
void bb_arm_register_runtime_services(void);
void bb_arm_register_runtime_savedata(void);
void bb_arm_register_runtime_rtc(void);
void bb_arm_register_runtime_ajm(void);
void bb_arm_register_runtime_audio(void);
void bb_arm_register_runtime_pad(void);
void bb_arm_runtime_register(void) {bb_arm_register_runtime();bb_arm_register_runtime_thread();bb_arm_register_runtime_kernel();bb_arm_register_runtime_memory();bb_arm_register_runtime_mutex();bb_arm_register_runtime_sema();bb_arm_register_runtime_rwlock();bb_arm_register_runtime_time();bb_arm_register_runtime_content();bb_arm_register_runtime_file();bb_arm_register_runtime_services();bb_arm_register_runtime_savedata();bb_arm_register_runtime_rtc();bb_arm_register_runtime_ajm();bb_arm_register_runtime_audio();bb_arm_register_runtime_pad();}
