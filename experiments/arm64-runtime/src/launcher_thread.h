// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <pthread.h>
#include <cstddef>
namespace BbLauncherThread {
// Match the preserved macOS loader. This is virtual stack reservation; it does
// not precommit 64 MiB, and does not change the separate guest stack budget.
inline constexpr std::size_t StackBytes=std::size_t(64)<<20;
inline int Start(pthread_t* thread,void* (*entry)(void*),void* args) {
 pthread_attr_t attr;
 int result=pthread_attr_init(&attr);
 if(result)return result;
 result=pthread_attr_setstacksize(&attr,StackBytes);
 if(!result)result=pthread_create(thread,&attr,entry,args);
 pthread_attr_destroy(&attr);
 return result;
}
}
