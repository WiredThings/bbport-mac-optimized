// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <vector>
#include <stdexcept>
// FEX's bootstrap validates sealed code and this invocation's owned stack.
// Other threads' temporary stacks can retire immediately after a global
// snapshot. Guest data remains guarded by State::valid/current VMAs and must
// not be mistaken for a process-lifetime bootstrap permission declaration.
template<class Range> std::vector<Range> ThreadBootstrapRanges(const std::vector<Range>& process,size_t permanent,uintptr_t stack,size_t size){
 if(permanent>process.size()||!stack||!size||stack>UINTPTR_MAX-size)throw std::runtime_error("invalid bootstrap ownership");
 std::vector<Range> result;result.reserve(permanent+1);
 for(size_t i=0;i<permanent;++i)if(process[i].Executable)result.push_back(process[i]);
 result.push_back({stack,size,false,true});return result;
}
