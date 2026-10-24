// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace BbQaNetDiag {
inline bool Enabled(){static const bool on=[] {auto*p=std::getenv("BB_ARM_NET_DIAG");return p&&!std::strcmp(p,"1");}();return on;}
// Fixed labels/statuses only: no account, URL, headers, body, token or new buffer.
inline void Route(bool thd,unsigned port,bool https,bool ss_info){
 if(!Enabled())return;
 static std::atomic<unsigned> records{0};if(records.fetch_add(1,std::memory_order_relaxed)>=32)return;
 std::fprintf(stderr,"ARM NET ROUTE host=%s port=%u https=%d server_info=%d\n",thd?"thehuntersdream.com":"other",port,int(https),int(ss_info));
}
inline void Response(const char* service,int status,int result_kind,size_t bytes){
 if(!Enabled())return;
 static std::atomic<unsigned> records{0};
 if(records.fetch_add(1,std::memory_order_relaxed)>=32)return;
 std::fprintf(stderr,"ARM NET RESPONSE service=%s http=%d result_kind=%d bytes=%zu\n",service,status,result_kind,bytes);
}
}
