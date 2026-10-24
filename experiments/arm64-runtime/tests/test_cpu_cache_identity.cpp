#include "bb_cpu_cache_identity.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace BbCpuCache;
int main(){
 unsigned checked=0;auto check=[&](bool ok){++checked;if(!ok){std::printf("identity failure=%u\n",checked);std::exit(1);}};
 Identity id{};unsigned fill=1;
 for(auto* field:{&id.runtime_build,&id.patched_code,&id.module_set,&id.bridge_abi,&id.host_features,&id.codegen_config})field->fill(fill++);
 std::array<std::byte,64> payload{};payload[2]=std::byte(17);auto prefix=Prefix(payload,id);
 std::vector<std::byte> file(prefix.begin(),prefix.end());file.insert(file.end(),payload.begin(),payload.end());
 check(Valid(id)&&Check(file,id).size()==64);const auto key=Key(id);
 for(unsigned field=0;field<6;++field){Identity other=id;Digest* fields[]={&other.runtime_build,&other.patched_code,&other.module_set,&other.bridge_abi,&other.host_features,&other.codegen_config};
  (*fields[field])[0]^=0x80;check(Key(other)!=key&&Check(file,other).empty());
  fields[field]->fill(0);check(!Valid(other)&&Check(file,other).empty());}
 auto hw=id;hw.ordering=Identity::Ordering::Hardware;check(Key(hw)!=key&&Check(file,hw).empty());
 auto bad=id;bad.ordering=static_cast<Identity::Ordering>(2);check(!Valid(bad)&&Check(file,bad).empty());
 for(size_t at:{size_t(0),size_t(8),size_t(16),size_t(48),size_t(80),size_t(4098)}){auto corrupt=file;corrupt[at]^=std::byte(1);check(Check(corrupt,id).empty());}
 auto extra=file;extra.push_back(std::byte{});check(Check(extra,id).empty());
 for(size_t size:{size_t(0),size_t(79),size_t(4095),file.size()-1})check(Check({file.data(),size},id).empty());
 auto overflow=file;for(unsigned i=8;i<16;++i)overflow[i]=std::byte(255);check(Check(overflow,id).empty());
 check(Hash(payload)==Hash({file.data()+PrefixBytes,payload.size()}));
 std::printf("CPU_CACHE_IDENTITY checks=%u passed=1\n",checked);
}
