// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
namespace BbStartup {
inline void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class Image> void Apply(const std::filesystem::path& path,void* image,const Image& boot){
 std::ifstream f(path,std::ios::binary);check(bool(f),"startup patches missing");
 auto read=[&](void* ptr,size_t n){check(bool(f.read((char*)ptr,n)),"truncated startup patches");};
 char magic[8];uint32_t count;read(magic,8);read(&count,4);check(!memcmp(magic,"BBSTART1",8)&&count>0&&count<=512,"invalid startup patch header");
 struct Edit{uint64_t offset;std::vector<unsigned char> after;};std::vector<Edit> edits;size_t total=0;
 for(unsigned i=0;i<count;++i){uint64_t offset;uint32_t size;read(&offset,8);read(&size,4);
  check(size>0&&size<=4096&&boot.covered(offset,size),"startup patch outside image");
  for(auto& old:edits)check(offset+size<=old.offset||old.offset+old.after.size()<=offset,"overlapping startup patches");
  std::vector<unsigned char> before(size),after(size);read(before.data(),size);read(after.data(),size);
  check(!memcmp((unsigned char*)image+offset,before.data(),size),"startup patch original bytes mismatch");
  total+=size;edits.push_back({offset,std::move(after)});
 }
 check(f.peek()==EOF,"trailing startup patch bytes");
 // No write occurs until all records and original bytes have been checked.
 for(auto& edit:edits)memcpy((unsigned char*)image+edit.offset,edit.after.data(),edit.after.size());
 printf("ARM STARTUP guarded startup patches: writes=%u bytes=%zu\n",count,total);
}
}
