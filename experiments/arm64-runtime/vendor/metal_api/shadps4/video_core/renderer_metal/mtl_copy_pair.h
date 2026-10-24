// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cctype>
namespace Metal {
struct CopyPairRewrite {std::string source,error;bool ok=false;unsigned pairs=0;};
inline CopyPairRewrite RewriteCopyPair(std::string_view input) {
    CopyPairRewrite out;
    const auto fail=[&](const char* s){out.error=s;return out;};
    const auto trim=[](std::string_view s){while(!s.empty()&&std::isspace((unsigned char)s.front()))s.remove_prefix(1);while(!s.empty()&&std::isspace((unsigned char)s.back()))s.remove_suffix(1);return s;};
    const auto ident=[](std::string_view s){if(s.empty()||(!std::isalpha((unsigned char)s[0])&&s[0]!='_'))return false;for(char c:s)if(!std::isalnum((unsigned char)c)&&c!='_')return false;return true;};
    const auto pure=[&](std::string_view s){
        if(s.find("++")!=s.npos||s.find("--")!=s.npos||s.find("=")!=s.npos) {
            for(size_t n=0;n<s.size();++n)if(s[n]=='=' && (n==0||(s[n-1]!='!'&&s[n-1]!='<'&&s[n-1]!='>'&&s[n-1]!='=')) && (n+1==s.size()||s[n+1]!='='))return false;
            if(s.find("++")!=s.npos||s.find("--")!=s.npos)return false;
        }
        for(size_t n=0;n<s.size();) {
            if(!std::isalpha((unsigned char)s[n])&&s[n]!='_'){++n;continue;}
            const size_t first=n;while(n<s.size()&&(std::isalnum((unsigned char)s[n])||s[n]=='_'))++n;
            const auto name=s.substr(first,n-first);size_t at=n;while(at<s.size()&&std::isspace((unsigned char)s[at]))++at;
            if(at<s.size()&&s[at]=='(' && name!="bool"&&name!="uint"&&name!="ulong"&&name!="float")return false;
            if(at<s.size()&&s[at]=='<' && name!="as_type" && name!="reinterpret_cast") {
                auto end=s.find('>',at+1);if(end!=s.npos){++end;while(end<s.size()&&std::isspace((unsigned char)s[end]))++end;if(end<s.size()&&s[end]=='(')return false;}
            }
        }
        return true;
    };
    if(input.size()>128*1024||input.find("bb_copy_pair_")!=input.npos||input.find("bb_copy_ptr")!=input.npos||input.find("atomic_")!=input.npos||input.find("volatile")!=input.npos)return fail("unsupported source");
    const auto kernel=input.find("kernel void ");
    if(kernel==input.npos||input.find("kernel void ",kernel+1)!=input.npos)return fail("one kernel required");
    const auto open=input.find('{',kernel),close=input.rfind('}');
    if(open==input.npos||close==input.npos||close<=open)return fail("kernel body");
    const auto body=input.substr(open+1,close-open-1);
    for(auto word:{"//","/*","\"","'","while","for (","for(","do "})if(body.find(word)!=body.npos)return fail("unsupported body");
    struct Store{size_t begin,end,block;std::string_view type,address,value;};std::vector<Store> stores;
    constexpr std::string_view cast="*(reinterpret_cast<device ";
    for(size_t at=open+1;(at=input.find(cast,at))<close;) {
        const auto begin=at;auto type_end=input.find("*>(",at+cast.size());if(type_end==input.npos||type_end>=close)return fail("cast syntax");
        auto type=trim(input.substr(at+cast.size(),type_end-at-cast.size()));
        const size_t arg=type_end+2;size_t stop=arg;unsigned depth=0;
        for(;stop<close;++stop){if(input[stop]=='(')++depth;if(input[stop]==')'&&!--depth)break;}
        if(stop+1>=close||input[stop+1]!=')')return fail("cast parentheses");
        size_t next=stop+2;while(next<close&&std::isspace((unsigned char)input[next]))++next;
        if(next>=close||input[next]!='='||(next+1<close&&input[next+1]=='=')){at=begin+cast.size();continue;}
        const size_t semi=input.find(';',next);if(semi==input.npos||semi>=close)return fail("store terminator");
        const auto value=trim(input.substr(next+1,semi-next-1));
        if((type!="float"&&type!="uint")||!ident(value))return fail("store type/value");
        std::vector<size_t> blocks;
        for(size_t n=open;n<begin;++n){if(input[n]=='{')blocks.push_back(n);else if(input[n]=='}'){if(blocks.empty())return fail("block");blocks.pop_back();}}
        if(blocks.empty())return fail("store outside body");
        const auto address=input.substr(arg+1,stop-arg-1);if(!pure(address))return fail("impure address");
        stores.push_back({begin,semi+1,blocks.back(),type,address,value});at=semi+1;
    }
    if(stores.size()!=2)return fail("expected exactly two physical stores");
    const auto& a=stores[0];const auto& b=stores[1];
    if(a.block!=b.block||a.type!=b.type||a.value!=b.value)return fail("pair mismatch");
    auto between=trim(input.substr(a.end,b.begin-a.end));
    if(between.find(a.value)!=between.npos||between.find('{')!=between.npos||between.find('}')!=between.npos)return fail("interposed value/control flow");
    while(!between.empty()) {
        auto semi=between.find(';');if(semi==between.npos)return fail("interposed statement");
        auto stmt=trim(between.substr(0,semi));between=trim(between.substr(semi+1));
        const auto space=stmt.find(' '),eq=stmt.find('=');if(space==stmt.npos||eq==stmt.npos||eq<=space)return fail("interposed mutation");
        const auto type=stmt.substr(0,space);if(type!="bool"&&type!="uint"&&type!="ulong"&&type!="float")return fail("interposed type");
        if(!ident(trim(stmt.substr(space+1,eq-space-1)))||!pure(stmt.substr(eq+1)))return fail("interposed side effect");
    }
    const std::string type(a.type),value(a.value);
    const auto first="device "+type+"* const bb_copy_pair_current = reinterpret_cast<device "+type+"*>("+std::string(a.address)+");\n*bb_copy_pair_current = "+value+";";
    const auto second="device "+type+"* const bb_copy_pair_guest = reinterpret_cast<device "+type+"*>("+std::string(b.address)+");\nif (bb_copy_pair_guest != bb_copy_pair_current) *bb_copy_pair_guest = "+value+";";
    out.source=std::string(input.substr(0,a.begin))+first+std::string(input.substr(a.end,b.begin-a.end))+second+std::string(input.substr(b.end));out.ok=true;out.pairs=1;return out;
}
}
