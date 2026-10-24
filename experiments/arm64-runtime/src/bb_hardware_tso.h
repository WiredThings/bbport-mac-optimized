#include "runtime_exit_codes.h"
// SPDX-License-Identifier: MIT
// Admission policy adapted from HyperBridge 0055 at ac49813a48908d7a4a94f0e5583fedbe8fbc71e0.
// Preserve software ordering unless the immutable hardware-only policy is admitted.
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
namespace BbTSO {
using EnableFn=int(*)(uint32_t);
class Policy {
 pthread_key_t key{};bool key_valid{};EnableFn enable{};
 std::atomic<unsigned> attempts{0},successes{0},failures{0};unsigned mode{};
 bool Enable(){const auto n=attempts.fetch_add(1)+1;int saved=errno;int rc=enable?enable(1):46;
  if(rc==0)++successes;else ++failures;
  if(n<=64||rc){char b[160];uint64_t tid{};pthread_threadid_np(nullptr,&tid);int l=snprintf(b,sizeof(b),"ARM TSO admission tid=%llu rc=%d attempt=%u\n",(unsigned long long)tid,rc,n);if(l>0&&size_t(l)<sizeof(b))write(2,b,l);}
  errno=saved;return rc==0;
 }
public:
 explicit Policy(unsigned requested,EnableFn fn):enable(fn){
  if((requested==1||requested==2)&&pthread_key_create(&key,nullptr)==0){key_valid=true;
   if(Enable()&&pthread_setspecific(key,reinterpret_cast<void*>(1))==0)mode=requested;}
 }
 unsigned Mode()const{return mode;}
 // Called only at established C++ JIT entry points, never first initialized by a signal handler.
 void Admit(){if(!mode||pthread_getspecific(key))return;
  if(!Enable()||pthread_setspecific(key,reinterpret_cast<void*>(1))){
   static constexpr char msg[]="ARM TSO FATAL late thread admission failed; refusing shared JIT\n";write(2,msg,sizeof(msg)-1);_exit(BB_ARM_EXIT_TSO_ADMISSION);}}
 unsigned Attempts()const{return attempts.load();}
 ~Policy(){if(key_valid)pthread_key_delete(key);}
};
inline unsigned Requested(){const char* v=getenv("BB_ARM_HW_TSO");if(!v||!strcmp(v,"0"))return 0;if(!strcmp(v,"1"))return 1;if(!strcmp(v,"2"))return 2;return 0;}
inline Policy& Get(){static Policy p(Requested(),reinterpret_cast<EnableFn>(dlsym(RTLD_DEFAULT,"thread_set_x86_64_compat")));return p;}
inline unsigned Initialize(){auto& p=Get();fprintf(stderr,"ARM TSO decision requested=%u selected=%u software=%u immutable=1\n",Requested(),p.Mode(),p.Mode()!=2);return p.Mode();}
inline void Admit(){Get().Admit();}
}
