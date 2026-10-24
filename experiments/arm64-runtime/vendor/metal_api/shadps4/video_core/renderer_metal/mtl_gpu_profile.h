// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <memory>
#include <mutex>
#include <string_view>
#include "bbport_toggles.h"

namespace Metal {
// Diagnostic only: stage-boundary samples on existing encoders. Four bounded slots are
// reused ONLY after completion + resolution. No resource readback, GPU wait or cache change.
// Opaque MetalFX work needs two four-byte blit boundary probes in sampled command buffers; their
// cost is included in the MetalFX boundary span and count is reported separately. Neither sampled spans nor their sum are critical-path time.
class GpuProfile : public std::enable_shared_from_this<GpuProfile> {
public:
    enum Stage : unsigned { GuestRender, HostRender, Compute, Blit, VelocityDepth,
        VelocityDraw, VelocityMerge, VelocityPacking, CameraMotion, Occlusion, BlurCopy,
        UpscaleMerge, Ui, Geometry, Tessellation, MetalFX, HostCopy, Mixed, StageCount };
    static constexpr std::array<const char*, StageCount> Names{
        "guest-render", "host-render", "compute", "blit", "velocity-depth", "velocity-draw",
        "velocity-merge", "velocity-packing", "camera-motion", "occlusion", "blur-copy",
        "upscale-merge", "ui", "geometry", "tessellation", "metalfx-boundary", "host-copy", "mixed"};
    static constexpr unsigned Slots = 4, MaxEntries = 1024, Samples = MaxEntries * 4;
    struct Stat { unsigned long long count = 0; double ns = 0, max_ns = 0; };
    struct RenderParts {
        Stat vertex, fragment;
        unsigned long long paired = 0;
        double overlap_ns = 0, gap_ns = 0;
        void Add(const RenderParts& r) {
            for (auto pair : {std::pair{&vertex, &r.vertex}, std::pair{&fragment, &r.fragment}}) {
                pair.first->count += pair.second->count; pair.first->ns += pair.second->ns;
                pair.first->max_ns = std::max(pair.first->max_ns, pair.second->max_ns);
            }
            paired += r.paired; overlap_ns += r.overlap_ns; gap_ns += r.gap_ns;
        }
    };
    struct Snapshot {
        std::array<Stat, StageCount> stages{};
        std::array<RenderParts, StageCount> render{};
        unsigned long long completed = 0, invalid = 0, missing = 0, partial_render = 0;
        double cb_ns = 0, covered_ns = 0;
    };
private:
    struct Entry {
        Stage stage; bool render = false, labeled = false, opaque = false;
        std::array<char,88> label{};
    };
    struct Hot { std::array<char,88> label{}; Stat stat; RenderParts render; };
    struct HotTable { std::array<Hot,128> entries{}; unsigned used=0, dropped=0; };
    std::unique_ptr<HotTable> hot;
    struct Batch {
        id<MTLCounterSampleBuffer> buffer;
        id<MTLFence> private_fence, opaque_fence;
        id<MTLBuffer> probe;
        std::atomic<bool> busy{false};
        std::array<Entry, MaxEntries> entries{};
        unsigned count = 0, current = MaxEntries, opaque = MaxEntries;
        MTLTimestamp cpu = 0, gpu = 0;
    };
    id<MTLDevice> device;
    id<MTLCounterSet> counter_set;
    std::array<std::shared_ptr<Batch>, Slots> pool{};
    std::shared_ptr<Batch> active;
    bool enabled = false;
    unsigned every = 8;
    unsigned long long serial = 0;
    std::mutex mutex;
    Snapshot total, window;
    std::atomic<unsigned long long> opened{0}, barriers{0}, fence_waits{0}, cpu_encode_ns{0},
        cpu_commit_ns{0}, probes{0}, capacity_drops{0}, busy_drops{0};
    std::array<std::atomic<unsigned long long>, 16> transitions{};
    unsigned last_kind = 0;
    std::atomic<unsigned> invalid_reports{0};
    std::chrono::steady_clock::time_point reported = std::chrono::steady_clock::now();
    unsigned long long last_frame = BbStats::gpu_frames.load();

    static Stage Classify(std::string_view s, Stage fallback) {
        if (s.find("velocity depth") != s.npos || s.find("Velocity depth") != s.npos) return VelocityDepth;
        if (s.find("Host texture copy") != s.npos) return HostCopy;
        if (s.find("velocity mirror") != s.npos) return VelocityDraw;
        if (s.find("object velocity") != s.npos) return VelocityMerge;
        if (s.find("velocity packing") != s.npos) return VelocityPacking;
        if (s.find("Camera motion") != s.npos) return CameraMotion;
        if (s.find("Occlusion") != s.npos) return Occlusion;
        if (s.find("without motion blur") != s.npos) return BlurCopy;
        if (s.find("Upscale merge") != s.npos) return UpscaleMerge;
        if (s.find("UI ") != s.npos) return Ui;
        if (s.find("Geometry") != s.npos) return Geometry;
        if (s.find("Tessellation") != s.npos) return Tessellation;
        return fallback;
    }
    void Resolve(const std::shared_ptr<Batch>& b, id<MTLCommandBuffer> cb) {
        @autoreleasepool {
        MTLTimestamp cpu = 0, gpu = 0;
        [device sampleTimestamps:&cpu gpuTimestamp:&gpu];
        // Apple's sampleTimestamps CPU values are nanoseconds. Do not assume GPU ticks
        // are nanoseconds (or apply mach_timebase to the already converted CPU values).
        const double scale = gpu > b->gpu && cpu > b->cpu
            ? double(cpu - b->cpu) / double(gpu - b->gpu) : 0;
        NSData* data = cb.status == MTLCommandBufferStatusCompleted && b->count
            ? [b->buffer resolveCounterRange:NSMakeRange(0, b->count * 4)] : nil;
        Snapshot delta;
        delta.completed = 1;
        delta.cb_ns = std::max(0.0, cb.GPUEndTime-cb.GPUStartTime)*1e9;
        std::array<std::pair<unsigned long long,unsigned long long>,MaxEntries> intervals{};
        unsigned interval_count = 0;
        std::array<double,MaxEntries> measured{};
        std::array<RenderParts,MaxEntries> parts{};
        if (data.length >= b->count * 4 * sizeof(MTLCounterResultTimestamp) && scale > 0) {
            const auto* values = static_cast<const MTLCounterResultTimestamp*>(data.bytes);
            for (unsigned i = 0; i < b->count; ++i) {
                const auto& e = b->entries[i];
                const auto* v = values + i * 4;
                const auto pair_valid = [&](unsigned a,unsigned z) {
                    return v[a].timestamp >= b->gpu && v[z].timestamp <= gpu &&
                        v[a].timestamp && v[z].timestamp &&
                        v[a].timestamp != MTLCounterErrorValue && v[z].timestamp != MTLCounterErrorValue &&
                        v[z].timestamp >= v[a].timestamp;
                };
                unsigned long long start = v[0].timestamp;
                unsigned long long end = v[e.opaque ? 3 : 1].timestamp;
                bool valid = pair_valid(0,e.opaque ? 3 : 1);
                if (e.render) {
                    const bool vertex=pair_valid(0,1), fragment=pair_valid(2,3);
                    valid=vertex||fragment;
                    if(vertex && fragment) {
                        start=std::min(v[0].timestamp,v[2].timestamp);
                        end=std::max(v[1].timestamp,v[3].timestamp);
                    } else if(valid) {
                        ++delta.partial_render;
                        start=v[vertex?0:2].timestamp; end=v[vertex?1:3].timestamp;
                    }
                }
                // Empty stages may never write a counter. Reject stale slots from an earlier
                // use of this buffer; a valid sample must fall inside this batch's clock pair.
                if(!valid) { ++delta.missing; continue; }
                const double ns = end >= start ? (end - start) * scale : -1;
                if (!valid || !std::isfinite(ns) || ns < 0 || ns > 10e9) {
                    ++delta.invalid;
                    if(invalid_reports.fetch_add(1)<4) std::printf("Metal GPU timing invalid: stage=%s, raw=%llu/%llu/%llu/%llu\n",Names[e.stage], (unsigned long long)v[0].timestamp,(unsigned long long)v[1].timestamp,(unsigned long long)v[2].timestamp,(unsigned long long)v[3].timestamp);
                    continue;
                }
                intervals[interval_count++]={start,end};
                measured[i]=ns;
                if (e.render) {
                    auto& r=parts[i];
                    if (pair_valid(0,1)) r.vertex={1,(v[1].timestamp-v[0].timestamp)*scale,0};
                    if (pair_valid(2,3)) r.fragment={1,(v[3].timestamp-v[2].timestamp)*scale,0};
                    r.vertex.max_ns=r.vertex.ns; r.fragment.max_ns=r.fragment.ns;
                    if (r.vertex.count && r.fragment.count) {
                        r.paired=1;
                        const auto a=std::max(v[0].timestamp,v[2].timestamp);
                        const auto z=std::min(v[1].timestamp,v[3].timestamp);
                        r.overlap_ns=z>a?(z-a)*scale:0;
                        r.gap_ns=std::max(0.0,ns-r.vertex.ns-r.fragment.ns+r.overlap_ns);
                    }
                    delta.render[e.stage].Add(r);
                }
                auto& s = delta.stages[e.stage];
                ++s.count; s.ns += ns; s.max_ns = std::max(s.max_ns, ns);
            }
        } else delta.invalid += b->count;
        std::sort(intervals.begin(),intervals.begin()+interval_count);
        unsigned long long covered_end=0;
        for (unsigned i=0;i<interval_count;++i) {
            const auto [start,end]=intervals[i];
            if(end>covered_end) delta.covered_ns+=(end-std::max(start,covered_end))*scale;
            covered_end=std::max(covered_end,end);
        }
        {
            std::scoped_lock lock{mutex};
            for (auto* s : {&total, &window}) {
                s->completed += delta.completed; s->invalid += delta.invalid;
                s->missing += delta.missing; s->partial_render += delta.partial_render;
                s->cb_ns += delta.cb_ns; s->covered_ns += delta.covered_ns;
                for (unsigned i = 0; i < StageCount; ++i) {
                    s->render[i].Add(delta.render[i]);
                    s->stages[i].count += delta.stages[i].count;
                    s->stages[i].ns += delta.stages[i].ns;
                    s->stages[i].max_ns = std::max(s->stages[i].max_ns, delta.stages[i].max_ns);
                }
            }
            for(unsigned i=0;i<b->count;++i) {
                const auto& label=b->entries[i].label;
                if(!label[0] || measured[i]<=0) continue;
                unsigned at=0;
                for(;at<hot->used;++at) if(hot->entries[at].label==label) break;
                if(at==hot->used) {
                    if(at==hot->entries.size()) { ++hot->dropped;continue; }
                    hot->entries[at].label=label; ++hot->used;
                }
                auto& stat=hot->entries[at].stat; ++stat.count;stat.ns+=measured[i];
                stat.max_ns=std::max(stat.max_ns,measured[i]);
                hot->entries[at].render.Add(parts[i]);
            }
            ReportLocked(false);
        }
        b->busy.store(false, std::memory_order_release);
        }
    }
    void ReportLocked(bool force) {
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - reported).count();
        if (!force && seconds < 5) return;
        const auto frame = BbStats::gpu_frames.load();
        std::printf("Metal GPU timing: seconds=%.3f, frames=%llu, sampled-cb=%llu, every=%u, invalid=%llu, missing=%llu, partial-render=%llu, capacity-drops=%llu, busy-drops=%llu, encoders=%llu, barriers=%llu, fence-waits=%llu, cpu-boundary-ms=%.3f, cpu-commit-ms=%.3f, probe-encoders=%llu, sampled-cb-ms=%.4f, sampled-covered-ms=%.4f, sampled-uncovered-ms=%.4f; spans may overlap, estimates are not critical-path time\n",
            seconds, frame - last_frame, window.completed, every, window.invalid, window.missing, window.partial_render,
            capacity_drops.exchange(0), busy_drops.exchange(0), opened.exchange(0), barriers.exchange(0),
            fence_waits.exchange(0), cpu_encode_ns.exchange(0)/1e6, cpu_commit_ns.exchange(0)/1e6, probes.exchange(0), window.cb_ns/1e6, window.covered_ns/1e6, std::max(0.0,window.cb_ns-window.covered_ns)/1e6);
        for (unsigned i = 0; i < StageCount; ++i) {
            const auto& s = window.stages[i];
            if (s.count) std::printf("Metal GPU stage: name=%s, samples=%llu, sampled-ms=%.4f, mean-us=%.3f, max-us=%.3f, estimated-ms/frame=%.4f\n",
                Names[i], s.count, s.ns/1e6, s.ns/s.count/1e3, s.max_ns/1e3,
                frame > last_frame ? s.ns * every / 1e6 / (frame - last_frame) : 0);
        }
        for (unsigned i=0;i<StageCount;++i) {
            const auto& r=window.render[i];
            if(r.vertex.count || r.fragment.count)
                std::printf("Metal GPU render parts: name=%s, vertex-samples=%llu, sampled-vertex-ms=%.4f, fragment-samples=%llu, sampled-fragment-ms=%.4f, paired-samples=%llu, sampled-overlap-ms=%.4f, sampled-stage-gap-ms=%.4f; stage spans include dependencies/tile work and can overlap\n",
                    Names[i],r.vertex.count,r.vertex.ns/1e6,r.fragment.count,r.fragment.ns/1e6,r.paired,r.overlap_ns/1e6,r.gap_ns/1e6);
        }
        std::array<unsigned,128> order{};
        for(unsigned i=0;i<hot->used;++i) order[i]=i;
        std::sort(order.begin(),order.begin()+hot->used,[&](unsigned a,unsigned b){return hot->entries[a].stat.ns>hot->entries[b].stat.ns;});
        for(unsigned i=0;i<std::min(hot->used,8u);++i) {
            const auto& h=hot->entries[order[i]];
            std::printf("Metal GPU hot pass: label=%s, samples=%llu, sampled-ms=%.4f, estimated-ms/frame=%.4f, vertex-samples=%llu, sampled-vertex-ms=%.4f, fragment-samples=%llu, sampled-fragment-ms=%.4f, paired-samples=%llu, sampled-overlap-ms=%.4f, sampled-stage-gap-ms=%.4f; first pipeline of pass, not isolated shader time\n",
                h.label.data(),h.stat.count,h.stat.ns/1e6,frame>last_frame?h.stat.ns*every/1e6/(frame-last_frame):0,
                h.render.vertex.count,h.render.vertex.ns/1e6,h.render.fragment.count,h.render.fragment.ns/1e6,h.render.paired,h.render.overlap_ns/1e6,h.render.gap_ns/1e6);
        }
        if(hot->dropped) std::printf("Metal GPU hot pass overflow: samples=%u; overall stage totals retained\n",hot->dropped);
        *hot={};
        std::printf("Metal GPU transitions:");
        for (unsigned a = 1; a < 4; ++a) for (unsigned b = 1; b < 4; ++b) {
            auto n = transitions[a*4+b].exchange(0);
            if (n) std::printf(" %u>%u=%llu", a,b,n);
        }
        std::printf(" (1=render 2=compute 3=blit; includes same-kind new passes)\n");
        reported = now; last_frame = frame; window = {};
    }
public:
    explicit GpuProfile(id<MTLDevice> d) : device{d} {
        const char* env = std::getenv("BB_MTL_GPU_TIMING");
        if (!env || env[0] != '1') return;
        if (const char* n = std::getenv("BB_MTL_GPU_TIMING_EVERY")) {
            char* end; auto v = std::strtoul(n, &end, 10);
            if (*n && !*end && v >= 1 && v <= 256) every = v;
        }
        if ([d supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
            for (id<MTLCounterSet> s in d.counterSets)
                if ([s.name isEqualToString:MTLCommonCounterSetTimestamp]) { counter_set = s; break; }
        enabled = counter_set != nil;
        if(enabled) hot=std::make_unique<HotTable>();
        std::printf("Metal GPU timing: %s, every=%u, max-slots=%u, max-encoders/slot=%u; stage boundaries, async resolve, bounded storage\n",
            enabled ? "enabled" : "unavailable", every, Slots, MaxEntries);
    }
    bool Enabled() const { return enabled; }
    bool HasSample() const { return active && active->current != MaxEntries; }
    Snapshot Stats() { std::scoped_lock lock{mutex}; return total; }
    void Report() { if (enabled) { std::scoped_lock lock{mutex}; ReportLocked(true); } }
    void Begin(id<MTLCommandBuffer> cb) {
        if (!enabled) return;
        active.reset(); last_kind = 0;
        const auto n = ++serial;
        // Hash the command-buffer serial rather than periodically sampling a fixed phase.
        auto hash = n * 0x9e3779b97f4a7c15ull; hash ^= hash >> 32;
        if (n != 1 && hash % every != 0) return;
        for (auto& slot : pool) {
            if (!slot) {
                slot = std::make_shared<Batch>();
                auto* desc = [MTLCounterSampleBufferDescriptor new];
                desc.counterSet = counter_set; desc.storageMode = MTLStorageModeShared;
                desc.sampleCount = Samples; desc.label = @"Bounded GPU stage timestamps";
                NSError* error = nil;
                slot->buffer = [device newCounterSampleBufferWithDescriptor:desc error:&error];
                slot->private_fence = [device newFence];
                slot->probe = [device newBufferWithLength:4 options:MTLResourceStorageModePrivate];
                if (!slot->buffer || !slot->private_fence || !slot->probe) { enabled = false; std::printf("Metal GPU timing: counter allocation failed (%s); profiling disabled\n", error ? error.localizedDescription.UTF8String : "fence allocation"); return; }
            }
            if (!slot->busy.exchange(true, std::memory_order_acq_rel)) { active = slot; break; }
        }
        if (!active) { ++busy_drops; return; }
        active->count = 0; active->current = active->opaque = MaxEntries;
        [device sampleTimestamps:&active->cpu gpuTimestamp:&active->gpu];
        auto self = shared_from_this(); auto b = active;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> completed) { self->Resolve(b, completed); }];
    }
    void Detach() { active.reset(); }
    void Open(unsigned kind, bool guest) {
        if (!enabled) return;
        ++opened;
        if (last_kind) ++transitions[last_kind*4+kind];
        last_kind = kind;
        if (!active) return;
        if (active->count >= MaxEntries) { ++capacity_drops; active->current = MaxEntries; return; }
        const auto i = active->count++;
        active->entries[i] = {kind==1 ? (guest ? GuestRender : HostRender) : kind==2 ? Compute : Blit, kind==1};
        active->current = i;
    }
    void Render(MTLRenderPassDescriptor* pass) {
        if (!active || active->current == MaxEntries) return;
        auto* a = pass.sampleBufferAttachments[0]; auto i = active->current * 4;
        a.sampleBuffer = active->buffer; a.startOfVertexSampleIndex=i; a.endOfVertexSampleIndex=i+1;
        a.startOfFragmentSampleIndex=i+2; a.endOfFragmentSampleIndex=i+3;
    }
    MTLComputePassDescriptor* ComputePass() {
        auto* pass = [MTLComputePassDescriptor computePassDescriptor];
        if (active && active->current != MaxEntries) {
            auto* a = pass.sampleBufferAttachments[0]; a.sampleBuffer=active->buffer;
            a.startOfEncoderSampleIndex=active->current*4; a.endOfEncoderSampleIndex=active->current*4+1;
        }
        return pass;
    }
    MTLBlitPassDescriptor* BlitPass() {
        auto* pass = [MTLBlitPassDescriptor blitPassDescriptor];
        if (active && active->current != MaxEntries) {
            auto* a = pass.sampleBufferAttachments[0]; a.sampleBuffer=active->buffer;
            a.startOfEncoderSampleIndex=active->current*4; a.endOfEncoderSampleIndex=active->current*4+1;
        }
        return pass;
    }
    void Label(const char* label) {
        if (!active || active->current == MaxEntries || !label) return;
        auto& e = active->entries[active->current];
        const auto stage = Classify(label, e.stage);
        if (e.labeled && e.stage != stage) e.stage = Mixed;
        else e.stage = stage;
        e.labeled = true;
        e.label={};
        std::string_view key{label};
        if(e.stage==GuestRender && key.starts_with("P")) {
            const auto space=key.find(' ');
            if(space==key.npos) return;
            key.remove_prefix(space+1); // drop the monotonically increasing pass number
        } else if(!((e.stage==Compute && key.starts_with("CS ")) || e.stage==HostCopy || e.stage==BlurCopy)) return;
        const auto bytes=std::min(key.size(),e.label.size()-1);
        std::memcpy(e.label.data(),key.data(),bytes);e.label[bytes]=0;
    }
    void Close() { if (active) active->current = MaxEntries; }
    void Barrier() { if (enabled) ++barriers; }
    void FenceWait() { if (enabled) ++fence_waits; }
    void CpuEncode(unsigned long long ns) { if (enabled) cpu_encode_ns += ns; }
    void CpuCommit(unsigned long long ns) { if (enabled) cpu_commit_ns += ns; }
    // Bracket an opaque encoder producer without splitting any of its internal passes.
    id<MTLFence> OpaqueFence() const {
        return active && active->opaque != MaxEntries ? active->opaque_fence : nil;
    }
    void Opaque(id<MTLCommandBuffer> cb, bool begin, id<MTLFence> existing = nil) {
        if (!active) return;
        if (begin) {
            if (active->count >= MaxEntries) { ++capacity_drops; return; }
            active->opaque_fence=existing ?: active->private_fence;
            active->opaque=active->count++;
            active->entries[active->opaque]={MetalFX,false,true,true};
        }
        if (active->opaque == MaxEntries) return;
        auto* pass = [MTLBlitPassDescriptor blitPassDescriptor]; auto* a=pass.sampleBufferAttachments[0];
        a.sampleBuffer=active->buffer; auto i=active->opaque*4+(begin?0:2);
        a.startOfEncoderSampleIndex=i; a.endOfEncoderSampleIndex=i+1;
        auto encoder=[cb blitCommandEncoderWithDescriptor:pass];
        // A boundary marker without a dependency may run in parallel with the library.
        // The library waits/updates this same fence; existing external fences are preserved.
        if(begin && existing) [encoder waitForFence:existing];
        if(!begin) [encoder waitForFence:active->opaque_fence];
        // Empty blit passes are removed by the M4 driver (all four timestamps stay zero).
        [encoder fillBuffer:active->probe range:NSMakeRange(0,4) value:0];
        if(begin) [encoder updateFence:active->opaque_fence];
        [encoder endEncoding]; ++probes;
        if (!begin) active->opaque=MaxEntries;
    }
};
} // namespace Metal
