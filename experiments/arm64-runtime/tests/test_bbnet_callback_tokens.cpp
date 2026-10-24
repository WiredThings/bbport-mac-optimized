#include "bb_typed_adapter.h"
#include <cstdio>
#include <cstdint>
using namespace Core::GuestCpu;
static int failed,calls,range_checks;static uintptr_t observed;static void(*code)();
static void check(bool v,const char*n){printf("%s %s\n",v?"PASS":"FAIL",n);failed+=!v;}
static int push(int,int,void(*fn)(),void*arg){++calls;observed=reinterpret_cast<uintptr_t>(arg);code=fn;return 73;}
static int notify(int,void(*fn)(),void*arg){return push(0,0,fn,arg);}
static int output(int,int,char*,char*){++calls;return 0;}
static bool ok(const HleCallResult&r){return std::holds_alternative<bool>(r)&&std::get<bool>(r);}
static HleCallFrame frame(){HleCallFrame f{};f.validate_range=[](void*,uintptr_t,size_t,bool){++range_checks;return false;};return f;}
int main(){
 const char*names[]={"sceNpWebApiRegisterPushEventCallback","sceNpWebApiRegisterServicePushEventCallback","sceNpWebApiRegisterExtdPushEventCallback","sceNpWebApiRegisterExtdPushEventCallbackA","sceNpWebApiIntRegisterServicePushEventCallback","sceNpWebApiIntRegisterServicePushEventCallbackA","sceNpWebApi2PushEventRegisterCallback","sceNpWebApi2PushEventRegisterPushContextCallback"};
 for(const char*name:names){
  BbNet::TypedAdapter<decltype(&push)> adapter(push,BbNet::OpaqueMask(name));
  for(uintptr_t tag:{uintptr_t(0),uintptr_t(1),uintptr_t(0xdeadbeef),UINTPTR_MAX}){
   auto f=frame();f.gpr[7]=11;f.gpr[6]=22;f.gpr[2]=0x12345678;f.gpr[1]=tag;int n=calls,checks=range_checks;
   check(ok(adapter.Invoke(f))&&calls==n+1&&observed==tag&&reinterpret_cast<uintptr_t>(code)==0x12345678&&f.gpr[0]==73&&range_checks==checks,name);
  }
 }
 BbNet::TypedAdapter<decltype(&notify)> notification(notify,BbNet::OpaqueMask("sceNpWebApiRegisterNotificationCallback"));
 auto f=frame();f.gpr[7]=11;f.gpr[6]=0x12345678;f.gpr[2]=1;int n=calls;
 check(ok(notification.Invoke(f))&&calls==n+1&&observed==1,"notification arg2 opaque token");
 // Real buffers stay checked, even in an API which also has opaque user data.
 BbNet::TypedAdapter<decltype(&output)> buffers(output,BbNet::OpaqueMask(names[0]));f=frame();f.gpr[2]=1;f.gpr[1]=1;n=calls;
 auto result=buffers.Invoke(f);auto*e=std::get_if<HleCallFailure>(&result);
 check(e&&e->error==EFAULT&&calls==n,"non-opaque data pointer rejected before host");
 BbNet::TypedAdapter<decltype(&push)> unknown(push,BbNet::OpaqueMask("sceNpWebApiRegisterPushEventCallback_suffix"));f=frame();f.gpr[2]=0x12345678;f.gpr[1]=1;n=calls;
 result=unknown.Invoke(f);e=std::get_if<HleCallFailure>(&result);check(e&&e->error==EFAULT&&calls==n,"unknown API name gains no opaque bypass");
 printf("callback-token failures=%d\n",failed);return failed?1:0;
}
