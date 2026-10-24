#include "bb_thd_region_url.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace BbThdRegionUrl;
int main(){
 const size_t page=getpagesize(),size=(End+page-1)&~(page-1);auto*mem=(unsigned char*)mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mem!=MAP_FAILED);
 for(size_t at:{Getter,Region,Table,Eu,Empty})assert(!mprotect(mem+(at&~(page-1)),page,PROT_READ|PROT_WRITE));
 uintptr_t base=(uintptr_t)mem;bool write_allowed=true;unsigned queries=0;
 auto writable=[&](uintptr_t a,size_t n){++queries;assert(a==base+Table+5*8&&n==8);return write_allowed;};
 auto pointer=[&](size_t at){uintptr_t v{};memcpy(&v,mem+at,8);return v;};
 auto set=[&](size_t at,uintptr_t v){memcpy(mem+at,&v,8);};
 auto reset=[&]{memcpy(mem+Getter,Unlock.data(),Unlock.size());uint32_t r=5;memcpy(mem+Region,&r,4);memcpy(mem+Eu,EuUrl,sizeof(EuUrl));memset(mem+Empty,0,2);set(Table+2*8,base+Eu);set(Table+5*8,base+Empty);write_allowed=true;};
 reset();assert(Repair(std::span(mem,size),"CUSA03173",false,writable)==Result::NotApplicable);assert(queries==0&&pointer(Table+5*8)==base+Empty);
 assert(Repair(std::span(mem,size),"CUSA01363",true,writable)==Result::NotApplicable);assert(queries==0);
 assert(Repair(std::span(mem,End-1),"CUSA03173",true,writable)==Result::Refused);assert(queries==0);
 mem[Getter]=0x55;assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::NotApplicable);reset();
 uint32_t r=2;memcpy(mem+Region,&r,4);assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::NotApplicable);reset();
 set(Table+2*8,base+Eu+2);assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::Refused);reset();
 set(Table+5*8,base+Empty+2);assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::Refused);reset();
 mem[Eu]='X';assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::Refused);reset();
 mem[Empty]=1;assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::Refused);reset();
 write_allowed=false;assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::Refused);assert(pointer(Table+5*8)==base+Empty);reset();
 std::array<unsigned char,6> before{};memcpy(before.data(),mem+Getter,6);assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::Repaired);
 assert(pointer(Table+5*8)==pointer(Table+2*8)&&before==Unlock);memcpy(&r,mem+Region,4);assert(r==5);unsigned q=queries;
 assert(Repair(std::span(mem,size),"CUSA03173",true,writable)==Result::AlreadyCorrect&&queries==q);
 munmap(mem,size);puts("PASS THD region URL: disabled/wrong title/truncation/getter/region/relocations/string/write-permission guards; one pointer repair, idempotent, language getter and region preserved");
}
