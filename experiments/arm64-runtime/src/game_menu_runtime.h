#include "runtime_exit_codes.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the original CPU loader's State namespace. No independent menu.
namespace ArmGameMenu {
using MenuABI=BbGameMenu::CallbackABI;
std::map<std::pair<uintptr_t,MenuABI>,uintptr_t> callbacks;
bool sealed{};const Boot* metadata{};std::unique_ptr<Mapping>* owner{};
class Guarded final:public HleCallAdapter {
 std::shared_ptr<HleCallAdapter> inner;
public:explicit Guarded(std::shared_ptr<HleCallAdapter> value):inner(std::move(value)){}
 HleCallResult Invoke(HleCallFrame& frame)const override {
  try{return inner->Invoke(frame);}catch(const std::exception& e){fprintf(stderr,"ARM MENU refused invalid boundary: %s\n",e.what());_Exit(BB_ARM_EXIT_HLE_FAULT);}
 }
};
uint64_t Callback(void* fn,MenuABI abi){
 const auto key=std::pair{reinterpret_cast<uintptr_t>(fn),abi};
 if(auto it=callbacks.find(key);it!=callbacks.end())return it->second;
 check(!sealed&&fn,"menu callback publication after startup refused");
 std::shared_ptr<HleCallAdapter> a;
 switch(abi){
 case MenuABI::Rows:a=MakeHleCallAdapter(reinterpret_cast<void(*)(void*,void*)>(fn));break;
 case MenuABI::TwoPointers:a=MakeHleCallAdapter(reinterpret_cast<void*(*)(void*,void*)>(fn));break;
 case MenuABI::Lookup:a=MakeHleCallAdapter(reinterpret_cast<const char16_t*(*)(void*,uint32_t,uint32_t,uint32_t)>(fn));break;
 case MenuABI::Add:a=MakeHleCallAdapter(reinterpret_cast<void*(*)(void*,void*,void*,void*)>(fn));break;
 default:throw std::runtime_error("unknown menu callback MenuABI");
 }
 auto guarded=std::make_shared<Guarded>(framed(a));
 const auto v=veneer(*active,active->registry.Register(guarded,"game.menu."+std::to_string(callbacks.size())));callbacks.emplace(key,v);return v;
}
uint64_t Call(uint64_t fn,const uint64_t*args,uint64_t count){check(count<=6,"menu guest arity");return bb_arm_call(fn,args,size_t(count));}
void* Allocate(uint64_t n){check(n&&n<=65536,"menu guest allocation size");return bb_low_aligned_alloc(16,(n+15)&~uint64_t(15));}
void Release(void*p){bb_low_free(p);}
bool Readable(const void*p,uint64_t n){return active&&n&&n<=SIZE_MAX&&active->valid(reinterpret_cast<uintptr_t>(p),size_t(n),false);}
bool InstallHooks(unsigned char*image,uint64_t size,BbGameMenu::GuestHook*hooks,uint64_t count){
 check(metadata&&owner&&image==reinterpret_cast<unsigned char*>(active->image)&&size==active->image_size&&count==2,"menu startup ownership");
 for(size_t i=0;i<count;++i)check(metadata->covered(hooks[i].offset,hooks[i].length,true),"menu hook not guest executable");
 auto mapping=std::make_unique<Mapping>(Page);
 const auto plan=BbArmMenu::Prepare({image,size},active->image,mapping->at(),mapping->size,{hooks,size_t(count)});
 memcpy(mapping->ptr,plan.stubs.data(),plan.stubs.size());check(!mprotect(mapping->ptr,mapping->size,PROT_READ),"menu stubs read-only protection");
 // All checks and allocation complete before either image site is changed.
 active->ranges.push_back({mapping->at(),mapping->size,true,false});
 for(size_t i=0;i<count;++i)hooks[i].original=plan.originals[i];
 for(auto&patch:plan.patches)memcpy(image+patch.offset,patch.bytes.data(),patch.bytes.size());
 *owner=std::move(mapping);return true;
}
void Install(const Boot&boot,Mapping&image,std::unique_ptr<Mapping>&stubs){
 const char* on=getenv("BB_GAME_MENU");if(on&&on[0]=='0')return;
 metadata=&boot;owner=&stubs;
 const BbGameMenu::GuestBridge bridge{Call,Callback,Allocate,Release,Readable,InstallHooks};
 check(BbGameMenu::SetGuestBridge(bridge),"menu architecture bridge setup failed");
 BbGameMenu::PatchImage(static_cast<unsigned char*>(image.ptr),boot.size);
 check(bool(stubs),"requested game System menu hooks not installed");
 sealed=true;metadata=nullptr;owner=nullptr;
 printf("ARM MENU original System pages bridged; callbacks=%zu; guest data stays in existing low heap; no JIT/cache budget change\n",callbacks.size());
}
}
