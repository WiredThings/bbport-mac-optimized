// SPDX-License-Identifier: GPL-2.0-or-later
#include "startup_patches.h"
#include <array>
#include <cassert>
#include <string_view>
#include <unistd.h>
struct Image{bool covered(uint64_t a,size_t n)const{return a<=2048&&n<=2048-a;}};
int main(int argc,char**argv){
 std::string_view mode=argc>1?argv[1]:"valid";auto path=std::filesystem::temp_directory_path()/("bb-startup-fixture-"+std::to_string(getpid()));
 std::array<unsigned char,2048> data{},original{};
 {
  std::ofstream f(path,std::ios::binary);auto put=[&](const void*p,size_t n){f.write((const char*)p,n);};
  put("BBSTART1",8);uint32_t count=mode=="many"?180:mode=="excess"?513:2;put(&count,4);
  for(unsigned i=0;i<(mode=="many"?180u:2u);++i){uint64_t a=mode=="many"?8+i*4:i==0?8:mode=="bounds"?2047:mode=="overlap"?8:16;uint32_t n=2;put(&a,8);put(&n,4);unsigned char before[2]={0,0},after[2]={1,2};if(mode=="mismatch"&&i==1)before[0]=3;put(before,2);if(!(mode=="truncated"&&i==1))put(after,2);}
 }
 bool rejected=false;try{BbStartup::Apply(path,data.data(),Image{});}catch(const std::runtime_error&){rejected=true;}
 std::filesystem::remove(path);
 if(mode=="many"){assert(!rejected);for(unsigned i=0;i<180;++i)assert(data[8+i*4]==1&&data[9+i*4]==2);}
 else if(mode=="valid"){assert(!rejected&&data[8]==1&&data[9]==2&&data[16]==1&&data[17]==2);}
 else assert(rejected&&data==original);
 puts("STARTUP guarded application / atomic refusal verified");
}
