#include "bb_hardware_tso.h"
#include <cassert>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <string>
static std::atomic<unsigned> attempts{0};
static int yes(uint32_t value){assert(value==1);++attempts;return 0;}
static int no(uint32_t){++attempts;return 5;}
static int first_only(uint32_t){return ++attempts==1?0:5;}
int main(int argc,char** argv){
 std::string mode=argc>1?argv[1]:"all";
 if(mode=="real"){auto s=BbTSO::Initialize();BbTSO::Admit();printf("real selected=%u software=%u\n",s,s!=2);return 0;}
 for(unsigned requested:{0u,1u,2u,3u}){attempts=0;BbTSO::Policy p(requested,no);assert(p.Mode()==0);std::thread t([&]{p.Admit();});t.join();assert(attempts==(requested==1||requested==2?1u:0u));}
 for(unsigned requested:{1u,2u}){attempts=0;BbTSO::Policy p(requested,yes);assert(p.Mode()==requested);p.Admit();
 std::vector<std::thread> ts;for(int i=0;i<8;i++)ts.emplace_back([&]{for(int n=0;n<20;n++)p.Admit();});for(auto& t:ts)t.join();assert(attempts==9&&p.Attempts()==9);}
 for(unsigned requested:{1u,2u}){pid_t child=fork();assert(child>=0);if(!child){attempts=0;BbTSO::Policy p(requested,first_only);std::thread t([&]{p.Admit();});t.join();_exit(9);}int status{};waitpid(child,&status,0);assert(WIFEXITED(status)&&WEXITSTATUS(status)==190);}
 puts("TSO startup fallback, per-host-thread admission, cached re-entry and fatal late failure passed");
}
