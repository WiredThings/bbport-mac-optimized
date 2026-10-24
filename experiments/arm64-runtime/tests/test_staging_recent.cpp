// SPDX-License-Identifier: GPL-2.0-or-later
#include "bb_staging_recent.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <numeric>
#include <set>
#include <vector>
struct Block {std::uint64_t last_used_frame{},pending_tick{};};
int main() {
 using namespace BbStagingRecent;
 std::vector<Block> blocks{{3,0},{8,0},{8,0},{1,0}};std::vector<std::size_t> tried;
 auto no=[&](auto i)->std::optional<std::uint64_t>{tried.push_back(i);return {};};
 assert(!Try(blocks,0,no));assert((tried==std::vector<std::size_t>{0,1,2,3}));
 tried.clear();assert(!Try(blocks,3,no));assert((tried==std::vector<std::size_t>{3,1,2,0}));
 tried.clear();auto selected=Try(blocks,3,[&](auto i)->std::optional<std::uint64_t>{tried.push_back(i);if(i==2)return 0;return {};});
 assert(selected&&selected->index==2&&selected->offset==0);assert((tried==std::vector<std::size_t>{3,1,2}));
 tried.clear();assert(!Try(std::vector<Block>{},0,no));assert(!Try(blocks,4,no));assert(tried.empty());
 for(unsigned n:{1,2,3,20,64,65,128}) {
  blocks.assign(n,{});for(unsigned i=0;i<n;++i)blocks[i].last_used_frame=(i*13)%17;
  for(unsigned current=0;current<n;++current) {
   tried.clear();assert(!Try(blocks,current,no));auto unique=tried;std::sort(unique.begin(),unique.end());assert(unique.size()==n&&std::adjacent_find(unique.begin(),unique.end())==unique.end()&&unique.front()==0&&unique.back()==n-1&&tried.front()==current);
   if(n>MaxOrderedBlocks)for(unsigned k=0;k<n;++k)assert(tried[k]==(current+k)%n);
  }
 }
 // Deterministic GPU model: every frame consumes two whole blocks; the prior
 // frame is complete. Keep all20 blocks/capacity in both policies, no eviction.
 auto model=[](bool hot) {
  std::vector<Block>b(20);std::size_t current=19;std::set<std::size_t> touched;
  for(std::uint64_t frame=1;frame<=180;++frame) {
   for(unsigned request=0;request<2;++request) {
    const auto reserve=[&](auto i)->std::optional<std::uint64_t>{if(b[i].pending_tick>frame-1)return {};b[i].pending_tick=frame;return 0;};
    std::optional<Selection>s;
    if(hot)s=Try(b,current,reserve);
    else for(unsigned k=0;k<b.size();++k){auto i=(current+k)%b.size();if(auto at=reserve(i)){s=Selection{i,*at};break;}}
    assert(s&&b.size()==20);current=s->index;b[current].last_used_frame=frame;if(frame>100)touched.insert(current);
   }
  }
  return touched.size();
 };
 const auto old=model(false),hot=model(true);assert(old==20&&hot==2);
 // Pending state always wins over recency; callback cannot reserve a busy block.
 blocks={{100,99},{99,99},{1,0}};tried.clear();selected=Try(blocks,0,[&](auto i)->std::optional<std::uint64_t>{tried.push_back(i);if(blocks[i].pending_tick>10)return {};return 128;});assert(selected&&selected->index==2&&selected->offset==128);
 assert(!Try(blocks,0,[](auto)->std::optional<std::uint64_t>{return {};}));
 printf("STAGING_RECENT order/ties/zero-offset/coverage/bounded-fallback/pending/failure passed; simulated active old=%zu hot=%zu capacity=20 unchanged\n",old,hot);
}
