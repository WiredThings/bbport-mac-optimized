#include "bbport_texture_trace_batch.h"
#include <cassert>
int main(){
 BbTextureTraceBatch b;std::vector<std::string> rows;std::size_t bytes=0;bool truncated=false;
 const std::string A=R"({"op":"texture_binding","uid":1})",B=R"({"op":"texture_binding","uid":2})";
 const std::string set=R"({"op":"texture_set"})";
 auto add=[&](auto line){b.Add(rows,bytes,truncated,line);};
 add(set);add(A);add(B);add(set);add(A);add(B);add(set);
 assert(!truncated&&rows.size()==5&&rows[3].find("\"record\":1,\"element\":0")!=std::string::npos);
 // Deliberately point every hash bucket at the wrong, equally sized definition.
 auto source=b.definitions[std::hash<std::string>{}(A)%b.definitions.size()];
 b.definitions.fill(source);add(B);add(set);assert(rows[5].find(B)!=std::string::npos);
 b.Flush(rows,bytes,truncated);std::size_t sum=0;for(const auto& row:rows)sum+=row.size();assert(sum==bytes);
 b.Reset();assert(b.pending.empty());for(auto x:b.definitions)assert(x.length==0);
 rows.clear();bytes=0;truncated=false;
 for(int i=0;i<9000;++i)add(set);assert(truncated&&rows.size()==8192);
 rows.clear();bytes=0;truncated=false;b.Reset();
 const std::string large(2048,'x');for(int i=0;i<2000;++i)add(large);assert(truncated&&bytes==2*1024*1024);
 rows.clear();bytes=0;truncated=false;b.Reset();
 for(int i=0;i<130;++i)add(A);b.Flush(rows,bytes,truncated);assert(!truncated&&b.pending.empty()&&rows.size()==3);
 puts("TEXTURE_TRACE_BATCH_ASAN_PASS");
}
