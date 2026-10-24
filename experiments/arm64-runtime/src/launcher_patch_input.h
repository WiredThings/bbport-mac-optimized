// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <cstdio>
// Consumes the existing scripts/patches.py BBPATCH2 wire format. Selection and
// compilation stay in the original launcher. Semantics match src/probe.c:
// ordered writes; whole image-pointer relocation slots are rebased at runtime.
namespace BbLauncherPatch {
inline void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class Boot>void Apply(const std::filesystem::path& path,void* image,const Boot& boot){
 std::ifstream f(path,std::ios::binary);Require(bool(f),"launcher patches unavailable");
 auto read=[&](void*p,size_t n){Require(bool(f.read(static_cast<char*>(p),n)),"truncated launcher patches");};
 auto word=[&](){uint64_t x;read(&x,8);return x;};
 char magic[8];read(magic,8);Require(!memcmp(magic,"BBPATCH2",8),"invalid launcher patch magic");
 const uint64_t base=word(),count=word();Require(base&&base<=UINT64_MAX-boot.size&&count<=65536,"invalid launcher patch bounds");
 struct Edit{uint64_t at;std::vector<unsigned char> bytes;};std::vector<Edit> edits;size_t total=0,rebased=0;
 for(uint64_t i=0;i<count;++i){
  auto at=word(),n=word();Require(n&&n<=4096&&at<=boot.size&&n<=boot.size-at&&boot.covered(at,n),"launcher patch outside mapped image");
  Require(n<=(16u<<20)-total,"launcher patch payload too large");total+=n;
  Edit edit{at,std::vector<unsigned char>(size_t(n))};read(edit.bytes.data(),size_t(n));
  for(auto r:boot.relocs){
   Require(r.target<=boot.size&&8<=boot.size-r.target,"invalid relocation slot");
   if(r.target>=at+n||at>=r.target+8)continue;
   Require(r.kind==0&&r.target>=at&&r.target+8<=at+n,"launcher patch overlaps a relocation");
   uint64_t value{};memcpy(&value,edit.bytes.data()+r.target-at,8);
   Require(value>=base&&value-base<boot.size&&boot.covered(value-base,1),"launcher patch pointer outside image");
   const auto native=reinterpret_cast<uintptr_t>(image);Require(native<=UINTPTR_MAX-(value-base),"launcher patch pointer overflow");
   value=native+(value-base);memcpy(edit.bytes.data()+r.target-at,&value,8);++rebased;
  }
  edits.push_back(std::move(edit));
 }
 Require(f.peek()==EOF,"trailing launcher patch bytes");
 // Validate the complete batch before changing the image. Original ordered
 // last-write behavior is kept, including intentionally overlapping writes.
 for(const auto& e:edits)memcpy(static_cast<unsigned char*>(image)+e.at,e.bytes.data(),e.bytes.size());
 printf("ARM LAUNCHER patches: writes=%llu bytes=%zu pointers_rebased=%zu\n",(unsigned long long)count,total,rebased);
}
}
