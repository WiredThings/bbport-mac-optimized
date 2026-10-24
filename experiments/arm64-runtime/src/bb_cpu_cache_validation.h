#pragma once
// Structural validator for the frozen Darwin FXCC v2 layout. Does not authenticate
// executable code or replace the separate runtime/ABI/host/configuration identity.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>
namespace BbCacheValidation {
struct Policy { uint64_t image_bytes; std::span<const std::pair<uint64_t,uint64_t>> sections; std::span<const uint8_t,20> fex; };
struct Block { uint64_t guest,host; std::vector<uint64_t> pages; };
struct Reloc { uint64_t offset,rip; uint32_t kind,reg; };
struct Validated { size_t code_offset,code_bytes,relocation_offset; std::vector<Block> blocks;std::vector<Reloc> relocs; };
inline bool Inside(const Policy& p,uint64_t value){
 return std::any_of(p.sections.begin(),p.sections.end(),[&](auto r){return value>=r.first&&value<r.second;});
}
inline std::optional<uint64_t> DecodeMove(std::span<const std::byte> bytes,unsigned reg){
 if(bytes.size()!=16||reg>30||reg==18)return {};
 uint64_t value=0;bool initialized=false;constexpr uint64_t all=~uint64_t{};
 for(size_t i=0;i<16;i+=4){uint32_t op;memcpy(&op,bytes.data()+i,4);
  if(op==0xd503201f)continue;
  if((op&31)!=reg)return {};
  unsigned shift=((op>>21)&3)*16;uint64_t imm=(op>>5)&65535;
  switch(op&0xff800000){
   case 0xd2800000:if(initialized)return {};value=imm<<shift;initialized=true;break;
   case 0x92800000:if(initialized)return {};value=~(imm<<shift);initialized=true;break;
   case 0xf2800000:if(!initialized)return {};value=(value&~(uint64_t(65535)<<shift))|(imm<<shift);break;
   case 0xb2000000:{
    if(initialized||((op>>5)&31)!=31)return {};
    unsigned n=(op>>22)&1,r=(op>>16)&63,s=(op>>10)&63,encoded=(n<<6)|((~s)&63);
    if(!encoded)return {};unsigned len=0;while(encoded>>=1)++len;if(len<1)return {};
    unsigned size=1u<<len,levels=size-1,S=s&levels,R=r&levels;if(S==levels)return {};
    uint64_t mask=size==64?all:(uint64_t(1)<<size)-1,element=(uint64_t(1)<<(S+1))-1;
    element=R?((element>>R)|(element<<(size-R)))&mask:element;
    value=0;for(unsigned bit=0;bit<64;bit+=size)value|=element<<bit;initialized=true;break;
   }
   default:return {};
  }
 }
 return initialized?std::optional<uint64_t>(value):std::nullopt;
}
inline std::optional<Validated> Validate(std::span<const std::byte> s,const Policy& p){
 constexpr size_t MaxFile=16u<<20,MaxEdges=131072;
 if(s.size()<56||s.size()>MaxFile||!p.image_bytes||p.image_bytes>512u<<20||p.sections.empty()||p.sections.size()>131072)return {};
 uint64_t last_section=0;for(auto [lo,hi]:p.sections){if(lo>=hi||lo%4096||hi>p.image_bytes||lo<last_section)return {};last_section=hi;}
 auto u32=[&](size_t at){uint32_t x;memcpy(&x,s.data()+at,4);return x;};
 auto u64=[&](size_t at){uint64_t x;memcpy(&x,s.data()+at,8);return x;};
 if(memcmp(s.data(),"FXCC",4)||u32(4)!=2||memcmp(s.data()+8,p.fex.data(),20)||u32(44)||u64(48))return {};
 const auto blocks=u32(28),pages=u32(32),code=u32(36),relocs=u32(40);
 if(!blocks||blocks>65536||!pages||pages>32768||!code||code>16u<<20||code%4096||relocs>131072)return {};
 size_t at=56;auto read=[&](uint64_t& x){if(at>s.size()||s.size()-at<8)return false;x=u64(at);at+=8;return true;};
 Validated result{};result.blocks.reserve(std::min<size_t>(blocks,s.size()/24));
 std::map<uint64_t,std::set<uint64_t>> reverse;
 uint64_t last=0;size_t edges=0;
 for(uint32_t i=0;i<blocks;++i){Block b{};uint64_t n;
  if(!read(b.guest)||!read(b.host)||!read(n)||!Inside(p,b.guest)||(i&&b.guest<=last)||b.host%4||b.host>=code||!n||n>131072||n>MaxEdges-edges)return {};
  edges+=n;last=b.guest;uint64_t previous=0;bool own=false;
  for(uint64_t j=0;j<n;++j){uint64_t page;if(!read(page)||page%4096||!Inside(p,page)||(j&&page<=previous))return {};
   previous=page;b.pages.push_back(page);reverse[page].insert(b.guest);own|=page==(b.guest&~uint64_t(4095));}
  if(!own)return {};result.blocks.push_back(std::move(b));
 }
 result.relocation_offset=at;uint64_t previous_end=0;result.relocs.reserve(std::min<size_t>(relocs,s.size()/48));
 for(uint32_t i=0;i<relocs;++i){if(at>s.size()||s.size()-at<48)return {};
  Reloc r{u64(at),u64(at+16),u32(at+8),u32(at+12)};const size_t width=r.kind==3?16:8;
  if((r.kind!=0&&r.kind!=2&&r.kind!=3)||r.offset%4||r.offset<previous_end||r.offset>code||code-r.offset<width)return {};
  if(r.kind==0){if(r.reg!=0)return {};}
  else if(r.rip>=p.image_bytes||(r.kind==3&&(r.reg>30||r.reg==18)))return {};
  previous_end=r.offset+width;result.relocs.push_back(r);at+=48;
 }
 const size_t aligned=(at+4095)&~size_t(4095);
 if(aligned>s.size())return {};for(size_t i=at;i<aligned;++i)if(s[i]!=std::byte{})return {};
 result.code_offset=aligned;result.code_bytes=code;at=aligned;
 if(s.size()-at<code)return {};auto body=s.subspan(at,code);at+=code;
 for(auto r:result.relocs){
  if(r.kind==3){auto value=DecodeMove(body.subspan(r.offset,16),r.reg);if(!value||*value!=r.rip)return {};}
  else{uint64_t value;memcpy(&value,body.data()+r.offset,8);if(value!=(r.kind==0?0:r.rip))return {};}
 }
 if(reverse.size()!=pages)return {};auto expected=reverse.begin();size_t registrations=0;
 for(uint32_t i=0;i<pages;++i){uint64_t page,n;
  if(!read(page)||!read(n)||expected==reverse.end()||page!=expected->first||!n||n>MaxEdges-registrations)return {};
  registrations+=n;std::set<uint64_t> observed;
  // FEX serializes an append-only vector here, including repeated/reordered
  // entry registrations after overlapping multiblock translation. Compare the
  // complete relation as a set while bounding the serialized duplicate count.
  for(uint64_t j=0;j<n;++j){uint64_t x;if(!read(x)||!expected->second.contains(x))return {};observed.insert(x);}
  if(observed!=expected->second)return {}; ++expected;
 }
 if(at!=s.size())return {};return result;
}
}
