// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <stdexcept>
// Built before guest threads start, never updated while they execute. Only
// lifetime-long image/veneer/host-heap ranges belong here, never guest VMAs or
// temporary callback stacks. Prefix maxima preserve the original any-one-range
// semantics even with overlapping ranges; they never bridge unmapped holes.
class StaticRangeIndex {
 struct Entry {uintptr_t begin, read_end, write_end;};
 std::vector<Entry> entries;
 bool built{};
public:
 template<class Ranges> void Build(const Ranges& ranges) {
  if(built)throw std::logic_error("static range index already built");
  entries.reserve(ranges.size());
  for(auto r:ranges){
   if(!r.Size||r.Begin>UINTPTR_MAX-r.Size)throw std::logic_error("invalid static range");
   entries.push_back({r.Begin,r.Begin+r.Size,r.Writable?r.Begin+r.Size:0});
  }
  std::sort(entries.begin(),entries.end(),[](auto a,auto b){return a.begin<b.begin;});
  uintptr_t read=0,write=0;
  for(auto& r:entries){r.read_end=read=std::max(read,r.read_end);r.write_end=write=std::max(write,r.write_end);}
  built=true;
 }
 bool Contains(uintptr_t a,size_t n,bool write)const {
  if(!a||!n||a>UINTPTR_MAX-n)return false;
  auto end=std::upper_bound(entries.begin(),entries.end(),a,[](uintptr_t a,const Entry& r){return a<r.begin;});
  return end!=entries.begin()&&a+n<=(write?std::prev(end)->write_end:std::prev(end)->read_end);
 }
 bool Ready()const{return built;}
 size_t Bytes()const{return entries.capacity()*sizeof(Entry);}
};

// A temporary stack can bypass the global range lock only on its own thread,
// and only while the mapping's owning callback scope is alive. No stale TLS
// pointer survives its scope and other threads still use the live range list.
class ScopedGuestStack {
 struct Range{uintptr_t begin,end;};
 inline static thread_local Range current{};
 Range previous;
public:
 ScopedGuestStack(uintptr_t begin,size_t size):previous(current){
  if(!begin||!size||begin>UINTPTR_MAX-size)throw std::logic_error("invalid scoped guest stack");
  current={begin,begin+size};
 }
 ~ScopedGuestStack(){current=previous;}
 ScopedGuestStack(const ScopedGuestStack&)=delete;
 static bool Contains(uintptr_t a,size_t n){return a&&n&&a>=current.begin&&a<=current.end&&n<=current.end-a;}
};
