// SPDX-License-Identifier: GPL-3.0-or-later
// Included inside ArmTravel; a private diagnostic request never authorizes reclaim.
unsigned inspection_sequence{};
void InspectResources(){
 char data[4097]{};ssize_t size{};
 if(!File(directory/"inspect-request.txt",data,sizeof(data),size))return;
 BbResourceQuery::Request request;
 if(!BbResourceQuery::Parse(std::string_view(data,size),nonce,request)||request.sequence<=inspection_sequence)return;
 if(request.sequence!=inspection_sequence+1)return;
 std::ostringstream out;out<<"{\"schema\":1,\"sequence\":"<<request.sequence<<",\"nonce\":\""<<nonce<<"\",\"reclaim_ready\":"<<(BbResourceLifetime::ReclaimReady()?"true":"false")<<",\"queries\":[";
 const auto tag=[&](auto t){out<<"{\"address\":"<<t.address<<",\"size\":"<<t.size<<",\"generation\":"<<t.generation<<",\"domain\":"<<unsigned(t.domain)<<"}";};
 for(unsigned i=0;i<request.count;++i){
  if(i)out<<',';const auto r=request.ranges[i];RuntimeMappingSnapshot mapping{};
  const bool mapped=runtime_memory_inspect_mapping(r.address,&mapping),complete=mapped&&r.address>=mapping.begin&&r.size<=mapping.end-r.address;
  RuntimeMappingAlias aliases[8]{};int truncated{};U generation{};
  const bool supported=complete&&mapping.kind==2&&!mapping.private_mem&&!mapping.host_owned;
  const size_t count=supported?runtime_memory_inspect_aliases(r.address,r.size,aliases,8,&truncated,&generation):0;
  const auto inspection=BbResourceLifetime::Get().InspectRange({},r.address,r.size);
  out<<"{\"address\":"<<r.address<<",\"size\":"<<r.size<<",\"mapped\":"<<(mapped?"true":"false")<<",\"range_in_one_mapping\":"<<(complete?"true":"false")<<",\"mapping_begin\":"<<mapping.begin<<",\"mapping_end\":"<<mapping.end<<",\"physical_begin\":"<<mapping.physical<<",\"kind\":"<<mapping.kind<<",\"prot\":"<<mapping.prot<<",\"type\":"<<mapping.type<<",\"mapping_generation\":"<<mapping.generation<<",\"alias_generation\":"<<generation<<",\"aliases_supported\":"<<(supported?"true":"false")<<",\"mapping_consistent\":"<<(supported&&generation==mapping.generation?"true":"false")<<",\"aliases_truncated\":"<<(truncated?"true":"false")<<",\"cpu_owner\":";tag(inspection.covering_owner);
  out<<",\"cpu_heap_overlaps\":"<<inspection.overlaps[1]<<",\"cpu_range_overlaps\":"<<inspection.overlaps[0]<<",\"overlaps_truncated\":"<<(inspection.truncated?"true":"false")<<",\"aliases\":[";
  for(size_t j=0;j<count;++j){if(j)out<<',';const auto a=aliases[j];const auto owner=BbResourceLifetime::Get().InspectRange({},a.address,a.size);out<<"{\"address\":"<<a.address<<",\"size\":"<<a.size<<",\"physical\":"<<a.physical<<",\"cpu_owner\":";tag(owner.covering_owner);out<<",\"cpu_heap_overlaps\":"<<owner.overlaps[1]<<",\"cpu_range_overlaps\":"<<owner.overlaps[0]<<",\"overlaps_truncated\":"<<(owner.truncated?"true":"false")<<'}';}
  U issued{};const auto source=ArmAllocation::Match(r.address,r.size,issued);
  out<<"],\"source_allocation\":{\"probe_enabled\":"<<(ArmAllocation::enabled?"true":"false")<<",\"issued\":"<<issued<<",\"capacity\":1024,\"sequence\":"<<source.sequence<<",\"address\":"<<source.address<<",\"size\":"<<source.size<<",\"allocator\":"<<source.allocator<<",\"callee_offset\":"<<source.callee_offset<<",\"kind\":"<<source.kind<<",\"primary_offset\":"<<source.primary_offset<<",\"secondary_offset\":"<<source.secondary_offset<<",\"read_failed\":"<<(source.read_failed?"true":"false")<<"}}";
 }
 out<<"]}\n";const auto contents=out.str();
 const auto pending=directory/"inspection.pending",final=directory/("inspection-"+std::to_string(request.sequence)+".json");
 int fd=open(pending.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return;
 size_t offset=0;while(offset<contents.size()){ssize_t n=write(fd,contents.data()+offset,contents.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0)break;offset+=n;}
 bool ok=offset==contents.size();if(close(fd))ok=false;if(!ok||rename(pending.c_str(),final.c_str()))return;
 if(getenv("BB_ARM_EXTRA_HEAP_DIAG"))ArmExtraHeaps::ReportClasses();
 inspection_sequence=request.sequence;printf("ARM RESOURCE inspection=%u ranges=%u read-only, no reclaim authorization\n",request.sequence,request.count);
}
