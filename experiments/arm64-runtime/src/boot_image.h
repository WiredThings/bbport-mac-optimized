// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <map>
#include <string>
#include <cstring>
#include <stdexcept>
namespace BbBoot {
using u64=std::uint64_t;
namespace fs=std::filesystem;
inline void check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
struct Reader {
 std::ifstream f;
 explicit Reader(const fs::path& path):f(path,std::ios::binary){check(bool(f),"boot input unavailable");}
 void bytes(void* p,size_t n){check(bool(f.read(static_cast<char*>(p),n)),"truncated boot");}
 u64 word(){u64 v;bytes(&v,8);return v;}
};
struct Segment{u64 address,size,flags;};struct Reloc{u64 target,kind,value,addend;};
struct Boot {
 struct Module{u64 base,size,init,tls_address,tls_memsz,tls_filesz,tls_module;};
 u64 size{},entry{},caps{},procparam{};std::array<u64,4> main_tls{};std::string format;
 std::vector<Module> modules;std::vector<Segment> segments;std::vector<Reloc> relocs;std::vector<std::string> names;
 fs::path path;std::streamoff image_offset{};std::map<u64,std::pair<u64,u64>> bindings;
 bool covered(u64 a,u64 n,bool executable=false)const{if(a>size||n>size-a)return false;for(auto s:segments)if(a>=s.address&&a-s.address<=s.size&&n<=s.size-(a-s.address)&&(!executable||(s.flags&1)))return true;return false;}
 explicit Boot(const fs::path& file):path(file){Reader r(path);char magic[8];r.bytes(magic,8);format.assign(magic,8);
  check(format=="BBPROBE2"||format=="BBPROBE5","prototype supports BBPROBE2/5 metadata only");
  size=r.word();entry=r.word();auto ns=r.word(),nr=r.word(),ni=r.word();caps=r.word();
  check(size&&size<=(512u<<20)&&entry<size&&ns&&ns<=64&&nr<=1000000&&ni<=100000&&!(caps&~u64{1}),"unsupported boot limits/capabilities");
  if(format=="BBPROBE5"){
   procparam=r.word();for(auto& v:main_tls)v=r.word();auto nm=r.word();check(nm&&nm<=8,"invalid module count");
   check(main_tls[1]<=main_tls[2]&&main_tls[2]<=1048576&&main_tls[0]<=size&&main_tls[1]<=size-main_tls[0]&&!(main_tls[3]&(main_tls[3]-1))&&main_tls[3]<=4096,"invalid main TLS");
   for(u64 i=0;i<nm;++i){Module m{r.word(),r.word(),r.word(),r.word(),r.word(),r.word(),r.word()};check(m.base<size&&m.size&&m.size<=size-m.base&&m.init>=m.base&&m.init-m.base<m.size,"invalid module span");if(m.tls_module)check(m.tls_address<size&&m.tls_memsz<=size-m.tls_address&&m.tls_memsz<=1048576&&m.tls_filesz<=m.tls_memsz&&m.tls_module>=2&&m.tls_module<=7,"invalid module TLS");modules.push_back(m);}
   auto nb=r.word();check(nb<=ni,"invalid binding count");
   for(u64 i=0;i<nb;++i){auto index=r.word(),address=r.word(),kind=r.word();bool inside=false;for(auto m:modules)inside|=address>=m.base&&address-m.base<m.size;check(index<ni&&inside&&(kind==1||kind==2)&&!bindings.contains(index),"invalid binding");bindings[index]={address,kind};}
  }
  for(u64 i=0;i<ns;++i){Segment s{r.word(),r.word(),r.word()};check(s.address<=size&&s.size&&s.size<=size-s.address&&s.address%4096==0&&s.flags<=7,"invalid segment");for(auto old:segments)check(s.address+s.size<=old.address||old.address+old.size<=s.address,"overlapping segments");segments.push_back(s);}
  check(covered(entry,1,true),"entry is not executable");
  for(u64 i=0;i<ni;++i){char n[128];r.bytes(n,sizeof(n));check(memchr(n,0,sizeof(n)),"unterminated import");names.emplace_back(n);}
  for(u64 i=0;i<nr;++i){Reloc x{r.word(),r.word(),r.word(),r.word()};check(covered(x.target,8)&&x.kind<=2&&(x.kind==2||x.addend==0)&&x.addend<16384&&(x.kind?x.value<ni:x.value<size),"invalid relocation");relocs.push_back(x);}
  for(auto m:modules)check(covered(m.init,1,true)&&(!m.tls_module||covered(m.tls_address,m.tls_memsz)),"unmapped module entry/TLS");
  if(!modules.empty())check(covered(procparam,64),"unmapped procparam");
  for(auto [index,binding]:bindings)check(covered(binding.first,1,binding.second==1),"unmapped linked export");
  image_offset=r.f.tellg();r.f.seekg(0,std::ios::end);check(r.f.tellg()-image_offset==std::streamoff(size),"incorrect image size");
 }
 void load(void* address)const{Reader r(path);r.f.seekg(image_offset);r.bytes(address,size);check(r.f.peek()==EOF,"boot changed while loading");}
};
}
