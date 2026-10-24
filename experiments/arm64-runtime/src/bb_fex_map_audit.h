// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <ctime>
#include <unistd.h>
namespace BbFexMap {
using U=uint64_t;enum Kind:unsigned{Jit=1,Lookup=2,Dispatcher=3};
struct Counters{std::atomic<U>issued{},failed{};std::mutex output;};
inline Counters& Get(){static Counters c;return c;}
inline bool Enabled(){static const bool on=[] {const char*p=std::getenv("BB_FEX_MAPPING_AUDIT");return p&&p[0]=='1';}();return on;}
constexpr U Limit=16384;
// Diagnostics own no mapping references, change no allocation/free semantics, and
// preserve errno. Output is bounded metadata only, not game memory contents.
inline void Event(char action,Kind kind,const void*pointer,size_t bytes){
 if(!Enabled())return;const int saved=errno;auto&c=Get();std::lock_guard lock(c.output);U seq=c.issued.fetch_add(1,std::memory_order_relaxed)+1;
 if(seq>Limit+1){errno=saved;return;}
 char line[256];int length{};timespec t{};const U address=reinterpret_cast<uintptr_t>(pointer);
 if(seq==Limit+1)length=std::snprintf(line,sizeof(line),"\nFEX_MAP_OVERFLOW pid=%u seq=%llu\n",unsigned(getpid()),(unsigned long long)seq);
 else if((action!='A'&&action!='F')||kind<Jit||kind>Dispatcher||!address||!bytes||bytes>UINTPTR_MAX-address||clock_gettime(CLOCK_MONOTONIC,&t)){
  ++c.failed;length=std::snprintf(line,sizeof(line),"\nFEX_MAP_INVALID pid=%u seq=%llu\n",unsigned(getpid()),(unsigned long long)seq);
 }else length=std::snprintf(line,sizeof(line),"\nFEX_MAP pid=%u seq=%llu mono=%llu action=%c kind=%u address=%#llx bytes=%llu\n",unsigned(getpid()),(unsigned long long)seq,(unsigned long long)(U(t.tv_sec)*1000000000+U(t.tv_nsec)),action,unsigned(kind),(unsigned long long)address,(unsigned long long)bytes);
 if(length<=0||size_t(length)>=sizeof(line))++c.failed;
 else {size_t done=0;while(done<size_t(length)){const auto n=write(STDERR_FILENO,line+done,size_t(length)-done);if(n>0)done+=size_t(n);else if(n<0&&errno==EINTR)continue;else{++c.failed;break;}}}
 errno=saved;
}
inline void Checkpoint(){
 if(!Enabled())return;const int saved=errno;auto&c=Get();std::lock_guard lock(c.output);timespec t{};
 if(clock_gettime(CLOCK_MONOTONIC,&t)){++c.failed;errno=saved;return;}
 char line[192];const int n=std::snprintf(line,sizeof(line),"\nFEX_MAP_CHECKPOINT pid=%u mono=%llu issued=%llu failed=%llu\n",unsigned(getpid()),(unsigned long long)(U(t.tv_sec)*1000000000+U(t.tv_nsec)),(unsigned long long)c.issued.load(),(unsigned long long)c.failed.load());
 if(n<=0||size_t(n)>=sizeof(line)||write(STDERR_FILENO,line,size_t(n))!=n)++c.failed;errno=saved;
}

}
