// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace BbSession {
inline void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
inline void Prepare(const std::filesystem::path& state,const std::filesystem::path& root,const std::string& identity,bool resume){
 namespace fs=std::filesystem;
 check(fs::canonical(state.parent_path())==fs::canonical(root),"state outside prototype");
 auto marker=state/".arm-runtime-session";
 if(resume){
  check(!fs::is_symlink(state)&&fs::is_directory(state)&&!fs::is_symlink(marker)&&fs::is_regular_file(marker),"private resume session missing or linked");
  check(fs::file_size(marker)==identity.size(),"resume session identity size mismatch");
  std::ifstream f(marker);std::string actual((std::istreambuf_iterator<char>(f)),{});check(actual==identity,"resume session identity mismatch");
 } else {
  check(fs::create_directory(state),"state must be fresh");fs::permissions(state,fs::perms::owner_all,fs::perm_options::replace);
  std::ofstream f(marker);f<<identity;f.flush();check(bool(f),"private session marker write failed");
 }
}
}
