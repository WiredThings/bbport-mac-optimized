#pragma once
// Mirrors this frozen FEX serializer only. This is not a general cache-file API.
struct Header {
 char magic[4]; uint32_t version; uint8_t fex[20];
 uint32_t blocks,pages,code_bytes,relocs,pad; uint64_t base;
};
static_assert(sizeof(Header)==56);
static bool validate(std::span<const std::byte> s) {
 Header h{}; if(s.size()<sizeof(h)||s.size()>1024*1024)return false;
 std::memcpy(&h,s.data(),sizeof(h));
 if(std::memcmp(h.magic,"FXCC",4)||h.version!=2||h.base!=0||h.pad||
    !h.blocks||h.blocks>256||h.pages>256||!h.code_bytes||h.code_bytes>512*1024||h.code_bytes%4096||h.relocs>8192)return false;
 size_t at=sizeof(h); auto read=[&](uint64_t& x){if(at>s.size()||s.size()-at<8)return false;std::memcpy(&x,s.data()+at,8);at+=8;return true;};
 std::set<uint64_t> blocks;
 uint64_t last=0;
 for(uint32_t i=0;i<h.blocks;++i){
  uint64_t guest,host,n;if(!read(guest)||!read(host)||!read(n)||guest>=4096||host>=h.code_bytes||host%4||!n||n>4||
    (i&&guest<=last))return false;
  last=guest;blocks.insert(guest);
  uint64_t prev=0;for(uint64_t j=0;j<n;++j){uint64_t page;if(!read(page)||page>=16384||page%4096||(j&&page<=prev))return false;prev=page;}
 }
 uint64_t prev=0;
 for(uint32_t i=0;i<h.relocs;++i){
  if(at>s.size()||s.size()-at<48)return false;
  uint64_t offset;uint32_t kind;std::memcpy(&offset,s.data()+at,8);std::memcpy(&kind,s.data()+at+8,4);
  if(kind!=0&&kind!=2&&kind!=3)return false; // no host thunk addresses in this fixture
  size_t width=kind==3?16:8;
  if(offset%4||offset>h.code_bytes||h.code_bytes-offset<width||(i&&offset<prev))return false;
  if(kind==0){uint32_t symbol;std::memcpy(&symbol,s.data()+at+12,4);if(symbol!=0)return false;}
  else {uint64_t rip;std::memcpy(&rip,s.data()+at+16,8);if(rip>=16384)return false;if(kind==3&&std::to_integer<unsigned>(s[at+12])>30)return false;}
  prev=offset;at+=48;
 }
 if(at>SIZE_MAX-4095)return false;at=(at+4095)&~size_t(4095);
 if(at>s.size()||s.size()-at<h.code_bytes)return false;at+=h.code_bytes;
 std::set<uint64_t> registered;
 for(uint32_t i=0;i<h.pages;++i){uint64_t page,n;if(!read(page)||!read(n)||page>=16384||page%4096||n>256)return false;
  for(uint64_t j=0;j<n;++j){uint64_t entry;if(!read(entry)||!blocks.contains(entry))return false;registered.insert(entry);}}
 return at==s.size()&&registered==blocks;
}

static bool digest_ok(std::span<const std::byte> s,const char* expected){
 unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256(s.data(),static_cast<CC_LONG>(s.size()),hash);
 char hex[65];for(size_t i=0;i<32;++i)std::snprintf(hex+2*i,3,"%02x",hash[i]);return std::string_view(hex)==expected;
}

