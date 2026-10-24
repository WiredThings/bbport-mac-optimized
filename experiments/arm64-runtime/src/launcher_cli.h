// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <string_view>
#include <span>
#include <charconv>
#include <stdexcept>
namespace BbLauncherCli {
struct Options{std::filesystem::path boot,user,app0,content,patches;bool graphics=true,strict_imports=false;unsigned timeout=10;};
inline Options Parse(std::span<const std::string_view> args){
 auto require=[](bool b,const char*m){if(!b)throw std::runtime_error(m);};require(!args.empty()&&!args[0].starts_with("--"),"launcher boot argument missing");Options out;out.boot=args[0];
 bool user=false,app0=false,content=false,patches=false,timeout=false,cpu=false,strict=false;
 for(size_t i=1;i<args.size();++i){const auto key=args[i];if(key=="--strict-imports"){require(!strict,"duplicate strict-imports");strict=true;out.strict_imports=true;continue;}if(key=="--cpu-only"){require(!cpu,"duplicate cpu-only");cpu=true;out.graphics=false;continue;}
  require(i+1<args.size(),"launcher option value missing");auto value=args[++i];require(!value.empty()&&!value.starts_with("--"),"launcher option value missing");
  if(key=="--user"){require(!user,"duplicate launcher user");user=true;out.user=value;}
  else if(key=="--app0"){require(!app0,"duplicate launcher app0");app0=true;out.app0=value;}
  else if(key=="--content-profile"){require(!content,"duplicate launcher content");content=true;out.content=value;}
  else if(key=="--patches"){require(!patches,"duplicate launcher patches");patches=true;out.patches=value;}
  else if(key=="--timeout"){require(!timeout,"duplicate launcher timeout");timeout=true;auto r=std::from_chars(value.data(),value.data()+value.size(),out.timeout);require(r.ec==std::errc{}&&r.ptr==value.data()+value.size()&&out.timeout<=600,"invalid private launcher timeout");}
  else throw std::runtime_error("unsupported launcher argument");
 }
 require(user&&app0&&content&&patches,"launcher requires user, app0, content and patches");return out;
}
// Use the same parser as game_main: a normal graphical launcher invocation has
// no --graphics flag. Value strings must never be mistaken for mode switches.
inline bool NeedsMainThread(std::span<const std::string_view> args) {
 if(!args.empty()&&!args.front().starts_with("--"))return Parse(args).graphics;
 for(size_t i=0;i<args.size();++i) {
  const auto key=args[i];
  if(key=="--graphics")return true;
  if(key=="--boot"||key=="--state"||key=="--settings-template"||key=="--startup-patches"||key=="--seed-user"||key=="--app0"||key=="--content-profile")++i;
 }
 return false;
}
}
