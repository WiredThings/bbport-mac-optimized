// Private persistent-cache experiment only; no game integration.
#pragma once
#include <FEXCore/Core/CodeCache.h>
#include <limits>
#include <bit>
#include <algorithm>
namespace BbCachePages {
struct Selection {bool valid=false;std::span<std::byte> range{};};
// Caller must serialize page publication and keep mapped file/code/cache alive.
// Input file/relocations must have passed independent structural validation.
inline Selection Select(FEXCore::MappedCodeCacheFile& code,size_t start,size_t end,size_t host_page){
 constexpr size_t guest_page=4096;
 if(host_page<guest_page||host_page%guest_page||!std::has_single_bit(host_page)||
    reinterpret_cast<uintptr_t>(code.CodeBuffer.data())%host_page||
    code.CodeBuffer.empty()||code.CodeBuffer.size()%guest_page||code.LoadedPages.size()!=code.NumPages()||
    code.PageRelocationRanges.size()!=code.NumPages()||start>=end||end>code.NumPages())return {};
 const size_t group=host_page/guest_page,total=code.NumPages();
 if(total>std::numeric_limits<size_t>::max()-(group-1))return {};
 start=(start/group)*group;end=std::min(total,((end+group-1)/group)*group);
 for(size_t step=0;step<=total;++step){
   const auto first=code.LoadedPages.begin()+start,last=code.LoadedPages.begin()+end;
   if(std::find(first,last,false)==last)return {true,{}};
   // Never overwrite previously executable pages. Mixed groups indicate wrong state/caller synchronization.
   if(std::find(first,last,true)!=last)return {};
   auto selected=FEXCore::AbstractCodeCache::SelectCodeRangeToFinalize(code,start,end);
   if(selected.empty())return {};
   const size_t lo=(selected.data()-code.CodeBuffer.data())/guest_page;
   const size_t hi=lo+selected.size()/guest_page;
   const size_t next_start=(lo/group)*group,next_end=std::min(total,((hi+group-1)/group)*group);
   if(next_start==start&&next_end==end)return {true,code.CodeBuffer.subspan(start*guest_page,(end-start)*guest_page)};
   if(next_start>start||next_end<end||next_end>total)return {};
   start=next_start;end=next_end;
 }
 return {};
}
}
