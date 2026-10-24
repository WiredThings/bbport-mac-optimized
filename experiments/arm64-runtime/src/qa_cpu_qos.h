// SPDX-License-Identifier: MIT
#pragma once
#include <pthread.h>
#include <sys/qos.h>
#include <errno.h>
#include <string.h>
// Shared by the C guest-thread bridge and C++ loader. No host callback pool changes.
enum BbQaCpuQoSMode { BB_QA_QOS_INHERIT=0,BB_QA_QOS_USER_INITIATED=1 };
static inline int bb_qa_cpu_qos_parse(const char* value,int* mode){
 if(!mode)return EINVAL;
 if(!value||!strcmp(value,"inherit")){*mode=BB_QA_QOS_INHERIT;return 0;}
 if(!strcmp(value,"user-initiated")){*mode=BB_QA_QOS_USER_INITIATED;return 0;}
 return EINVAL;
}
static inline int bb_qa_cpu_qos_apply_with(int mode,int(*setter)(qos_class_t,int)){
 // Default performs no scheduling API call and preserves original behavior.
 if(mode==BB_QA_QOS_INHERIT)return 0;
 if(mode!=BB_QA_QOS_USER_INITIATED||!setter)return EINVAL;
 return setter(QOS_CLASS_USER_INITIATED,0);
}
static inline int bb_qa_cpu_qos_apply(int mode){return bb_qa_cpu_qos_apply_with(mode,pthread_set_qos_class_self_np);}
