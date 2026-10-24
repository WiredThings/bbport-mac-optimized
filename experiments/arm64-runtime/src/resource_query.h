// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
namespace BbResourceQuery {
struct Span {uint64_t address{},size{};};
struct Request {unsigned sequence{},count{};std::array<Span,24> ranges{};};
inline bool Parse(std::string_view data,std::string_view nonce,Request& output){
 if(data.size()>4096||nonce.size()!=32||nonce.find_first_not_of("0123456789abcdef")!=nonce.npos)return false;
 const auto word=[&](){auto first=data.find_first_not_of(" \t\r\n");if(first==data.npos){data={};return std::string_view{};}data.remove_prefix(first);auto end=data.find_first_of(" \t\r\n");auto value=data.substr(0,end);data.remove_prefix(end==data.npos?data.size():end);return value;};
 const auto number=[&](uint64_t& value){auto token=word();if(token.empty())return false;auto result=std::from_chars(token.data(),token.data()+token.size(),value);return !token.empty()&&result.ec==std::errc{}&&result.ptr==token.data()+token.size();};
 if(word()!="ARMRESOURCE1"||word()!=nonce)return false;
 Request request;uint64_t sequence{},count{};
 if(!number(sequence)||!sequence||sequence>32||!number(count)||!count||count>request.ranges.size())return false;
 request.sequence=sequence;request.count=count;
 for(unsigned i=0;i<count;++i){auto& r=request.ranges[i];if(!number(r.address)||!number(r.size)||!r.address||r.address>=(uint64_t(1)<<40)||!r.size||r.size>(uint64_t(1)<<32)||r.size>(uint64_t(1)<<40)-r.address)return false;}
 if(!word().empty())return false;output=request;return true;
}
}
