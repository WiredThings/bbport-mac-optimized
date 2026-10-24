#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/fextl/memory_resource.h>
#include "Interface/Core/LookupCache.h"
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <array>
#include <cstdio>
namespace {struct Entry{void*p{};size_t n{};bool live{};};std::array<Entry,256>records{};unsigned maps{},frees{},delinks{};void delink(FEXCore::Context::ExitFunctionLinkData*){++delinks;}
void* counting_map(void*base,size_t size,int prot,int flags,int fd,off_t off){void*p=::mmap(base,size,prot,flags,fd,off);assert(p!=MAP_FAILED&&maps<records.size());records[maps++]={p,size,true};return p;}
int counting_free(void*p,size_t n){for(auto&e:records)if(e.live&&e.p==p){assert(e.n==n);e.live=false;++frees;return ::munmap(p,n);}assert(false&&"foreign or duplicate free");return -1;}
}
static_assert(!std::is_copy_constructible_v<fextl::pmr::named_monotonic_page_buffer_resource>);
int main(int argc,char**argv){
 assert(FEXCore::Allocator::mmap==::mmap&&FEXCore::Allocator::munmap==::munmap);
 const bool clear=argc>1&&std::string_view(argv[1])=="clear";const bool explicit_release=argc>1&&!clear;auto original_map=FEXCore::Allocator::mmap;auto original_free=FEXCore::Allocator::munmap;FEXCore::Allocator::mmap=counting_map;FEXCore::Allocator::munmap=counting_free;
 unsigned released_clears{};
 if(clear){FEXCore::GuestToHostMap map;for(unsigned round=0;round<8;++round){for(unsigned i=0;i<128;++i)map.BlockLinks->insert({{uint64_t(i)*4096,reinterpret_cast<FEXCore::Context::ExitFunctionLinkData*>(uintptr_t(0x10000)+i*16)},delink});auto before=frees;{auto lock=map.AcquireWriteLock();map.ClearCache(lock);}assert(map.BlockLinks->empty());released_clears+=frees>before;}assert(delinks==1024);}
 else {fextl::pmr::named_monotonic_page_buffer_resource resource("authored-lifetime");void*p=resource.allocate(16384,16);assert(p);static_cast<unsigned char*>(p)[0]=0x5a;if(explicit_release){resource.release();resource.release();}}
 const bool reclaimed=maps==frees&&(!clear||released_clears==8);std::printf("PMR authored maps=%u frees_at_scope_exit=%u explicit_release=%u reclaimed=%u\n",maps,frees,explicit_release,reclaimed);
 // The red control must clean up its own intentionally exposed allocations.
 for(auto&e:records)if(e.live){assert(!::munmap(e.p,e.n));e.live=false;}
 FEXCore::Allocator::mmap=original_map;FEXCore::Allocator::munmap=original_free;std::printf("PMR clear=%u released_clears=%u delinks=%u\n",clear,released_clears,delinks);return reclaimed?0:7;
}
