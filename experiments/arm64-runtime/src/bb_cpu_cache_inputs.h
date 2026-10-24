// Canonical identity inputs shared by isolated producer, consumer and authored tests.
#pragma once
#include "bb_cpu_cache_identity.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/HostFeatures.h>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <vector>
namespace BbCacheInputs {
inline void Append(std::vector<std::byte>& out,std::string_view name,std::string_view value){
 for(uint64_t size:{uint64_t(name.size()),uint64_t(value.size())})for(unsigned i=0;i<8;++i)out.push_back(std::byte((size>>(8*i))&255));
 auto add=[&](std::string_view x){const auto offset=out.size();out.resize(offset+x.size());std::memcpy(out.data()+offset,x.data(),x.size());};add(name);add(value);
}
inline BbCpuCache::Digest Text(std::string_view value){return BbCpuCache::Hash(std::as_bytes(std::span(value.data(),value.size())));}
inline BbCpuCache::Digest Build(const char* path){
 int fd=open(path,O_RDONLY|O_NOFOLLOW);struct stat st{};if(fd<0)return {};if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>64*1024*1024){close(fd);return {};}
 CC_SHA256_CTX context;CC_SHA256_Init(&context);std::array<unsigned char,65536> buffer{};off_t count=0;
 for(;;){ssize_t got=read(fd,buffer.data(),buffer.size());if(got<0&&errno==EINTR)continue;if(got<0){close(fd);return {};}if(!got)break;count+=got;CC_SHA256_Update(&context,buffer.data(),static_cast<CC_LONG>(got));}
 struct stat after{};const bool stable=fstat(fd,&after)==0&&count==st.st_size&&after.st_size==st.st_size&&after.st_ino==st.st_ino;close(fd);if(!stable)return {};
 BbCpuCache::Digest result{};CC_SHA256_Final(result.data(),&context);return result;
}
inline BbCpuCache::Digest Features(const FEXCore::HostFeatures& features){
 std::vector<std::byte> out;
#define FIELD(name) Append(out,#name,std::to_string(features.name));
 FIELD(DCacheLineSize) FIELD(ICacheLineSize) FIELD(SupportsCacheMaintenanceOps)
 FIELD(SupportsAES) FIELD(SupportsCRC) FIELD(SupportsCLZERO) FIELD(SupportsAtomics)
 FIELD(SupportsRCPC) FIELD(SupportsTSOImm9) FIELD(SupportsRAND) FIELD(SupportsAVX)
 FIELD(SupportsSVE128) FIELD(SupportsSVE256) FIELD(SupportsSHA) FIELD(SupportsPMULL_128Bit)
 FIELD(SupportsCSSC) FIELD(SupportsFCMA) FIELD(SupportsFlagM) FIELD(SupportsFlagM2)
 FIELD(SupportsRPRES) FIELD(SupportsPreserveAllABI) FIELD(SupportsAES256) FIELD(SupportsSVEBitPerm)
 FIELD(SupportsCPUIndexInTPIDRRO) FIELD(SupportsFRINTTS) FIELD(SupportsECV) FIELD(SupportsWFXT)
 FIELD(Supports3DNow) FIELD(SupportsSSE4a) FIELD(SupportsMOPS) FIELD(PreferZVAForVZero)
 FIELD(SupportsAFP) FIELD(SupportsFloatExceptions) FIELD(IsInstCountCI)
#undef FIELD
 Append(out,"HostType",std::to_string(static_cast<unsigned>(features.HostType)));
 Append(out,"MIDR-count",std::to_string(features.CPUMIDRs.size()));for(auto id:features.CPUMIDRs)Append(out,"MIDR",std::to_string(id));
 return BbCpuCache::Hash(out);
}
inline BbCpuCache::Digest Config(){
 std::vector<std::byte> out;
 // Include every generated option's effective value and defaults, not just environment overrides.
#define OPT_BASE(type,group,name,json,def) Append(out,#name,std::to_string(FEXCore::Config::Get_##name()()));
#define OPT_STR(group,name,json,def) Append(out,#name,FEXCore::Config::Get_##name()());
#define OPT_STRARRAY(group,name,json,def) {auto value=FEXCore::Config::Get_##name();Append(out,#name,std::to_string(value.All().size()));for(auto& v:value.All())Append(out,#name,v);}
#include <FEXCore/Config/ConfigValues.inl>
 return BbCpuCache::Hash(out);
}
inline BbCpuCache::Digest CanonicalConfig(){
 std::vector<std::byte> out;
 const auto add=[&](std::string_view name,std::string_view value){
  // These three storage-pipeline switches intentionally differ between an
  // offline relocation producer and a normal-budget consumer. Other options
  // retain their effective values; do not normalize semantic/compiler options.
  if(name=="ENABLECODECACHINGWIP"||name=="ENABLELAZYCODECACHINGWIP"||name=="ENABLECODECACHEVALIDATION")value="0";
  Append(out,name,value);
 };
 Append(out,"storage-contract","Darwin-FXCC-v2-padded-producer/eager-MAP_JIT-consumer-v1");
#define OPT_BASE(type,group,name,json,def) add(#name,std::to_string(FEXCore::Config::Get_##name()()));
#define OPT_STR(group,name,json,def) add(#name,FEXCore::Config::Get_##name()());
#define OPT_STRARRAY(group,name,json,def) {auto value=FEXCore::Config::Get_##name();add(#name,std::to_string(value.All().size()));for(auto& v:value.All())add(#name,v);}
#include <FEXCore/Config/ConfigValues.inl>
 return BbCpuCache::Hash(out);
}

template<class Context>inline BbCpuCache::Digest ContextConfig(const Context& context){
 std::vector<std::byte> out;auto config=CanonicalConfig();
 Append(out,"canonical-effective-options",std::string_view(reinterpret_cast<const char*>(config.data()),config.size()));
 Append(out,"effective-scalar-software-TSO",std::to_string(context.IsAtomicTSOEnabled()));
 Append(out,"effective-vector-software-TSO",std::to_string(context.IsVectorAtomicTSOEnabled()));
 Append(out,"effective-memcpy-software-TSO",std::to_string(context.IsMemcpyAtomicTSOEnabled()));
 return BbCpuCache::Hash(out);
}

template<class Registry>inline BbCpuCache::Digest RegistryABI(const BbCpuCache::Digest& build,const Registry& registry,size_t frame_size,size_t frame_align,std::string_view veneer_policy="authored-direct-syscall"){
 std::vector<std::byte> out;Append(out,"compiled-bridge",std::string_view(reinterpret_cast<const char*>(build.data()),build.size()));
 Append(out,"guest-veneer-layout",veneer_policy);
 Append(out,"HleCallFrame-size",std::to_string(frame_size));Append(out,"HleCallFrame-align",std::to_string(frame_align));
 for(uint64_t operation=1;operation<=65536;++operation){
  auto* adapter=registry.FindBorrowed(operation);
  if(!adapter){Append(out,"entry-count",std::to_string(operation-1));return BbCpuCache::Hash(out);}
  Append(out,"lookup-operation",std::to_string(operation));Append(out,"adapter-operation",std::to_string(adapter->Operation()));Append(out,"name",adapter->Name());
 }
 return {}; // Refuse oversized tables rather than omit a suffix from identity.
}

}
