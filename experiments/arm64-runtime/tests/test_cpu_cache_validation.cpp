#include "bb_cpu_cache_validation.h"
#include <cassert>
#include <cstdio>
using namespace BbCacheValidation;
int main(){
 std::array<uint8_t,20> fex{};std::array<std::pair<uint64_t,uint64_t>,1> sections{{{0,4096}}};Policy p{16384,sections,fex};
 std::vector<std::byte> good(4096+4096+24);auto put=[&](auto& b,size_t at,uint64_t x,size_t n){memcpy(b.data()+at,&x,n);};
 memcpy(good.data(),"FXCC",4);for(auto [at,x]:std::vector<std::pair<size_t,uint64_t>>{{4,2},{28,1},{32,1},{36,4096},{40,1},{56,32},{64,4},{72,1},{80,0},{88,16},{96,2},{104,1234},{4096+16,1234},{8192,0},{8200,1},{8208,32}})put(good,at,x,(at>=4&&at<=40)?4:8);
 // Relocation kind is32bits; the preceding generic initializer writes a zero register too.
 assert(Validate(good,p));unsigned checks=1;
 auto bad=[&](size_t at,uint64_t value,size_t width){auto b=good;put(b,at,value,width);assert(!Validate(b,p));++checks;};
 bad(4,3,4);bad(8,1,1);bad(28,0,4);bad(28,262145,4);bad(32,0,4);bad(36,1,4);bad(40,349526,4);bad(44,1,4);bad(48,1,8);
 bad(56,4096,8);bad(64,3,8);bad(72,0,8);bad(80,4096,8);bad(88,4092,8);bad(96,1,4);bad(104,16384,8);bad(136,1,1);bad(4112,1235,8);bad(8192,4096,8);bad(8200,2,8);bad(8208,33,8);
 for(auto n:{size_t(55),size_t(100),good.size()-1}){assert(!Validate(std::span(good).first(n),p));++checks;}
 auto tail=good;tail.push_back(std::byte{});assert(!Validate(tail,p));++checks;
 auto duplicate=good;duplicate.resize(good.size()+8);put(duplicate,8200,2,8);put(duplicate,8216,32,8);assert(Validate(duplicate,p));++checks;
 auto wrong=p;wrong.image_bytes=0;assert(!Validate(good,wrong));++checks;
 std::array<std::byte,16> move{};auto instruction=[&](int at,uint32_t x){memcpy(move.data()+at,&x,4);};
 for(int i=0;i<16;i+=4)instruction(i,0xd503201f);
 instruction(0,0xd2800004|(0x1234<<5));assert(DecodeMove(move,4)==0x1234);++checks;
 instruction(4,0xf2a00004|(0xabcd<<5));assert(DecodeMove(move,4)==0xabcd1234ULL);++checks;
 assert(!DecodeMove(move,18));++checks;instruction(0,0x92800004);instruction(4,0xd503201f);assert(DecodeMove(move,4)==~uint64_t{});++checks;
 instruction(0,0xb24003e4);assert(DecodeMove(move,4)==1);++checks;
 instruction(0,0xffffffff);assert(!DecodeMove(move,4));++checks;
 printf("CACHE_VALIDATION checks=%u passed=1\n",checks);
}
