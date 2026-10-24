// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic-only original texture-allocation return; observes, never reclaims.
namespace ArmAllocation {
using U=uint64_t;
struct Record{U sequence{},address{},size{},allocator{},callee_offset{},kind{},primary_offset{},secondary_offset{};bool read_failed{};};
std::array<Record,1024> records{};std::mutex mutex;U issued{},fixture_calls{};bool enabled{};
void Observe(U address,U allocator,U size,U kind){
 if(!address||size<524288||size>(U(1)<<32)||address>=(U(1)<<40)||size>(U(1)<<40)-address)return;
 U table{},callee{};
 auto read=[&](U at,U& value){mach_vm_size_t count{};return active&&active->valid(at,8,false)&&mach_vm_read_overwrite(mach_task_self(),at,8,reinterpret_cast<mach_vm_address_t>(&value),&count)==KERN_SUCCESS&&count==8;};
 const bool ok=read(allocator,table)&&table<=UINT64_MAX-0x58&&read(table+0x58,callee);
 const U offset=ok&&callee>=active->image&&callee-active->image<active->image_size&&executable(active,callee)?callee-active->image:0;
 U primary{},secondary{};
 if(offset==0x1fbeb30&&allocator<=UINT64_MAX-16){
  // Proven vtable-dispatch body: >=1MiB selects child +8, then +16;
  // smaller sizes reverse them and use slot 0x88 on the fallback.
  auto method=[&](U child_slot,U method_slot){U object{},vtable{},entry{};if(!read(child_slot,object)||!read(object,vtable)||vtable>UINT64_MAX-method_slot||!read(vtable+method_slot,entry))return U{};return entry>=active->image&&entry-active->image<active->image_size&&executable(active,entry)?entry-active->image:U{};};
  const bool big=size>=1048576;primary=method(allocator+(big?8:16),0x58);secondary=method(allocator+(big?16:8),big?0x58:0x88);
 }
 std::scoped_lock lock(mutex);const U sequence=++issued;records[(sequence-1)%records.size()]={sequence,address,size,allocator,offset,kind,primary,secondary,!ok};
}
Record Match(U address,U size,U& count){
 std::scoped_lock lock(mutex);count=issued;Record result{};
 for(auto r:records)if(r.sequence>result.sequence&&r.address<=address&&size<=r.size&&address-r.address<=r.size-size)result=r;
 return result;
}
class FixtureProbe final:public HleCallAdapter {
public:HleCallResult Invoke(HleCallFrame& frame)const override{
 check(frame.gpr[7]==11&&frame.gpr[6]==22&&frame.gpr[2]==33&&(frame.gpr[1]==1||frame.gpr[1]==2),"allocation probe argument preservation");++fixture_calls;
 for(unsigned i:{0,1,2,6,7,8,9,10,11})frame.gpr[i]=0xfeedface;for(auto& x:frame.xmm)x={0,0};return true;
 }
};
std::unique_ptr<Mapping> Install(State& host,Mapping& image,const Boot& boot,bool authored){
 const U sites[]={authored?0x2e20:BbAllocationProbe::BufferReturn,authored?0x2f20:BbAllocationProbe::TextureReturn};
 check(boot.covered(sites[0],BbAllocationProbe::Original.size(),true)&&boot.covered(sites[1],BbAllocationProbe::TextureOriginal.size(),true),"allocation probe outside executable segment");
 auto mapping=std::make_unique<Mapping>(16384);
 auto adapter=authored?std::shared_ptr<HleCallAdapter>(std::make_shared<FixtureProbe>()):MakeHleCallAdapter(Observe);
 const U target=veneer(host,host.registry.Register(framed(adapter),"diagnostic.resource_allocation_return"));
 auto first=BbAllocationProbe::Prepare(std::span(static_cast<const unsigned char*>(image.ptr),size_t(boot.size)),image.at(),mapping->at(),sites[0],target,mapping->size,1);
 const size_t offset=(first.stubs.size()+15)&~size_t(15);
 auto second=BbAllocationProbe::Prepare(std::span(static_cast<const unsigned char*>(image.ptr),size_t(boot.size)),image.at(),mapping->at()+offset,sites[1],target,mapping->size-offset,2);
 memcpy(mapping->ptr,first.stubs.data(),first.stubs.size());memcpy(static_cast<unsigned char*>(mapping->ptr)+offset,second.stubs.data(),second.stubs.size());check(!mprotect(mapping->ptr,mapping->size,PROT_READ),"allocation probe read permission failed");host.ranges.push_back({mapping->at(),mapping->size,true,false});
 for(const auto* plan:{&first,&second})for(const auto& patch:plan->patches)memcpy(static_cast<unsigned char*>(image.ptr)+patch.offset,patch.bytes.data(),patch.bytes.size());enabled=true;
 printf("ARM RESOURCE allocation diagnostic installed: buffer+texture original returns, 1024 bounded records, >=512KiB only, no lifetime/reclaim change\n");return mapping;
}
}
