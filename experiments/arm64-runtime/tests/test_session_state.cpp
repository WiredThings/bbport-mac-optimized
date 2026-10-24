// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_state.h"
#include <cassert>
#include <unistd.h>
int main(){
 namespace fs=std::filesystem;auto root=fs::temp_directory_path()/("bb-session-unit-"+std::to_string(getpid()));assert(fs::create_directory(root));auto state=root/"qa";
 auto refused=[&](auto fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}assert(failed);};
 BbSession::Prepare(state,root,"fixed boot and session identity\n",false);std::ofstream(state/"progress")<<"keep me";
 refused([&]{BbSession::Prepare(state,root,"fixed boot and session identity\n",false);});
 refused([&]{BbSession::Prepare(state,root,"other boot/session\n",true);});
 BbSession::Prepare(state,root,"fixed boot and session identity\n",true);
 assert(fs::file_size(state/"progress")==7);
 fs::rename(state/".arm-runtime-session",root/"marker");fs::create_symlink(root/"marker",state/".arm-runtime-session");
 refused([&]{BbSession::Prepare(state,root,"fixed boot and session identity\n",true);});assert(fs::file_size(state/"progress")==7);
 auto link=root/"linked";fs::create_directory_symlink(state,link);refused([&]{BbSession::Prepare(link,root,"fixed boot and session identity\n",true);});
 fs::remove_all(root);puts("SESSION fresh refusal, identity, resume preservation and symlink checks PASS");
}
