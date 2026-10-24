// SPDX-License-Identifier: MIT
#ifdef BB_ABI_ORIGINAL
#include "baseline_hle_call_adapter.h"
namespace Core::GuestCpu {
template<class T>struct GuestAggregate{};
template<class T>struct GuestVector128:std::false_type{};
template<class T,std::size_t O>struct GuestField{};
}
#else
#include "core/guest_cpu/hle_call_adapter.h"
#endif
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace Core::GuestCpu;
struct Pair{u64 a,b;};struct Doubles{double a,b;};struct Mixed{double a;u64 b;};
struct Floats{float a,b;};struct Large{u64 a,b,c;};struct Unknown{u64 a,b;};
struct __attribute__((packed)) Packed{char a;double b;};
struct alignas(16) Vector{u64 a,b;};
namespace Core::GuestCpu {
#define FIELD(T,F) GuestField<decltype(T::F),offsetof(T,F)>{}
#define LAYOUT(T,...) template<>struct GuestAggregate<T>{static constexpr bool enabled=true;static constexpr auto fields=std::tuple{__VA_ARGS__};};
LAYOUT(Pair,FIELD(Pair,a),FIELD(Pair,b))
LAYOUT(Doubles,FIELD(Doubles,a),FIELD(Doubles,b))
LAYOUT(Mixed,FIELD(Mixed,a),FIELD(Mixed,b))
LAYOUT(Floats,FIELD(Floats,a),FIELD(Floats,b))
LAYOUT(Large,FIELD(Large,a),FIELD(Large,b),FIELD(Large,c))
LAYOUT(Packed,FIELD(Packed,a),FIELD(Packed,b))
template<>struct GuestVector128<Vector>:std::true_type{};
}
static int passed,failed,entered;
static void check(bool v,const char*n){std::printf("%s %s\n",v?"PASS":"FAIL",n);if(v)++passed;else ++failed;}
static u64 bits(double d){u64 x;std::memcpy(&x,&d,8);return x;}
struct Test{
 alignas(16) u64 stack[64]{};HleCallFrame f{};
 Test(){f.rsp=(uintptr_t)stack+8;f.validate_context=this;f.validate_range=[](void*p,uintptr_t a,size_t n,bool){auto*t=(Test*)p;auto b=(uintptr_t)t->stack;return a>=b&&a<=b+sizeof(t->stack)&&n<=b+sizeof(t->stack)-a;};}
 template<class T>void spill(T x,size_t offset=8){std::memcpy((void*)(f.rsp+offset),&x,sizeof(x));}
 template<class F>bool run(F fn){auto r=MakeHleCallAdapter(fn)->Invoke(f);return std::holds_alternative<bool>(r)&&std::get<bool>(r);}
};
static u64 pair_sum(Pair p){return p.a+p.b;}
static double doubles_sum(Doubles p){return p.a+p.b;}
static double mixed_sum(Mixed p){return p.a+p.b;}
static double floats_sum(Floats p){return p.a+p.b;}
static u64 vector_sum(Vector p){return p.a+p.b;}
static u64 large_sum(Large p){return p.a+p.b+p.c;}
static double packed_sum(Packed p){return p.a+p.b;}
static u64 unknown_sum(Unknown p){++entered;return p.a+p.b;}
static u64 rollback(u64 a,u64 b,u64 c,u64 d,u64 e,Pair p,u64 last){return a+b+c+d+e+p.a+p.b+last;}
static double mixed_rollback(u64,u64,u64,u64,u64,u64,Mixed p,double last){return p.a+p.b+last;}
static double sse_spill(double,double,double,double,double,double,double,double,Doubles p,u64 last){return p.a+p.b+last;}
static u64 vector_spill(double,double,double,double,double,double,double,double,Vector p,u64 n){return p.a+p.b+n;}
static Pair return_pair(u64 a,u64 b){return {a,b};}
static Doubles return_doubles(double a,double b){return {a,b};}
static Mixed return_mixed(double a,u64 b){return {a,b};}
static u64 pointer_value(const u64*p){++entered;return p?*p:0;}
int main(){
 {Test t;t.f.gpr[7]=3;t.f.gpr[6]=5;check(t.run(+[](u64 a,u64 b){return a+b;})&&t.f.gpr[0]==8,"scalar control");}
 {Test t;t.f.xmm[0][0]=bits(1.25);t.f.xmm[1][0]=bits(2.75);check(t.run(+[](double a,double b){return a+b;})&&t.f.xmm[0][0]==bits(4),"double control");}
 {Test t;t.f.gpr[7]=3;t.f.gpr[6]=5;t.f.xmm[0]={101,103};check(t.run(pair_sum)&&t.f.gpr[0]==8,"two integer eightbytes");}
 {Test t;t.f.xmm[0]={bits(1.25),bits(100)};t.f.xmm[1][0]=bits(2.75);check(t.run(doubles_sum)&&t.f.xmm[0][0]==bits(4),"two SSE eightbytes");}
 {Test t;t.f.gpr[7]=5;t.f.xmm[0][0]=bits(2);check(t.run(mixed_sum)&&t.f.xmm[0][0]==bits(7),"mixed SSE integer");}
 {Test t;Floats p{1.5,2.5};std::memcpy(&t.f.xmm[0][0],&p,8);check(t.run(floats_sum)&&t.f.xmm[0][0]==bits(4),"two floats in one eightbyte");}
 {Test t;t.f.xmm[0]={3,5};check(t.run(vector_sum)&&t.f.gpr[0]==8,"explicit vector stays one XMM");}
 {Test t;t.f.gpr[7]=3;t.f.gpr[6]=5;entered=0;auto r=MakeHleCallAdapter(unknown_sum)->Invoke(t.f);check(std::holds_alternative<HleCallFailure>(r)&&entered==0,"unknown aggregate rejected before host call");}
 {Test t;for(int i=0;i<5;++i)t.f.gpr[detail::SysVIntegerArgs[i]]=1;t.f.gpr[9]=42;t.spill(Pair{3,5});check(t.run(rollback)&&t.f.gpr[0]==55,"GPR exhaustion rolls whole struct to stack");}
 {Test t;t.spill(Mixed{2,5});t.f.xmm[0][0]=bits(11);check(t.run(mixed_rollback)&&t.f.xmm[0][0]==bits(18),"mixed rollback leaves SSE register available");}
 {Test t;t.spill(Doubles{1.25,2.75});t.f.gpr[7]=7;check(t.run(sse_spill)&&t.f.xmm[0][0]==bits(11),"SSE exhaustion leaves integer register available");}
 {Test t;t.spill(Vector{3,5});t.f.gpr[7]=7;check(t.run(vector_spill)&&t.f.gpr[0]==15,"spilled vector stack alignment");}
 {Test t;t.spill(Large{1,2,3});check(t.run(large_sum)&&t.f.gpr[0]==6,"registered memory aggregate");}
 {Test t;Packed p{2,3.5};t.spill(p);check(t.run(packed_sum)&&t.f.xmm[0][0]==bits(5.5),"unaligned member forces MEMORY");}
 {Test t;t.f.gpr[7]=3;t.f.gpr[6]=5;check(t.run(return_pair)&&t.f.gpr[0]==3&&t.f.gpr[2]==5,"aggregate RAX RDX return");}
 {Test t;t.f.xmm[0][0]=bits(1.25);t.f.xmm[1][0]=bits(2.75);check(t.run(return_doubles)&&t.f.xmm[0][0]==bits(1.25)&&t.f.xmm[1][0]==bits(2.75),"aggregate XMM0 XMM1 return");}
 {Test t;t.f.xmm[0][0]=bits(2);t.f.gpr[7]=5;check(t.run(return_mixed)&&t.f.xmm[0][0]==bits(2)&&t.f.gpr[0]==5,"mixed aggregate return");}
 {Test t;t.f.validate_range=nullptr;t.spill(Pair{3,5});check(!t.run(rollback),"spilled argument without range validator rejected");}
 {Test t;t.f.rsp=UINTPTR_MAX-1;check(!t.run(rollback),"stack overflow rejected");}
 {Test t;t.f.gpr[7]=0;entered=0;check(t.run(pointer_value)&&entered==1&&t.f.gpr[0]==0,"null pointer control");}
 {Test t;t.f.gpr[7]=1;entered=0;check(!t.run(pointer_value)&&entered==0,"invalid data pointer rejected");}
 std::printf("ABI tests passed=%d failed=%d\n",passed,failed);return failed?1:0;
}
