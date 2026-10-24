// Shared isolated prototype: cache identity primitives, not enabled in the game.
#pragma once
#include <CommonCrypto/CommonDigest.h>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
namespace BbCpuCache {
using Digest=std::array<unsigned char,CC_SHA256_DIGEST_LENGTH>;
inline Digest Hash(std::span<const std::byte> bytes){
 CC_SHA256_CTX context;CC_SHA256_Init(&context);
 constexpr size_t chunk=1024*1024;
 while(!bytes.empty()){const auto n=std::min(chunk,bytes.size());CC_SHA256_Update(&context,bytes.data(),static_cast<CC_LONG>(n));bytes=bytes.subspan(n);}
 Digest out{};CC_SHA256_Final(out.data(),&context);return out;
}
// Every digest must describe final content/configuration, not a path or process address.
// runtime_build must cover local translator/bridge changes (upstream git hash alone is insufficient).
struct Identity {
 Digest runtime_build,patched_code,module_set,bridge_abi,host_features,codegen_config;
 enum class Ordering:uint8_t{Software=0,Hardware=1};Ordering ordering=Ordering::Software;
};
inline bool Valid(const Identity& id){
 if(id.ordering!=Identity::Ordering::Software&&id.ordering!=Identity::Ordering::Hardware)return false;
 for(const auto* field:{&id.runtime_build,&id.patched_code,&id.module_set,&id.bridge_abi,&id.host_features,&id.codegen_config})
  if(std::all_of(field->begin(),field->end(),[](auto b){return b==0;}))return false;
 return true;
}
inline Digest Key(const Identity& id){
 if(!Valid(id))return {};
 // Fixed-width, explicitly tagged fields; not raw padded C++ structs.
 std::array<std::byte,16+6*33+2> bytes{};const char domain[]="BE-CPU-CACHE-v1";
 std::memcpy(bytes.data(),domain,sizeof(domain));size_t at=16;unsigned tag=1;
 for(const auto* field:{&id.runtime_build,&id.patched_code,&id.module_set,&id.bridge_abi,&id.host_features,&id.codegen_config}){
  bytes[at++]=std::byte(tag++);std::memcpy(bytes.data()+at,field->data(),field->size());at+=field->size();
 }
 bytes[at++]=std::byte(7);bytes[at++]=std::byte(static_cast<uint8_t>(id.ordering));return Hash(bytes);
}
// Prefix aligns the embedded FXCC payload to its 4KiB serialization boundary.
// Consumer must also validate payload structure before calling the FEX loader.
constexpr size_t PrefixBytes=4096,MaxPayload=16*1024*1024;
inline std::array<std::byte,PrefixBytes> Prefix(std::span<const std::byte> payload,const Identity& id){
 std::array<std::byte,PrefixBytes> result{};if(!Valid(id)||payload.empty()||payload.size()>MaxPayload)return result;
 const char magic[]="BECC0001";std::memcpy(result.data(),magic,8);
 uint64_t length=payload.size();for(unsigned i=0;i<8;++i)result[8+i]=std::byte((length>>(8*i))&255);
 auto key=Key(id),content=Hash(payload);std::memcpy(result.data()+16,key.data(),32);std::memcpy(result.data()+48,content.data(),32);return result;
}
inline std::span<const std::byte> Check(std::span<const std::byte> file,const Identity& expected){
 if(!Valid(expected)||file.size()<PrefixBytes||std::memcmp(file.data(),"BECC0001",8))return {};
 uint64_t length=0;for(unsigned i=0;i<8;++i)length|=uint64_t(std::to_integer<unsigned>(file[8+i]))<<(8*i);
 if(!length||length>MaxPayload||length!=file.size()-PrefixBytes)return {};
 for(size_t i=80;i<PrefixBytes;++i)if(file[i]!=std::byte{})return {};
 const auto key=Key(expected);if(std::memcmp(file.data()+16,key.data(),key.size()))return {};
 auto payload=file.subspan(PrefixBytes);const auto content=Hash(payload);
 if(std::memcmp(file.data()+48,content.data(),content.size()))return {};return payload;
}
}
