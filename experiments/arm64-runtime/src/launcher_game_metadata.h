// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <cstring>
#include <stdexcept>
namespace BbLauncherGame {
inline void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Metadata{std::string serial,title,version,saves;uint32_t attributes{};};
inline bool TitleId(const std::string& s){return s.size()==9&&s.starts_with("CUSA")&&s.find_first_not_of("0123456789",4)==std::string::npos;}
inline Metadata Read(const std::filesystem::path& path){
 const auto size=std::filesystem::file_size(path);Require(size>=20&&size<=65536,"invalid launcher SFO size");
 std::vector<unsigned char>b(size);std::ifstream f(path,std::ios::binary);Require(bool(f.read((char*)b.data(),b.size()))&&f.peek()==EOF,"launcher SFO changed/truncated");
 Require(!memcmp(b.data(),"\0PSF",4),"invalid launcher SFO magic");
 auto u32=[&](size_t a){uint32_t x;memcpy(&x,b.data()+a,4);return x;};auto u16=[&](size_t a){uint16_t x;memcpy(&x,b.data()+a,2);return x;};
 const auto keys=u32(8),values=u32(12),count=u32(16);
 Require(count<=(b.size()-20)/16&&keys>=20+count*16&&keys<=values&&values<=b.size(),"invalid launcher SFO tables");
 std::map<std::string,std::string> strings;uint32_t attributes=0;std::map<std::string,bool> seen;
 for(size_t i=0;i<count;++i){const auto a=20+i*16;const auto key=u16(a);const auto format=u16(a+2);const auto n=u32(a+4),capacity=u32(a+8),offset=u32(a+12);
  Require(key<values-keys&&offset<=b.size()-values&&n<=b.size()-values-offset&&n<=capacity,"invalid launcher SFO entry");
  const char*name=(const char*)b.data()+keys+key;auto*end=(const char*)memchr(name,0,values-keys-key);Require(end,"unterminated launcher SFO key");std::string k(name,end);Require(!seen.contains(k),"duplicate launcher SFO key");seen[k]=true;
  if(k=="TITLE_ID"||k=="TITLE"||k=="APP_VER"||k=="INSTALL_DIR_SAVEDATA"){
   Require(format==0x204&&n&&b[values+offset+n-1]==0,"invalid launcher SFO string");const char*s=(const char*)b.data()+values+offset;Require(strlen(s)==n-1,"embedded NUL in launcher SFO string");strings[k]=std::string(s,n-1);
  }else if(k=="ATTRIBUTE"){Require(format==0x404&&n==4,"invalid launcher SFO attributes");attributes=u32(values+offset);}
 }
 Metadata result{strings["TITLE_ID"],strings["TITLE"],strings["APP_VER"],strings.contains("INSTALL_DIR_SAVEDATA")?strings["INSTALL_DIR_SAVEDATA"]:strings["TITLE_ID"],attributes};
 Require(TitleId(result.serial)&&TitleId(result.saves)&&!result.title.empty()&&result.title.size()<128&&result.version=="01.09","unsupported launcher game metadata");return result;
}
}
