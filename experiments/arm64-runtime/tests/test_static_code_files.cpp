#include "bb_static_code_files.h"
#include "core/guest_cpu/fex_hle_bridge.h"
#include <cstdio>
#include <cstdlib>
struct FakeBoot{struct S{uint64_t address,size,flags;};struct R{uint64_t target;};size_t size=16384;std::filesystem::path path;std::vector<S> segments{{0,16384,5}};std::vector<R> relocs;};
int main(int argc,char**argv){
 if(argc!=2)return 2;std::filesystem::path out=argv[1];if(std::filesystem::exists(out))return 2;std::filesystem::create_directory(out);
 FakeBoot boot;boot.path=out/"authored-source.bin";{std::ofstream f(boot.path);f<<"authored source only";}
 std::vector<std::byte> bytes(boot.size,std::byte(0x90));unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok){std::printf("static-code failure=%u\n",checks);std::exit(1);}};
 BbStaticCode::Files a(boot,0x8000000000,bytes);check(!a.Query(0x8000001000));bytes[0x100]=std::byte(1);bytes[0x2100]=std::byte(2);a.Finalize(bytes);
 check(a.ModifiedPages()==2&&a.RelocationPages()==0&&a.EligibleBytes()==8192&&a.RetainedHashBytes()==0);
 check(!a.Query(0x8000000000)&&a.Query(0x8000001000)&&!a.Query(0x8000002000)&&a.Query(0x8000003000));
 check(!a.Query(0x7fffffffff)&&!a.Query(0x8000004000)&&!a.Query(UINTPTR_MAX));
 const auto first=a.Query(0x8000001fff);check(first&&first->BeginVA==0x8000001000&&first->EndVA==0x8000002000&&first->FileStartVA==0x8000000000);
 std::vector<std::byte> clean(boot.size,std::byte(0x90));BbStaticCode::Files b(boot,0x9000000000,clean);clean[0x100]=std::byte(1);clean[0x2100]=std::byte(2);b.Finalize(clean);check(a.Identity()==b.Identity());
 boot.relocs={{4092}};std::vector<std::byte> rel(boot.size,std::byte(0x90));BbStaticCode::Files c(boot,0xa000000000,rel);c.Finalize(rel);check(c.RelocationPages()==2&&c.EligibleBytes()==8192&&!c.Query(0xa000000000)&&!c.Query(0xa000001000)&&c.Query(0xa000002000));
 auto changed=rel;changed[0x2100]=std::byte(9);BbStaticCode::Files d(boot,0xa000000000,changed);d.Finalize(changed);check(d.Identity()!=c.Identity());
 bool rejected=false;try{a.Finalize(bytes);}catch(const std::exception&){rejected=true;}check(rejected);
 auto callback=+[](void* ctx,uintptr_t at){return static_cast<BbStaticCode::Files*>(ctx)->Query(at);};Core::GuestCpu::HleCallRegistry registry;
 Core::GuestCpu::HleGuestBridge bridge(registry,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,callback,&a);
 check(bridge.QueryCodeFile(0x8000001000)&&!bridge.QueryCodeFile(0x8000000000));
 Core::GuestCpu::HleGuestBridge disabled(registry,nullptr,nullptr);check(!disabled.QueryCodeFile(0x8000001000)&&!disabled.CodeMapOutput());
 a.Report(out/"manifest.json");std::printf("STATIC_CODE_FILES checks=%u passed=1\n",checks);
}
