#include "bb_guest_string.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unordered_map>
static std::unordered_map<void*,size_t> live;static size_t allocs,freed;
extern "C" void* bbnet_guest_alloc(uint64_t n,uint64_t alignment){void*p=nullptr;if(posix_memalign(&p,alignment,n))return nullptr;assert(live.emplace(p,n).second);++allocs;return p;}
extern "C" void bbnet_guest_free(void*p){if(!p)return;assert(live.erase(p)==1);++freed;free(p);}
int main(){
 {BbNet::GuestString empty;assert(!BbNet::ExposeGuestString(empty)&&live.empty());}
 for(unsigned i=0;i<1024;i++){
  {BbNet::GuestString value="X: y\r\n";auto*p=BbNet::ExposeGuestString(value);assert(p&&live.contains((void*)p)&&!strcmp(p,"X: y\r\n"));auto before=allocs;assert(BbNet::ExposeGuestString(value)==p&&allocs==before);
   BbNet::GuestString moved=std::move(value);assert(BbNet::ExposeGuestString(moved)==p);
  }
  assert(live.empty());
  {BbNet::GuestString value;value=std::string(8192,'h');auto*p=BbNet::ExposeGuestString(value);assert(live.contains((void*)p)&&strlen(p)==8192);}
  assert(live.empty());
 }
 assert(allocs==freed);printf("guest header string passed: empty, short SSO escape, stable repeated exposure, move ownership, 2048 release cycles; allocations=%zu frees=%zu\n",allocs,freed);
}
