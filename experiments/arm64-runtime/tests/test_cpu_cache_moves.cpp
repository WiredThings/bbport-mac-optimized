#define main upstream_smoke_main
#include AETHER_SMOKE_SOURCE
#undef main
#include "Interface/Context/Context.h"
#include "Interface/Core/ArchHelpers/Arm64Emitter.h"
#include "bb_cpu_cache_validation.h"
#include <cassert>
#include <set>
int main(){
 ConfigScope config;auto ctx=FEXCore::Context::Context::CreateNewContext(FEX::FetchHostFeatures());assert(ctx);
 auto* impl=static_cast<FEXCore::Context::ContextImpl*>(ctx.get());
 std::set<uint64_t> values{0,1,65535,65536,~uint64_t{},0xffffffffffff1234ULL,0xffffffff12345678ULL,0x123456789abcdef0ULL,0x10000000000ULL};
 for(unsigned bit=0;bit<64;++bit){values.insert(uint64_t(1)<<bit);values.insert(~(uint64_t(1)<<bit));}
 for(unsigned size=2;size<=64;size*=2)for(unsigned n=1;n<size;++n)for(unsigned shift=0;shift<size;++shift){
  uint64_t mask=size==64?~uint64_t{}:(uint64_t(1)<<size)-1;
  auto value=(uint64_t(1)<<n)-1;value=shift?((value>>shift)|(value<<(size-shift)))&mask:value;
  uint64_t full=0;for(unsigned i=0;i<64;i+=size)full|=value<<i;values.insert(full);
 }
 uint64_t rng=0x123456789abc;for(unsigned i=0;i<1024;++i){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;values.insert(rng);}
 size_t checks=0;
 for(auto value:values)for(unsigned reg:{0u,4u,19u,29u,30u}){
  std::array<std::byte,64> bytes{};FEXCore::CPU::Arm64Emitter emitter(impl,bytes.data(),bytes.size());
  emitter.LoadConstant(ARMEmitter::Size::i64Bit,ARMEmitter::Register(reg),value,FEXCore::CPU::Arm64Emitter::PadType::DOPAD);
  assert(emitter.GetCursorOffset()==16);auto got=BbCacheValidation::DecodeMove(std::span(bytes).first(16),reg);
  if(!got||*got!=value){printf("MOVE failed value=%#llx register=%u\n",(unsigned long long)value,reg);return 1;}++checks;
 }
 printf("CACHE_MOVES checks=%zu decoded_actual_FEX_emissions=1 guest_executed=0\n",checks);
}
