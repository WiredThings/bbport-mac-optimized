// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
// Caller holds the capture mutex. Each physical row contains at most 64 bindings.
// Full binding bytes must match a previously emitted definition before a reference
// can be used. The dictionary stores offsets, never a second copy of the metadata.
struct BbTextureTraceBatch {
    struct Definition {std::size_t row{},element{},offset{},length{};};
    std::array<Definition,1024> definitions{};
    std::vector<std::string> pending;
    std::size_t pending_bytes{};
    void Reset(){definitions={};pending.clear();pending.shrink_to_fit();pending_bytes=0;}
    static bool Append(std::vector<std::string>& rows,std::size_t& bytes,bool& truncated,std::string line){
        if(rows.size()>=8192||bytes+line.size()>2*1024*1024){truncated=true;return false;}
        bytes+=line.size();rows.push_back(std::move(line));return true;
    }
    static bool Match(const Definition& d,const std::vector<std::string>& rows,const std::string& line){
        return d.length==line.size()&&d.length&&d.row<rows.size()&&d.offset<=rows[d.row].size()&&d.length<=rows[d.row].size()-d.offset&&
            std::string_view(rows[d.row]).substr(d.offset,d.length)==line;
    }
    void Flush(std::vector<std::string>& rows,std::size_t& bytes,bool& truncated){
        if(pending.empty())return;
        std::string packed="{\"op\":\"texture_binding_batch\",\"bindings\":[";
        struct Update{std::size_t bucket;Definition value;};std::array<Update,64> updates{};std::size_t n=0;
        for(std::size_t i=0;i<pending.size();++i){
            if(i)packed+=',';
            const auto& line=pending[i];const auto bucket=std::hash<std::string>{}(line)%definitions.size();const auto& hit=definitions[bucket];
            if(Match(hit,rows,line))packed+="{\"record\":"+std::to_string(hit.row)+",\"element\":"+std::to_string(hit.element)+"}";
            else {updates[n++]={bucket,{rows.size(),i,packed.size(),line.size()}};packed+=line;}
        }
        packed+="]}";
        if(Append(rows,bytes,truncated,std::move(packed)))for(std::size_t i=0;i<n;++i)definitions[updates[i].bucket]=updates[i].value;
        pending.clear();pending_bytes=0;
    }
    void Add(std::vector<std::string>& rows,std::size_t& bytes,bool& truncated,std::string line){
        if(line.starts_with("{\"op\":\"texture_binding\"")){
            if(pending.size()>=64)Flush(rows,bytes,truncated);
            if(bytes+pending_bytes+line.size()>2*1024*1024){truncated=true;return;}
            pending_bytes+=line.size();pending.push_back(std::move(line));
        }else{Flush(rows,bytes,truncated);Append(rows,bytes,truncated,std::move(line));}
    }
};
