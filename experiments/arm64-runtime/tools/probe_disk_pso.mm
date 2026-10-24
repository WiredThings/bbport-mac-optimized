// Private diagnostic only: one compute PSO, no game or account access.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CommonCrypto/CommonDigest.h>
#include <mach/mach.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
static double now() { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static unsigned long long footprint() {
    task_vm_info_data_t info{};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&info,&count)==KERN_SUCCESS?info.phys_footprint:0;
}
static NSString* hash(NSData* data) { unsigned char h[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,h);NSMutableString* s=[NSMutableString string];for(auto c:h)[s appendFormat:@"%02x",c];return s; }
int main(int argc,char**argv) { @autoreleasepool {
    if(argc!=5){std::fprintf(stderr,"usage: probe_disk_pso seed|load|none source.metal|- private-archive threads\n");return 2;}
    NSString* mode=@(argv[1]);NSString* input=@(argv[2]);NSString* path=@(argv[3]);int threads=atoi(argv[4]);
    if(![@[@"seed",@"load",@"none"] containsObject:mode] || threads<1 || threads>1024 || ![path hasPrefix:@"/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/evidence/disk-pso-probe/"])return 2;
    const bool synthetic=[input isEqualToString:@"-"];
    NSError* error=nil;NSString* source=synthetic?@"#include <metal_stdlib>\nusing namespace metal;\nkernel void main0(device uint* out [[buffer(0)]],uint i [[thread_position_in_grid]]) {out[i]=(i*1664525u+1013904223u)^0x9e3779b9u;}\n":[NSString stringWithContentsOfFile:input encoding:NSUTF8StringEncoding error:&error];
    if(!source || source.length>4*1024*1024){std::fprintf(stderr,"source unavailable or oversized\n");return 3;}
    NSRange marker=[source rangeOfString:@"// ---- MSL\n"];if(marker.location!=NSNotFound)source=[source substringFromIndex:NSMaxRange(marker)];
    const auto before=footprint();const double t=now();id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device)return 4;
    NSDictionary* identity=@{@"source_sha256":hash([source dataUsingEncoding:NSUTF8StringEncoding]),@"registry_id":@(device.registryID),@"os":[NSProcessInfo processInfo].operatingSystemVersionString,@"language":@"3.1",@"math":@"Safe",@"entry":@"main0",@"max_threads":@(threads),@"schema":@1};
    NSString* metadata=[path stringByAppendingString:@".json"];NSString* snapshot=nil;id<MTLBinaryArchive> archive=nil;
    if(![mode isEqualToString:@"none"]) {
        MTLBinaryArchiveDescriptor* archive_desc=[MTLBinaryArchiveDescriptor new];
        if([mode isEqualToString:@"load"]) {
            NSDictionary* saved=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:metadata] options:0 error:&error];
            if(![identity isEqual:saved]){std::fprintf(stderr,"identity mismatch: archive not used\n");return 5;}
            snapshot=[path stringByAppendingFormat:@".read-%d",getpid()];
            if(![[NSFileManager defaultManager] copyItemAtPath:path toPath:snapshot error:&error])return 6;
            archive_desc.url=[NSURL fileURLWithPath:snapshot];
        }
        archive=[device newBinaryArchiveWithDescriptor:archive_desc error:&error];
        if(!archive){std::fprintf(stderr,"archive: %s\n",error.localizedDescription.UTF8String);return 7;}
    }
    double archive_done=now();auto archive_mem=footprint();
    MTLCompileOptions* options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion3_1;
    if(@available(macOS 15.0,*))options.mathMode=MTLMathModeSafe;else options.fastMathEnabled=NO;
    double begin=now();id<MTLLibrary> library=[device newLibraryWithSource:source options:options error:&error];double library_ms=now()-begin;
    if(!library){std::fprintf(stderr,"library: %s\n",error.localizedDescription.UTF8String);return 8;}
    id<MTLFunction> function=[library newFunctionWithName:@"main0"];if(!function)return 9;
    MTLComputePipelineDescriptor* desc=[MTLComputePipelineDescriptor new];desc.computeFunction=function;desc.maxTotalThreadsPerThreadgroup=threads;if(archive)desc.binaryArchives=@[archive];
    begin=now();id<MTLComputePipelineState> pso=[device newComputePipelineStateWithDescriptor:desc options:[mode isEqualToString:@"load"]?MTLPipelineOptionFailOnBinaryArchiveMiss:MTLPipelineOptionNone reflection:nil error:&error];double pso_ms=now()-begin;
    if(!pso){std::fprintf(stderr,"PSO: %s\n",error.localizedDescription.UTF8String);return 10;}
    auto pso_mem=footprint();double save_ms=0;
    if([mode isEqualToString:@"seed"]) {
        begin=now();desc.binaryArchives=@[];
        if(![archive addComputePipelineFunctionsWithDescriptor:desc error:&error])return 11;
        NSString* temp=[path stringByAppendingFormat:@".write-%d",getpid()];
        if(![archive serializeToURL:[NSURL fileURLWithPath:temp] error:&error])return 12;
        // This diagnostic never overwrites a cache. Each seed needs a new path.
        if([[NSFileManager defaultManager] fileExistsAtPath:path])return 13;
        if(![[NSFileManager defaultManager] moveItemAtPath:temp toPath:path error:&error])return 14;
        NSData* json=[NSJSONSerialization dataWithJSONObject:identity options:NSJSONWritingPrettyPrinted error:&error];if(![json writeToFile:metadata options:NSDataWritingAtomic error:&error])return 15;
        save_ms=now()-begin;
    }
    desc.binaryArchives=@[];archive=nil;const auto release_mem=footprint();
    if(snapshot && ![[NSFileManager defaultManager] removeItemAtPath:snapshot error:&error])return 16;
    bool readback=false;
    if(synthetic) {
        id<MTLBuffer> out=[device newBufferWithLength:256*sizeof(uint32_t) options:MTLResourceStorageModeShared];id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> cmd=[queue commandBuffer];id<MTLComputeCommandEncoder> enc=[cmd computeCommandEncoder];
        [enc setComputePipelineState:pso];[enc setBuffer:out offset:0 atIndex:0];[enc dispatchThreads:MTLSizeMake(256,1,1) threadsPerThreadgroup:MTLSizeMake(threads,1,1)];[enc endEncoding];[cmd commit];[cmd waitUntilCompleted];
        if(cmd.status!=MTLCommandBufferStatusCompleted)return 17;
        for(uint32_t i=0;i<256;i++)if(((uint32_t*)out.contents)[i]!=((i*1664525u+1013904223u)^0x9e3779b9u))return 18;
        readback=true;
    }
    NSDictionary* result=@{@"mode":mode,@"synthetic":@(synthetic),@"actual_shader_executed":@NO,@"synthetic_readback_passed":@(readback),@"archive_hit_required":@([mode isEqualToString:@"load"]),@"archive_setup_ms":@(archive_done-t),@"library_ms":@(library_ms),@"pso_ms":@(pso_ms),@"save_ms":@(save_ms),@"total_ms":@(now()-t),@"footprint_before":@(before),@"footprint_archive":@(archive_mem),@"footprint_pso":@(pso_mem),@"footprint_released_archive":@(release_mem),@"identity":identity};
    NSData* json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];std::fwrite(json.bytes,1,json.length,stdout);std::puts("");return 0;
}}
