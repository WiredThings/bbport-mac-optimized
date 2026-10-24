// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <cctype>
namespace Metal {
inline constexpr unsigned CopyWatchSlot=30;
inline constexpr unsigned CopyWatchRootSlot=29;
struct alignas(16) CopyWatchData {
    std::uint64_t guest{};
    std::uint32_t bytes{},shift{},plane_offset{},two_level{};
    std::uint32_t flags{},reads{},writes{},current_hits{},guest_hits{},overflow{};
    std::uint32_t mapping{},pad[3]{};
};
static_assert(sizeof(CopyWatchData)==64);
struct CopyWatchRewrite {
    std::string source,error;
    unsigned reads{},writes{},physical_reads{},physical_writes{};
    bool ok{};
};
// A diagnostic for the known plain copy shader only. Each observed expression
// returns precisely its original pointer; arithmetic, values and conditions stay
// intact. Counters describe evaluated source-level accesses, not hardware loads.
inline CopyWatchRewrite RewriteCopyWatch(std::string_view input) {
    CopyWatchRewrite out;
    auto fail=[&](const char* why){out.error=why;return out;};
    if(input.size()>128*1024 || input.find("[[buffer(30)]]")!=input.npos ||
       input.find("[[buffer(29)]]")!=input.npos || input.find("thread_index_in_threadgroup")!=input.npos ||
       input.find("atomic_")!=input.npos)return fail("unsupported source/binding");
    auto kernel=input.find("kernel void ");
    if(kernel==input.npos || input.find("kernel void ",kernel+1)!=input.npos)return fail("expected one kernel");
    auto open=input.find('{',kernel);
    if(open==input.npos)return fail("missing body");
    auto close=input.rfind('}');if(close==input.npos || close<=open)return fail("missing body");
    struct Parser {
        CopyWatchRewrite& out;bool valid=true;
        static size_t end(std::string_view s,size_t pos,char l,char r) {
            unsigned depth=0;
            for(size_t n=pos;n<s.size();++n){if(s[n]==l)++depth;if(s[n]==r && !--depth)return n;}
            return s.npos;
        }
        bool write(std::string_view s,size_t pos) {
            while(pos<s.size() && std::isspace(static_cast<unsigned char>(s[pos])))++pos;
            if(pos==s.size())return false;
            if(s[pos]=='=' && (pos+1==s.size() || s[pos+1]!='='))return true;
            if(s.substr(pos,2)=="++" || s.substr(pos,2)=="--" ||
               (pos+1<s.size() && s[pos+1]=='=' && s[pos]!='=' && s[pos]!='<' && s[pos]!='>' && s[pos]!='!'))valid=false;
            return false;
        }
        std::string body(std::string_view s,unsigned depth=0) {
            if(depth>32){valid=false;return {};}
            std::string result;
            for(size_t n=0;n<s.size();) {
                constexpr std::string_view cast="*(reinterpret_cast<device ";
                if(s.substr(n,cast.size())==cast) {
                    auto type_end=s.find("*>(",n+cast.size());
                    if(type_end==s.npos){valid=false;return {};}
                    auto arg=type_end+2;auto stop=end(s,arg,'(',')');
                    if(stop==s.npos || stop+1>=s.size() || s[stop+1]!=')'){valid=false;return {};}
                    const auto type=s.substr(n+cast.size(),type_end-n-cast.size());
                    if(type.empty() || type.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=type.npos){valid=false;return {};}
                    const bool w=write(s,stop+2);out.physical_reads+=!w;out.physical_writes+=w;
                    out.reads+=!w;out.writes+=w;
                    result+="*(bb_copy_ptr(reinterpret_cast<device "+std::string(type)+"*>("+
                        body(s.substr(arg+1,stop-arg-1),depth+1)+"), bb_copy_watch, bb_copy_ranges, "+(w?"true":"false")+"))";
                    n=stop+2;continue;
                }
                if(std::isalpha(static_cast<unsigned char>(s[n])) || s[n]=='_') {
                    size_t name=n+1;
                    while(name<s.size() && (std::isalnum(static_cast<unsigned char>(s[name])) || s[name]=='_'))++name;
                    if(s.substr(name,6)==".data[") {
                        const auto stop=end(s,name+5,'[',']');
                        if(stop==s.npos){valid=false;return {};}
                        bool w=write(s,stop+1);out.reads+=!w;out.writes+=w;
                        result+="*(bb_copy_ptr("+std::string(s.substr(n,name-n))+".data + ("+
                            body(s.substr(name+6,stop-name-6),depth+1)+"), bb_copy_watch, bb_copy_ranges, "+(w?"true":"false")+"))";
                        n=stop+1;continue;
                    }
                    result+=s.substr(n,name-n);n=name;continue;
                }
                result+=s[n++];
            }
            return result;
        }
    } parser{out};
    const auto original=input.substr(open+1,close-open-1);
    const auto rewritten=parser.body(original);
    // Only the recognized resource syntax is allowed to carry memory accesses.
    size_t casts=0;for(size_t at=0;(at=original.find("reinterpret_cast<device ",at))!=original.npos;++at)++casts;
    if(!parser.valid || casts!=out.physical_reads+out.physical_writes || !out.reads || !out.writes)
        return fail("uncovered physical access");
    static constexpr const char* helper=R"MSL(
struct BbCopyWatch { ulong guest; uint bytes,shift,plane_offset,two_level;
    atomic_uint flags,reads,writes,current_hits,guest_hits,overflow,mapping;uint pad[3]; };
struct BbCopyRanges {ulong current,guest;uint bytes;};
inline BbCopyRanges bb_copy_ranges_for(const device ulong* root,device BbCopyWatch& d) {
    ulong page=d.guest>>d.shift;ulong offset=d.guest&((1ul<<d.shift)-1ul);
    ulong a=0,b=0;
    if(d.two_level) {
        ulong la=root[page>>12ul],lb=root[(page>>12ul)+d.plane_offset];
        if(la)a=reinterpret_cast<const device ulong*>(la)[page&4095ul];
        if(lb)b=reinterpret_cast<const device ulong*>(lb)[page&4095ul];
    } else {a=root[page];b=root[page+d.plane_offset];}
    return BbCopyRanges{a?a+offset:0,b?b+offset:0,d.bytes};
}
inline bool bb_copy_overlap(ulong a,uint bytes,ulong b,uint count) {
    return b && (a<=b?b-a<ulong(bytes):a-b<ulong(count));
}
inline void bb_copy_count(device atomic_uint& value,device BbCopyWatch& d) {
    if(atomic_fetch_add_explicit(&value,1u,memory_order_relaxed)==0xffffffffu)
        atomic_fetch_or_explicit(&d.overflow,1u,memory_order_relaxed);
}
template<typename T> inline device T* bb_copy_ptr(device T* p,device BbCopyWatch& d,
    thread const BbCopyRanges& r,bool write) {
    ulong a=reinterpret_cast<ulong>(p);
    bool c=bb_copy_overlap(a,uint(sizeof(T)),r.current,r.bytes);
    bool g=bb_copy_overlap(a,uint(sizeof(T)),r.guest,r.bytes);
    if(c||g) {
        atomic_fetch_or_explicit(&d.flags,write?2u:1u,memory_order_relaxed);
        if(write)bb_copy_count(d.writes,d);else bb_copy_count(d.reads,d);
        if(c)bb_copy_count(d.current_hits,d);if(g)bb_copy_count(d.guest_hits,d);
    }
    return p;
}
)MSL";
    std::string prefix(input.substr(0,kernel));
    std::string signature(input.substr(kernel,open-kernel));
    auto paren=signature.rfind(')');if(paren==signature.npos)return fail("missing signature");
    signature.insert(paren,", const device ulong* bb_copy_root [[buffer(29)]], device BbCopyWatch& bb_copy_watch [[buffer(30)]], uint bb_copy_lane [[thread_index_in_threadgroup]]");
    out.source=prefix+helper+signature+"{\nBbCopyRanges bb_copy_ranges=bb_copy_ranges_for(bb_copy_root,bb_copy_watch);\nif(bb_copy_lane==0u)atomic_fetch_or_explicit(&bb_copy_watch.mapping,(bb_copy_ranges.current?1u:0u)|(bb_copy_ranges.guest?2u:0u),memory_order_relaxed);\n"+
        rewritten+"}"+std::string(input.substr(close+1));
    out.ok=true;return out;
}
}
