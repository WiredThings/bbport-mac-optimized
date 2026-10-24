#include "bb_fex_map_audit.h"
#include <cassert>
#include <sys/mman.h>
int main(int argc,char**){
 const bool enabled=argc>1;if(enabled)setenv("BB_FEX_MAPPING_AUDIT","1",1);else unsetenv("BB_FEX_MAPPING_AUDIT");assert(BbFexMap::Enabled()==enabled);
 void*p=mmap(nullptr,65536,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);assert(p!=MAP_FAILED);errno=E2BIG;BbFexMap::Event('A',BbFexMap::Lookup,p,65536);assert(errno==E2BIG);BbFexMap::Event('F',BbFexMap::Lookup,p,65536);assert(!munmap(p,65536));assert(BbFexMap::Get().issued==(enabled?2:0)&&!BbFexMap::Get().failed);
 BbFexMap::Event('A',BbFexMap::Lookup,p,0);assert(BbFexMap::Get().failed==(enabled?1:0));
 if(enabled){BbFexMap::Get().issued=BbFexMap::Limit;BbFexMap::Event('A',BbFexMap::Lookup,p,65536);assert(BbFexMap::Get().issued==BbFexMap::Limit+1);BbFexMap::Event('A',BbFexMap::Lookup,p,65536);}
 puts(enabled?"FEX map audit enabled: errno, bounds, alloc/free and hard output limit passed":"FEX map audit disabled: original mapping retained, no events");
}
