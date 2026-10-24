// SPDX-License-Identifier: GPL-3.0-or-later
#include "resource_query.h"
#include <cassert>
#include <string>
#include <cstdio>
int main(){using namespace BbResourceQuery;const std::string nonce(32,'a');Request output;output.sequence=99;
 const std::string prefix="ARMRESOURCE1 "+nonce+" ";
 assert(Parse(prefix+"1 2 481036337152 1048576 481037385728 128\n",nonce,output)&&output.count==2&&output.sequence==1);
 const auto before=output;
 for(auto bad:{"0 1 1 1","33 1 1 1","1 0","1 25","1 1 0 1","1 1 1 0","1 1 -1 8","1 1 +1 8","1 1 1099511627776 8","1 1 1099511627775 2","1 1 1 4294967297","1 1 1 8 extra","1 1 1","1 1 18446744073709551616 8"}){assert(!Parse(prefix+bad,nonce,output));assert(output.sequence==before.sequence&&output.count==before.count);}
 assert(!Parse(prefix+"1 1 1 8",std::string(32,'b'),output));assert(!Parse(std::string(4097,'x'),nonce,output));assert(!Parse(prefix+"1 1 1 8",std::string(32,'z'),output));
 assert(Parse(prefix+"32 1 1095216660480 4294967296",nonce,output));puts("RESOURCE QUERY bounded grammar/nonce/range/overflow rejection passed; invalid input never commits");}
