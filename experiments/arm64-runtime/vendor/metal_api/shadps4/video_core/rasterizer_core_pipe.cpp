#include "bb_packet_metadata.h"
#include <cstddef>
#include <type_traits>
#include "bbport_cpu.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project

#include <chrono>
#include <dlfcn.h>
#include <map>
#include <unordered_set>
#include <xxhash.h>
#include "video_core/ui_composition.h"
#include "bbport_timeline.h"
#include "bbport_sections.h"
#include "game_profile.h"
#include "bbport_ce_stats.h"
#include "bbport_toggles.h"
#include "bbport_write_log.h"
#include "bbport_free_check.h"
#include "bbport_guest_memory.h"
#include "video_core/frame_capture.h"
#include "common/hash.h"
#include "common/debug.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/rasterizer_core.h"
#include "bbport_threads.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/texture_cache.h"

// The draw pipeline's vocabulary (vk_draw_pipe.h, vk_draw_prep.h) and the translation helpers.
using Vulkan::DrawPipe;
using Vulkan::ImageDescHash;
using Vulkan::PreparedDraw;
using Vulkan::PreparedStage;
using Vulkan::PreparedVertexInputs;
namespace LiverpoolToVK = Vulkan::LiverpoolToVK;

namespace VideoCore {

// bbport: two-stage draw pipeline (vk_draw_pipe.h). Packet layout, 8-byte aligned parts:
// DrawPacket, u16 block indices, u32 block words (RegDirty::BlockWords each), then per stage a
// PacketStage with its user data and flattened user data, then (verification) the full file.
namespace {
enum class PacketKind : u32 { Draw, Task, Dispatch, Special, Indirect, IndirectDispatch };
struct TaskPacket {
    PacketKind kind;
    u32 size;
    VideoCore::RasterizerApi::OrderedTask task;
};
#if defined(__APPLE__) && defined(__aarch64__)
static_assert(sizeof(TaskPacket)==16 && offsetof(TaskPacket,kind)==0 && offsetof(TaskPacket,size)==4 && offsetof(TaskPacket,task)==8);
static_assert(sizeof(RasterizerApi::OrderedTask)==8 && std::is_trivially_copyable_v<TaskPacket>);
static_assert(u32(PacketKind::Draw)==0 && u32(PacketKind::Task)==BbPacketMetadata::TaskKind && u32(PacketKind::Dispatch)==2 && u32(PacketKind::Special)==3 && u32(PacketKind::Indirect)==4 && u32(PacketKind::IndirectDispatch)==5);
#endif
struct DrawPacket {
    PacketKind kind;
    const PipelineBase* pipeline;
    const PreparedDraw* prepared;
    u32 index_offset;
    u8 is_indexed;
    u8 regs_reset;
    u16 num_blocks;
    u32 num_stages;
    u32 verify;
    std::array<AmdGpu::CbDbExtent, AmdGpu::NUM_COLOR_BUFFERS> cb_extent;
    AmdGpu::CbDbExtent db_extent;
    u32 num_vsharps; ///< V#s of the vertex streams, after the indirect arguments
    u32 pad;
    u64 ring_end; ///< constant ring position after this packet's copies
};
struct PacketStage {
    const Shader::Info* info;
    VAddr pgm_base;
    u32 ud_size;
    u32 flat_size;
    u32 num_ring;
    u32 pad;
};
constexpr u32 AlignPacket(u32 size) {
    return (size + 7) & ~7u;
}
u32 VerifyInterval() {
    static const u32 interval = [] {
        const char* env = std::getenv("BB_PIPE_VERIFY");
        return env ? static_cast<u32>(std::strtoul(env, nullptr, 10)) : 0u;
    }();
    return interval;
}
} // namespace

bool RasterizerCore::DrawPipeWanted() {
    const char* env = std::getenv("BB_DRAW_PIPE");
    if (env && env[0]) {
        return env[0] == '1';
    }
    // Stage B spins while draws flow. Measured ahead with 16 threads (+19%) and with 4 cores /
    // 8 threads (taskset, Steam Deck-like: +18%).
    return BbThreads::Available() >= 8;
}

bool RasterizerCore::UseDrawPipe() const {
    return draw_pipe && !host_markers_enabled && !BbToggle::Disabled(BbToggle::DrawPipeline);
}

bool RasterizerCore::OnStageA() const {
    return std::this_thread::get_id() == liverpool->GetGpuCommandProcessorThread();
}

namespace {
/// Stage A: cycles waited per drain site (BB_FRAME_STATS), keyed by function name and line.
std::unordered_map<const char*, std::unordered_map<u32, u64>> drain_sites;
} // namespace

void RasterizerCore::DrainDrawPipe(u32 reason, u32 line, const char* function) {
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    static const bool stats = std::getenv("BB_FRAME_STATS") != nullptr;
    if (!stats) {
        draw_pipe->Drain(reason);
        return;
    }
    const u64 before = draw_pipe->drain_cycles;
    draw_pipe->Drain(reason);
    if (const u64 waited = draw_pipe->drain_cycles - before) {
        drain_sites[function][line] += waited;
    }
}

bool RasterizerCore::IsGpuSideThread() const {
    return OnStageA() || DrawPipe::OnStageB();
}

bool RasterizerCore::IsGpuSideThreadId(u32 tid) const {
    // The userfaultfd thread handles the fault while the faulting thread waits: without locks
    // only if that thread owns the caches. The GPU command thread does not while the draw
    // recording thread may be running, so its faults take the locked path of guest threads.
    if (draw_pipe) {
        return tid == draw_pipe->StageBThreadId();
    }
    return tid == liverpool->GetGpuCommandProcessorThreadId();
}

void RasterizerCore::NotePendingGpuWrite(VAddr address, u64 size) {
    if (!draw_pipe || !size) {
        return;
    }
    // Numbered by packet (a position, the packet's start, counted as reached once the packet
    // before it had run, while this one was not recorded yet), in hashed granules: a lookup per
    // constant binding instead of a scan of the writes (a list scan was ~5% of this thread).
    pending_write_table.Note(address, size, draw_pipe->NextPacket());
}

void RasterizerCore::NotePendingRead(VAddr address, u64 size) {
    if (draw_pipe && size && OnStageA()) {
        pending_reads.Note(address, size, draw_pipe->NextPacket());
    }
}

void RasterizerCore::WaitForPendingReads(VAddr address, u64 size) {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_CONSTRAM_PIPE");
        return !env || env[0] != '0';
    }();
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    if (!enabled || !UseDrawPipe() || size == 0) {
        DrainDrawPipe(DrawPipe::ReasonConstRam);
        return;
    }
    const u64 packet = std::max(untracked_reads_packet, pending_reads.Newest(address, size));
    if (draw_pipe->ReachedPacket(packet)) {
        ++constram_waits_skipped;
        return;
    }
    draw_pipe->WaitForPacket(packet, DrawPipe::ReasonConstRam);
}

void RasterizerCore::NoteCommandWriteInOrder(VAddr addr, const void* data, u64 size) {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_CONSTRAM_PIPE");
        return !env || env[0] != '0';
    }();
    if (VideoCore::WriteTracking() || !IsMapped(addr, size)) {
        return;
    }
    if (!enabled || !UseDrawPipe() || !OnStageA() || size > 1_MB) {
        NoteCommandWrite(addr, data, size, false);
        return;
    }
    // The caches belong to the recording thread while it has packets: it updates them in order
    // (later packets see the new bytes there; the memory already holds them for this thread).
    struct Header {
        VAddr addr;
        u64 size;
    };
    thread_local std::vector<u8> payload;
    payload.resize(sizeof(Header) + size);
    const Header header{addr, size};
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), data, size);
    RunInOrder(
        [](VideoCore::RasterizerApi& self, const u8* bytes) {
            Header write;
            std::memcpy(&write, bytes, sizeof(write));
            self.NoteCommandWrite(write.addr, bytes + sizeof(write), write.size, true);
        },
        payload.data(), static_cast<u32>(payload.size()), BbToggle::PipelinedTasks, false);
}

bool RasterizerCore::PendingWriteOverlaps(VAddr address, u64 size) {
    if (!draw_pipe || size == 0) {
        return false;
    }
    const u64 packet = pending_write_table.Newest(address, size);
    return packet != 0 && !draw_pipe->ReachedPacket(packet);
}

void RasterizerCore::CollectRingBindings(const Shader::Info& stage, const PreparedDraw* prepared,
                           boost::container::static_vector<RingBinding, Shader::NUM_BUFFERS>& out) {
    const PreparedStage* prepared_stage = nullptr;
    if (prepared && !BbToggle::Disabled(BbToggle::PreparedResources)) {
        for (u32 i = 0; i < prepared->num_stages; ++i) {
            const auto& candidate = prepared->stages[i];
            if (&candidate.program->info == &stage &&
                candidate.num_buffers == stage.buffers.size()) {
                prepared_stage = &candidate;
                break;
            }
        }
    }
    const u64 alignment = std::max<u64>(backend.Limits().storage_min_alignment,
                                        backend.Limits().uniform_min_alignment);
    const auto copy = [&](u32 index, const void* source, VAddr address, u64 size) {
        const auto offset = constant_ring->Allocate(size, alignment);
        if (!offset) {
            return false;
        }
        u8* dst = constant_ring->Data(*offset);
        if (source) {
            std::memcpy(dst, source, size);
        } else {
            if(FrameCapture::NativeRecording())FrameCapture::NativeBufferUse(
                0,0,*offset,size,address,u32(stage.sw_stage),stage.pgm_hash,false,"ring_cpu_snapshot");
            memory->CopySparseMemory(address, dst, size);
        }
        constant_ring->Flush(*offset, size);
        out.push_back({index, static_cast<u32>(size), *offset, address});
        return true;
    };
    // Compute stages may run as HLE copy shaders, which read their buffers on the recording
    // thread whether or not they were copied here.
    const bool hle_candidate = stage.sw_stage == Shader::SwStage::Compute;
    // BB_CONSTANTS_IN_PLACE: nothing is copied here; the recording thread binds the game's memory
    // (or copies what is not in place, a read noted below).
    const bool constants_in_place = VideoCore::ConstantsInPlace();
    for (u32 index = 0; index < stage.buffers.size(); ++index) {
        const auto& desc = stage.buffers[index];
        if (desc.IsSpecial()) {
            if (desc.buffer_type == Shader::BufferType::Flatbuf &&
                !stage.flattened_ud_buf.empty()) {
                copy(index, stage.flattened_ud_buf.data(), 0,
                     stage.flattened_ud_buf.size() * sizeof(u32));
            }
            continue;
        }
        const auto vsharp = prepared_stage ? prepared_stage->buffer_sharps[index]
                                           : desc.GetSharp(stage);
        const VAddr address = vsharp.base_address;
        if (address == 0 || vsharp.GetSize() == 0) {
            continue;
        }
        const u64 size = memory->ClampRangeSize(address, vsharp.GetSize());
        if (desc.is_written) {
            NotePendingGpuWrite(address, size);
            continue;
        }
        if (BbCeStats::Enabled()) {
            BbCeStats::Check(size <= VideoCore::BufferCache::STREAM_THRESHOLD
                                 ? BbCeStats::RingConstants
                                 : BbCeStats::LargeBuffer,
                             address, size);
        }
        // The stream path of BufferCache::ObtainBuffer, taken here: small, read-only, not
        // written by the GPU (now or by work still queued for the recording thread). What is not
        // copied is read on the recording thread: a CPU write over these bytes on this thread
        // waits for the packet (WaitForPendingReads).
        if (size == 0 || size > VideoCore::BufferCache::STREAM_THRESHOLD || constants_in_place ||
            buffer_cache.IsRegionGpuModified(address, size) || PendingWriteOverlaps(address, size) ||
            VideoCore::BufferCache::LayerMirrored(address, size) ||
            !copy(index, nullptr, address, size) || hle_candidate) {
            NotePendingRead(address, size);
        }
    }
}

void RasterizerCore::PostDraw(const PipelineBase* pipeline, const PreparedDraw* used_prepared,
                          bool is_indexed, u32 index_offset, const AmdGpu::ComputeProgram* cs,
                          const IndirectDraw* indirect) {
    auto& dirty = liverpool->pipe_dirty;
    if (!pipe_synced) {
        // Stage B has not run yet: its register copy starts as this one.
        pipe_regs = liverpool->regs;
        dirty.Clear();
        pipe_synced = true;
    }
    std::array<u16, AmdGpu::RegDirty::NumBlocks> blocks;
    u32 num_blocks = 0;
    // bbport: a word-wise scan of the bitset's storage (libstdc++ and libc++ both keep its bits
    // in 64-bit words from the start of the object); _Find_first/_Find_next are libstdc++ only.
    constexpr size_t NumWords = (AmdGpu::RegDirty::NumBlocks + 63) / 64;
    static_assert(sizeof(dirty.blocks) == NumWords * sizeof(u64));
    std::array<u64, NumWords> words;
    std::memcpy(words.data(), &dirty.blocks, sizeof(words));
    for (size_t word = 0; word < NumWords; ++word) {
        for (u64 bits = words[word]; bits; bits &= bits - 1) {
            blocks[num_blocks++] = static_cast<u16>(word * 64 + std::countr_zero(bits));
        }
    }
    const auto stages =
        pipeline ? pipeline->GetStages() : std::span<const Shader::Info* const>{};
    // Constants: copied here into the ring, the recording thread only binds them.
    thread_local std::array<boost::container::static_vector<RingBinding, Shader::NUM_BUFFERS>,
                            Shader::MaxStageTypes>
        rings;
    u32 num_stages = 0;
    u32 size = sizeof(DrawPacket) + AlignPacket(num_blocks * sizeof(u16)) +
               num_blocks * AmdGpu::RegDirty::BlockWords * sizeof(u32);
    for (const auto* stage : stages) {
        if (stage) {
            auto& ring = rings[num_stages];
            if (BbCeStats::Enabled()) {
                const auto ud = stage->UserData();
                for (size_t i = 0; i + 1 < ud.size(); ++i) {
                    const u64 value = u64(ud[i]) | u64(ud[i + 1]) << 32;
                    if (value >= 0x100000000ull && value < (1ull << 40)) {
                        BbCeStats::Check(BbCeStats::UserDataPointer, value, 64);
                    }
                }
                BbCeStats::Get().checks[BbCeStats::DmaStage]++;
                BbCeStats::Get().hits[0][BbCeStats::DmaStage] += stage->uses_dma ? 1 : 0;
            }
            // bbport BB_WATCH=addr,size (diagnostics): what the translator reads from that range on
            // the CPU: user data pointing into it, constants copied from it; printed every 2 s.
            static const std::pair<u64, u64> watch = [] {
                const char* env = std::getenv("BB_WATCH");
                if (!env) {
                    return std::pair<u64, u64>{0, 0};
                }
                char* end = nullptr;
                const u64 addr = std::strtoull(env, &end, 0);
                const u64 len = end && *end == ',' ? std::strtoull(end + 1, nullptr, 0) : 0;
                return std::pair<u64, u64>{addr, len};
            }();
            if (watch.second) {
                static u64 ud_hits = 0, ud_checks = 0, ring_hits = 0, vs_hits = 0;
                static u64 code_hits = 0;
                if (stage->ProgramBase() >= watch.first && stage->ProgramBase() < watch.first + watch.second) {
                    ++code_hits;
                }
                static u64 last_hash = 0;
                static auto report = std::chrono::steady_clock::now();
                const auto ud = stage->UserData();
                ++ud_checks;
                for (size_t i = 0; i + 1 < ud.size(); ++i) {
                    const u64 value = (u64(ud[i]) | u64(ud[i + 1]) << 32) & 0xFFFFFFFFFFFFull;
                    if (value >= watch.first && value < watch.first + watch.second) {
                        ++ud_hits;
                        last_hash = stage->pgm_hash;
                    }
                }
                for (u32 index = 0; index < stage->buffers.size(); ++index) {
                    const auto& desc = stage->buffers[index];
                    if (desc.IsSpecial()) {
                        continue;
                    }
                    const auto vsharp = desc.GetSharp(*stage);
                    if (vsharp.base_address < watch.first + watch.second &&
                        watch.first < vsharp.base_address + vsharp.GetSize()) {
                        ++(vsharp.GetSize() <= VideoCore::BufferCache::STREAM_THRESHOLD ? ring_hits
                                                                                      : vs_hits);
                        last_hash = stage->pgm_hash;
                    }
                }
                if (std::chrono::steady_clock::now() - report > std::chrono::seconds(2)) {
                    report = std::chrono::steady_clock::now();
                    std::printf("Watch %#llx+%#llx: user data pointers into it %llu of %llu stages, "
                                "small buffers over it %llu, large %llu, shader code in it %llu (last shader %016llx)\n",
                                (unsigned long long)watch.first, (unsigned long long)watch.second,
                                (unsigned long long)ud_hits, (unsigned long long)ud_checks,
                                (unsigned long long)ring_hits, (unsigned long long)vs_hits,
                                (unsigned long long)code_hits,
                                (unsigned long long)last_hash);
                    ud_hits = ud_checks = ring_hits = vs_hits = code_hits = 0;
                }
            }
            ring.clear();
            if (constant_ring && !BbToggle::Disabled(BbToggle::ConstantRing)) {
                CollectRingBindings(*stage, used_prepared, ring);
            } else {
                untracked_reads_packet = draw_pipe->NextPacket(); // bindings not listed
            }
            ++num_stages;
            size += sizeof(PacketStage) +
                    AlignPacket(u32(stage->user_data.size() + stage->flattened_ud_buf.size()) *
                                sizeof(u32)) +
                    u32(ring.size() * sizeof(RingBinding));
        }
    }
    if (cs) {
        size += AlignPacket(sizeof(AmdGpu::ComputeProgram));
    }
    if (indirect) {
        size += AlignPacket(sizeof(IndirectDraw));
        // The arguments are read on the recording thread.
        const u64 args_size = u64(std::max(indirect->stride, 4u)) * std::max(indirect->max_count, 1u);
        if (args_size <= 16_MB) {
            NotePendingRead(indirect->args, args_size);
        } else {
            untracked_reads_packet = draw_pipe->NextPacket();
        }
        if (indirect->count) {
            NotePendingRead(indirect->count, sizeof(u32));
        }
    }
    // bbport: the V#s of the vertex streams, read here from the tables the user data points to:
    // those tables are what the game's constant engine dumps (a ring of ~25 pages rewritten
    // ~14000 times a second). Read on the recording thread instead, a later dump had often
    // replaced them by then: depth of field drew its passes with another draw's vertices (a
    // flipped copy of the scene, streaks and blocks in the sky; issue #44). The streams' data and
    // the index buffer are still read there: a DumpConstRam over them waits for this packet.
    thread_local boost::container::static_vector<AmdGpu::Buffer, Shader::NUM_BUFFERS> vsharps;
    vsharps.clear();
    if (pipeline && !pipeline->IsCompute()) {
        const auto* graphics = pipeline;
        if (const auto& fetch = graphics->GetFetchShader();
            fetch && fetch->attributes.size() <= vsharps.capacity()) {
            const auto& vs = graphics->GetStage(Shader::SwStage::Vertex);
            for (const auto& attrib : fetch->attributes) {
                const auto buffer = attrib.GetSharp(vs);
                vsharps.push_back(buffer);
                if (buffer.base_address != 0 && buffer.GetSize() > 0) {
                    if (BbCeStats::Enabled()) {
                        BbCeStats::Check(BbCeStats::Vertex, buffer.base_address, buffer.GetSize());
                    }
                    NotePendingRead(buffer.base_address, buffer.GetSize());
                }
            }
        }
        if (is_indexed) {
            const auto& regs = liverpool->regs;
            const u32 index_size =
                regs.index_buffer_type.index_type == AmdGpu::IndexType::Index16 ? 2 : 4;
            const VAddr index_address =
                regs.index_base_address.Address<VAddr>() + u64(index_offset) * index_size;
            const u64 index_bytes = u64(regs.num_indices) * index_size;
            if (BbCeStats::Enabled()) {
                BbCeStats::Check(BbCeStats::Index, index_address, index_bytes);
            }
            NotePendingRead(index_address, index_bytes);
        }
    }
    size += AlignPacket(u32(vsharps.size() * sizeof(AmdGpu::Buffer)));
    const u32 interval = VerifyInterval();
    const bool verify = interval && (draw_pipe->packets % interval) == 0;
    if (verify) {
        size += sizeof(AmdGpu::Regs);
    }

    u8* out = draw_pipe->Begin(size);
    u8* const start = out;
    auto& packet = *reinterpret_cast<DrawPacket*>(out);
    packet.kind = cs         ? (indirect ? PacketKind::IndirectDispatch : PacketKind::Dispatch)
                  : indirect ? PacketKind::Indirect
                  : pipeline ? PacketKind::Draw
                             : PacketKind::Special;
    packet.pipeline = pipeline;
    packet.prepared = used_prepared;
    packet.index_offset = index_offset;
    packet.is_indexed = is_indexed;
    packet.regs_reset = dirty.reset;
    packet.num_blocks = static_cast<u16>(num_blocks);
    packet.num_stages = num_stages;
    packet.verify = verify;
    packet.cb_extent = liverpool->last_cb_extent;
    packet.db_extent = liverpool->last_db_extent;
    packet.ring_end = constant_ring ? constant_ring->Position() : 0;
    packet.num_vsharps = static_cast<u32>(vsharps.size());
    out += sizeof(DrawPacket);
    std::memcpy(out, blocks.data(), num_blocks * sizeof(u16));
    out += AlignPacket(num_blocks * sizeof(u16));
    for (u32 i = 0; i < num_blocks; ++i) {
        std::memcpy(out,
                    liverpool->regs.reg_array.data() + blocks[i] * AmdGpu::RegDirty::BlockWords,
                    AmdGpu::RegDirty::BlockWords * sizeof(u32));
        out += AmdGpu::RegDirty::BlockWords * sizeof(u32);
    }
    u32 stage_index = 0;
    for (const auto* stage : stages) {
        if (!stage) {
            continue;
        }
        const auto& ring = rings[stage_index++];
        auto& header = *reinterpret_cast<PacketStage*>(out);
        header = {stage,
                  stage->pgm_base,
                  static_cast<u32>(stage->user_data.size()),
                  static_cast<u32>(stage->flattened_ud_buf.size()),
                  static_cast<u32>(ring.size()),
                  0};
        out += sizeof(PacketStage);
        std::memcpy(out, ring.data(), ring.size() * sizeof(RingBinding));
        out += ring.size() * sizeof(RingBinding);
        std::memcpy(out, stage->user_data.data(), stage->user_data.size_bytes());
        std::memcpy(out + stage->user_data.size_bytes(), stage->flattened_ud_buf.data(),
                    stage->flattened_ud_buf.size() * sizeof(u32));
        out += AlignPacket(u32(stage->user_data.size() + stage->flattened_ud_buf.size()) *
                           sizeof(u32));
    }
    if (cs) {
        std::memcpy(out, cs, sizeof(AmdGpu::ComputeProgram));
        out += AlignPacket(sizeof(AmdGpu::ComputeProgram));
    }
    if (indirect) {
        std::memcpy(out, indirect, sizeof(IndirectDraw));
        out += AlignPacket(sizeof(IndirectDraw));
    }
    std::memcpy(out, vsharps.data(), vsharps.size() * sizeof(AmdGpu::Buffer));
    out += AlignPacket(u32(vsharps.size() * sizeof(AmdGpu::Buffer)));
    if (verify) {
        std::memcpy(out, &liverpool->regs, sizeof(AmdGpu::Regs));
        out += sizeof(AmdGpu::Regs);
    }
    dirty.Clear();
    draw_pipe->Commit(static_cast<u32>(out - start));
    PrintPipeStats();
}

void RasterizerCore::RetireSubmission() {
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    while (!pipe_keepalive.empty() && draw_pipe->Reached(pipe_keepalive.front().first)) {
        pipe_keepalive.pop_front();
    }
    if (!draw_pipe->Idle()) {
        pipe_keepalive.emplace_back(draw_pipe->Head(), draw_prep->KeepCurrent());
    }
}

bool RasterizerCore::RunInOrder(OrderedTask task, const void* data, u32 size, u64 toggle,
                            bool reads_guest_memory) {
    if (!UseDrawPipe() || BbToggle::Disabled(toggle)) {
        DrainDrawPipe(DrawPipe::ReasonRasterizer);
        task(*this, static_cast<const u8*>(data));
        return false;
    }
    if (reads_guest_memory) {
        untracked_reads_packet = draw_pipe->NextPacket();
    }
    const u32 total = sizeof(TaskPacket) + AlignPacket(size);
    u8* out = draw_pipe->Begin(total);
    *reinterpret_cast<TaskPacket*>(out) = {PacketKind::Task, size, task};
    std::memcpy(out + sizeof(TaskPacket), data, size);
    FrameCapture::NativeTaskPacket(true,reinterpret_cast<uintptr_t>(draw_pipe.get()),
        draw_pipe->Head(),data,out+sizeof(TaskPacket),size);
    draw_pipe->Commit(total);
    return true;
}

namespace {
// BB_SECTIONS: ordered tasks by function (cycles, count), stage B only.
constexpr std::size_t TaskKinds = 16;
std::array<void*, TaskKinds> task_kinds{};
std::array<u64, TaskKinds> task_cycles{}, task_counts{};
} // namespace

void RasterizerCore::RunDrawPacket(void* context, const u8* data, u32 size) {
    BB_SECTION(Packet);
    auto& self = *static_cast<RasterizerCore*>(context);
    if (*reinterpret_cast<const PacketKind*>(data) == PacketKind::Task) {
        BB_SECTION(Task);
        self.buffer_cache.NewPacket();
        const auto& task = *reinterpret_cast<const TaskPacket*>(data);
        FrameCapture::NativeTaskPacket(false,reinterpret_cast<uintptr_t>(self.draw_pipe.get()),
            self.draw_pipe->Consumed(),nullptr,data+sizeof(TaskPacket),task.size);
        if (BbSections::Enabled()) {
            const u64 t0 = BbCpu::Ticks();
            task.task(self, data + sizeof(TaskPacket));
            void* fn = reinterpret_cast<void*>(task.task);
            for (std::size_t k = 0; k < TaskKinds; ++k) {
                if (task_kinds[k] == fn || !task_kinds[k]) {
                    task_kinds[k] = fn;
                    task_cycles[k] += BbCpu::Ticks() - t0;
                    ++task_counts[k];
                    break;
                }
            }
            return;
        }
        task.task(self, data + sizeof(TaskPacket));
        return;
    }
    const auto& packet = *reinterpret_cast<const DrawPacket*>(data);
    const u8* in = data + sizeof(DrawPacket);
    const auto* blocks = reinterpret_cast<const u16*>(in);
    in += AlignPacket(packet.num_blocks * sizeof(u16));
    auto& regs = self.pipe_regs;
    if (packet.regs_reset) {
        regs.SetDefaults();
    }
    for (u32 i = 0; i < packet.num_blocks; ++i) {
        std::memcpy(regs.reg_array.data() + blocks[i] * AmdGpu::RegDirty::BlockWords, in,
                    AmdGpu::RegDirty::BlockWords * sizeof(u32));
        in += AmdGpu::RegDirty::BlockWords * sizeof(u32);
    }
    ASSERT(packet.num_stages <= Shader::Info::MaxUdSnapshots);
    for (u32 i = 0; i < packet.num_stages; ++i) {
        const auto& stage = *reinterpret_cast<const PacketStage*>(in);
        in += sizeof(PacketStage);
        self.ring_stages[i] = {stage.info, reinterpret_cast<const RingBinding*>(in),
                               stage.num_ring};
        in += stage.num_ring * sizeof(RingBinding);
        const auto* words = reinterpret_cast<const u32*>(in);
        Shader::Info::ud_snapshots[i] = {stage.info, words, stage.ud_size, words + stage.ud_size,
                                         stage.flat_size, stage.pgm_base};
        in += AlignPacket((stage.ud_size + stage.flat_size) * sizeof(u32));
    }
    Shader::Info::num_ud_snapshots = packet.num_stages;
    self.num_ring_stages = packet.num_stages;
    const AmdGpu::ComputeProgram* cs = nullptr;
    if (packet.kind == PacketKind::Dispatch || packet.kind == PacketKind::IndirectDispatch) {
        cs = reinterpret_cast<const AmdGpu::ComputeProgram*>(in);
        in += AlignPacket(sizeof(AmdGpu::ComputeProgram));
    }
    const IndirectDraw* indirect = nullptr;
    if (packet.kind == PacketKind::Indirect || packet.kind == PacketKind::IndirectDispatch) {
        indirect = reinterpret_cast<const IndirectDraw*>(in);
        in += AlignPacket(sizeof(IndirectDraw));
    }
    self.packet_vsharps = {reinterpret_cast<const AmdGpu::Buffer*>(in), packet.num_vsharps};
    in += AlignPacket(u32(packet.num_vsharps * sizeof(AmdGpu::Buffer)));
    if (packet.verify) {
        // The resource tables the GPU thread walked must read the same now: else a write
        // still queued here (WriteData, DMA) changed them after that thread read them.
        static u32 flat_reports = 0;
        for (u32 i = 0; i < packet.num_stages && flat_reports < 32; ++i) {
            const auto& snapshot = Shader::Info::ud_snapshots[i];
            const auto& srt = snapshot.info->srt_info;
            // Stages the pipeline selection did not refresh (no user data) keep their tables.
            if (!srt.walker_func || snapshot.flat_size == 0 || snapshot.user_data_size == 0) {
                continue;
            }
            std::vector<u32> flat(snapshot.flat_size);
            std::memcpy(flat.data(), snapshot.user_data,
                        std::min(snapshot.user_data_size, snapshot.flat_size) * sizeof(u32));
            // Pointers in the tables may be stale by now: a fault only skips the check.
            sigjmp_buf recover;
            if (sigsetjmp(recover, 0)) {
                runtime_fault_recover = nullptr;
                continue;
            }
            runtime_fault_recover = &recover;
            
#if defined(__aarch64__)
            Shader::RunSrtWalker(srt,snapshot.user_data,flat.data());
#else
            srt.walker_func(snapshot.user_data, flat.data());
#endif
            runtime_fault_recover = nullptr;
            if (std::memcmp(flat.data(), snapshot.flat, snapshot.flat_size * sizeof(u32)) != 0) {
                u32 first = 0, count = 0;
                for (u32 w = 0; w < snapshot.flat_size; ++w) {
                    if (flat[w] != snapshot.flat[w]) {
                        first = count++ ? first : w;
                    }
                }
                std::printf("Draw pipe: resource tables of shader %016llx changed after the GPU "
                            "thread read them (%u of %u words, first %u: %#x -> %#x, user data "
                            "%u words)\n",
                            static_cast<unsigned long long>(snapshot.info->pgm_hash), count,
                            snapshot.flat_size, first, snapshot.flat[first], flat[first],
                            snapshot.user_data_size);
                ++flat_reports;
            }
        }
        const auto* live = reinterpret_cast<const AmdGpu::Regs*>(in);
        in += sizeof(AmdGpu::Regs);
        static u32 reports = 0;
        for (u32 block = 0; block < AmdGpu::RegDirty::NumBlocks && reports < 32; ++block) {
            const u32 at = block * AmdGpu::RegDirty::BlockWords;
            if (std::memcmp(regs.reg_array.data() + at, live->reg_array.data() + at,
                            AmdGpu::RegDirty::BlockWords * sizeof(u32)) != 0) {
                for (u32 w = at; w < at + AmdGpu::RegDirty::BlockWords; ++w) {
                    if (regs.reg_array[w] != live->reg_array[w]) {
                        std::printf("Draw pipe: register copy differs at word %#x (%#x vs %#x)\n",
                                    w, regs.reg_array[w], live->reg_array[w]);
                        ++reports;
                        break;
                    }
                }
            }
        }
    }
    ASSERT(static_cast<u32>(in - data) == size);
    stage_regs = &regs;
    self.pipe_cb_extent = packet.cb_extent;
    self.pipe_db_extent = packet.db_extent;
    if (packet.kind == PacketKind::Special) {
        // A draw FilterDraw handles (fast clear elimination, resolve, depth copy, skip).
        FrameCapture::Poll();
        self.backend.PopPendingOperations();
        self.FilterDraw();
    } else if (cs) {
        stage_cs = cs;
        const auto* pipeline = packet.pipeline;
        if (indirect) {
            self.DispatchIndirectRecord(pipeline, indirect->args, indirect->stride);
        } else {
            self.DispatchRecord(pipeline);
        }
        stage_cs = nullptr;
    } else if (indirect) {
        self.DrawIndirectRecord(packet.pipeline,
                                packet.is_indexed, *indirect);
    } else {
        self.DrawRecord(packet.pipeline, packet.prepared,
                        packet.is_indexed, packet.index_offset);
    }
    Shader::Info::num_ud_snapshots = 0;
    self.num_ring_stages = 0;
    self.packet_vsharps = {};
    if (self.constant_ring) {
        self.constant_ring->Stamp(packet.ring_end);
    }
}

void RasterizerCore::PrintPipeStats() {
    static const bool stats = std::getenv("BB_FRAME_STATS") != nullptr;
    if (!stats || (draw_pipe->packets & 1023) != 0) {
        return;
    }
    static auto window = std::chrono::steady_clock::now();
    static u64 last_packets = 0, last_drains = 0, last_drain_cycles = 0, last_busy = 0;
    static u64 last_tsc = BbCpu::Ticks();
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - window).count();
    if (seconds < 5.0) {
        return;
    }
    const u64 tsc = BbCpu::Ticks();
    const double cycles = double(tsc - last_tsc);
    const u64 busy = draw_pipe->busy_cycles.load(std::memory_order_relaxed);
    std::printf("Draw pipe: %.0f draws/s pipelined, %.0f drains/s that waited, stage A waited "
                "%.1f%%, stage B busy %.1f%%\n",
                (draw_pipe->packets - last_packets) / seconds,
                (draw_pipe->drains - last_drains) / seconds,
                100.0 * (draw_pipe->drain_cycles - last_drain_cycles) / cycles,
                100.0 * (busy - last_busy) / cycles);
    std::printf("  drains by reason/s:");
    for (u32 r = 0; r < DrawPipe::NumReasons; ++r) {
        if (const u64 n = draw_pipe->drains_by_reason[r]) {
            static constexpr const char* names[] = {"ConstRam", "commands", "compute",
                                                    "submission end", "draw", "rasterizer"};
            const double share = 100.0 * draw_pipe->cycles_by_reason[r] / cycles;
            draw_pipe->cycles_by_reason[r] = 0;
            if (r < 256) {
                std::printf(" %s=%.0f (%.1f%%)",
                            magic_enum::enum_name(AmdGpu::PM4ItOpcode(r)).data(), n / seconds,
                            share);
            } else {
                std::printf(" %s=%.0f (%.1f%%)", names[r - 256], n / seconds, share);
            }
        }
    }
    if (constram_waits_skipped) {
        std::printf(" ConstRam not waited=%.0f", constram_waits_skipped / seconds);
        constram_waits_skipped = 0;
    }
    if (BbSections::Enabled()) {
        static std::array<u64, BbSections::Count> last_sections{};
        const double packets = double(draw_pipe->packets - last_packets);
        const double tsc_hz = cycles / seconds;
        std::printf("\n  stage B us per packet:");
        for (u32 s = 0; s < BbSections::Count; ++s) {
            const u64 now_cycles = BbSections::cycles[s].load(std::memory_order_relaxed);
            if (packets > 0) {
                std::printf(" %s %.2f", BbSections::Names[s],
                            1e6 * double(now_cycles - last_sections[s]) / tsc_hz / packets);
            }
            last_sections[s] = now_cycles;
        }
        // Racy reads of stage B's counters: diagnostics only.
        std::printf("\n  stage B tasks (us per frame-second, count/s):");
        static std::array<u64, TaskKinds> last_task_cycles{}, last_task_counts{};
        for (std::size_t k = 0; k < TaskKinds && task_kinds[k]; ++k) {
            Dl_info info{};
            dladdr(task_kinds[k], &info);
            char name[32];
            std::snprintf(name, sizeof(name), "+0x%llx",
                          (unsigned long long)(reinterpret_cast<u64>(task_kinds[k]) -
                                               reinterpret_cast<u64>(info.dli_fbase)));
            const u64 c = task_cycles[k], n = task_counts[k];
            std::printf(" [%s %.0f us/s %.0f/s]", name,
                        1e6 * double(c - last_task_cycles[k]) / tsc_hz / seconds,
                        double(n - last_task_counts[k]) / seconds);
            last_task_cycles[k] = c;
            last_task_counts[k] = n;
        }
    }
    std::printf("\n  stage A waits over 0.5%%:");
    for (const auto& [function, lines] : drain_sites) {
        for (const auto& [line, waited] : lines) {
            if (waited * 200 > cycles) {
                std::printf(" %s:%u %.1f%%", function, line, 100.0 * waited / cycles);
            }
        }
    }
    std::printf("\n");
    drain_sites.clear();
    draw_pipe->drains_by_reason = {};
    window = now;
    last_tsc = tsc;
    last_packets = draw_pipe->packets;
    last_drains = draw_pipe->drains;
    last_drain_cycles = draw_pipe->drain_cycles;
    last_busy = busy;
}

const PreparedStage* RasterizerCore::FindPreparedStage(const Shader::Info& stage) const {
    // Sharps a draw-preparation worker read from the same flattened user data.
    if (!bind_prepared || BbToggle::Disabled(BbToggle::PreparedResources)) {
        return nullptr;
    }
    for (u32 i = 0; i < bind_prepared->num_stages; ++i) {
        const auto& candidate = bind_prepared->stages[i];
        if (&candidate.program->info == &stage &&
            candidate.num_images == stage.images.size() &&
            candidate.num_samplers == stage.samplers.size() &&
            candidate.num_buffers == stage.buffers.size()) {
            return &candidate;
        }
    }
    return nullptr;
}

bool RasterizerCore::BindHelperWanted() {
    // A spinning helper pays off only with cores to spare (the recording thread spins too).
    const char* env = std::getenv("BB_TEXTURE_HELPER");
    if (env && env[0]) {
        return env[0] == '1';
    }
    // Measured neutral on 16 threads (docs/parallel_gpu.md): opt-in.
    return false;
}

bool RasterizerCore::HelperEligible(const PipelineBase* pipeline) const {
    if (pipeline->IsCompute() || !bind_helper.Available() ||
        (FrameCapture::Active() && !FrameCapture::TextureBindingsEnabled()) ||
        BbToggle::Disabled(BbToggle::TextureBindHelper) ||
        BbToggle::Disabled(BbToggle::TextureBindingMemo) ||
        BbToggle::Disabled(BbToggle::UpdateImageFastPath)) {
        return false;
    }
    for (const auto* stage : pipeline->GetStages()) {
        if (!stage) {
            continue;
        }
        // Mip arrays make the descriptor layout depend on the T#s.
        for (const auto& image : stage->images) {
            if (image.mip_fallback_mode != Shader::MipStorageFallbackMode::None) {
                return false;
            }
        }
        // Storage writes invalidate cached images from the buffer side.
        for (const auto& buffer : stage->buffers) {
            if (!buffer.IsSpecial() && buffer.is_written) {
                return false;
            }
        }
    }
    return true;
}

bool RasterizerCore::TexturesBindableOnHelper(const Shader::Info& stage,
                                          const PreparedStage* prepared) {
    const u64 generation = texture_cache.RegistryGeneration();
    for (u32 image_index = 0; image_index < stage.images.size(); ++image_index) {
        const auto& image_desc = stage.images[image_index];
        if (image_desc.is_written) {
            return false;
        }
        const auto tsharp =
            prepared ? prepared->image_sharps[image_index] : image_desc.GetSharp(stage);
        const auto data_fmt = tsharp.GetDataFmt();
        const auto num_fmt = tsharp.GetNumberFmt();
        if (tsharp.Address() == 0 || data_fmt == AmdGpu::DataFormat::FormatInvalid ||
            !memory->IsValidGpuMapping(tsharp.Address(), 0) ||
            !IsKnownFormat(data_fmt, num_fmt)) {
            continue; // null descriptor
        }
        const auto& entry =
            prepared ? CachedImageDescEntry(tsharp, image_desc, prepared->image_hashes[image_index])
                     : CachedImageDescEntry(tsharp, image_desc);
        if (entry.found_generation != generation) {
            return false;
        }
        VideoCore::ImageId image_id = entry.found_id;
        auto* image = &texture_cache.GetImage(image_id);
        if (const auto depth_image_id = texture_cache.GetAssociatedDepth(*image)) {
            image_id = depth_image_id;
            image = &texture_cache.GetImage(image_id);
        }
        if (image->binding.needs_rebind || !texture_cache.IsUpToDate(image_id) ||
            scene_targets->Tracks(image->image_uid) ||
            (upscaler_enabled && upscaler->RedirectsSampled(image_id))) {
            return false;
        }
    }
    return true;
}

void RasterizerCore::RunTextureTask(void* context) {
    auto& self = *static_cast<RasterizerCore*>(context);
    auto& task = self.texture_task;
    for (u32 i = 0; i < task.count; ++i) {
        auto& stage = task.stages[i];
        if (!self.TexturesBindableOnHelper(*stage.info, stage.prepared)) {
            return;
        }
        auto binding = stage.binding;
        u32 write_index = stage.write_index;
        self.BindTextures(*stage.info, stage.prepared, binding, write_index, task.barrier, true);
        task.completed = i + 1;
    }
}

void RasterizerCore::JoinBindHelper(void* context) {
    if (!BindHelper::OnHelper()) {
        static_cast<RasterizerCore*>(context)->bind_helper.Join();
    }
}

void RasterizerCore::DrawIndirectRecord(const PipelineBase* pipeline, bool is_indexed,
                                        const IndirectDraw& indirect) {
    buffer_cache.NewPacket();
    if (DrawPipe::OnStageB()) {
        FrameCapture::Poll();
        backend.PopPendingOperations();
    }
    const VAddr count_address = indirect.count;
    const u32 stride = indirect.stride;
    const u32 max_count = indirect.max_count;

    // Indirect arguments may be GPU-written: leave these draws on camera fallback.
    motion_draw = false;
    motion_geometry = 0;
    PrepareRenderState(pipeline);
    if (upscaler_enabled && std::popcount(pipeline->GetGraphicsKey().mrt_mask) == 1) {
        const auto& viewport = Regs().viewports[0];
        upscaler->OnDraw(pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                         cb_descs[0].first, db_desc.first,
                         UiComposition::NativeViewport(viewport.xscale * 2, viewport.yscale * 2));
    }
    if (!BindResources(pipeline)) {
        return;
    }
    const auto state = BeginRendering(pipeline);

    // The vertex buffers are recorded before the index buffer is resolved (which can record an
    // upload of its own).
    ResolveVertexBuffers(pipeline, nullptr);
    EmitDrawBuffers(true, false);
    if (is_indexed) {
        ResolveIndexBuffer(0);
        EmitDrawBuffers(false, true);
    }

    const auto [buffer, base] =
        buffer_cache.ObtainBuffer(indirect.args, stride * max_count, false);
    IndirectBuffers buffers{.args = buffer, .args_offset = base};
    needs_barrier |= backend.IsBufferAccessed(buffer, base, stride * max_count);
    NoteGuestAccess(indirect.args, stride * max_count, false);

    if (count_address != 0) {
        std::tie(buffers.counts, buffers.counts_offset) =
            buffer_cache.ObtainBuffer(count_address, 4, false);
        needs_barrier |= backend.IsBufferAccessed(buffers.counts, buffers.counts_offset, 4);
        NoteGuestAccess(count_address, 4, false);
    }

    if (needs_barrier) {
        backend.FlushBarriers();
    }

    // bbport: screen-space (clip disabled) draws into the upscaler's output-size images.
    push_data.xscale *= target_scale[0];
    push_data.xoffset *= target_scale[0];
    push_data.yscale *= target_scale[1];
    push_data.yoffset *= target_scale[1];
    draw_jitter = {};
    if (upscaler_enabled && db_desc.first && db_desc.first == camera_motion->Depth()) {
        draw_jitter = upscaler->Jitter();
    }
    DynamicDrawState dynamic;
    UpdateDynamicState(dynamic, is_indexed);
    RecordDrawIndirect(pipeline, state, is_indexed, indirect, buffers, dynamic);
    DebugState.IncDrawCall();

    ResetBindings(false);
    queue.KickRecording();
}

void RasterizerCore::DispatchRecord(const PipelineBase* pipeline) {
    BB_SECTION(DispatchRecord);
    buffer_cache.NewPacket();
    FrameCapture::Poll();
    gbuffer_draw = false;

    backend.PopPendingOperations();

    const auto& cs_program = CsRegs();

    const auto& cs = pipeline->GetStage(Shader::SwStage::Compute);
    if (upscaler_enabled) {
        upscaler->OnDispatch(cs.pgm_hash);
    }
    bool hle;
    {
        BB_SECTION(ShaderHle);
        hle = ExecuteShaderHLE(cs, cs_program);
    }
    if (hle) {
        return;
    }
    // bbport: the game's copy shader running as itself (BB_COPY_SHADER_NATIVE): its destinations stay in
    // the game's memory. They hold data our translator reads on the CPU (next to shader code the
    // game uploads this way); a VRAM copy of them showed it a stale copy (a black scene). The
    // copy list read on the CPU (vk_shader_hle.cpp) kept them in place too.
    buffer_cache.force_writes_in_place =
        Game::BufferCopyShader() != 0 && cs.pgm_hash == Game::BufferCopyShader();
    const bool bound = BindResources(pipeline, true);
    buffer_cache.force_writes_in_place = false;
    if (!bound) {
        return;
    }
    if (HoistDispatch(pipeline, cs, cs_program)) {
        return;
    }

    if (needs_barrier) {
        backend.FlushBarriers();
    }

    backend.EndRendering();
    RecordDispatch(pipeline, cs, cs_program);
    DebugState.IncDispatch();

    ResetBindings(true);
    queue.KickRecording();
}

void RasterizerCore::DispatchIndirectRecord(const PipelineBase* pipeline, VAddr address,
                                            u32 size) {
    buffer_cache.NewPacket();
    backend.PopPendingOperations();
    const u32 offset = 0;

    if (!BindResources(pipeline)) {
        return;
    }

    const auto [buffer, base] = buffer_cache.ObtainBuffer(address + offset, size, false);
    needs_barrier |= backend.IsBufferAccessed(buffer, base, size);

    if (needs_barrier) {
        backend.FlushBarriers();
    }

    backend.EndRendering();
    RecordDispatchIndirect(pipeline, address, size, buffer, base);
    DebugState.IncDispatch();

    ResetBindings(true);
    queue.KickRecording();
}


namespace {
std::pair<u32, u32> GetDrawOffsets(
    const AmdGpu::Regs& regs, const Shader::Info& info,
    const std::optional<Shader::Gcn::FetchShaderData>& fetch_shader) {
    u32 vertex_offset = regs.index_offset;
    u32 instance_offset = 0;
    if (fetch_shader) {
        if (vertex_offset == 0 && fetch_shader->vertex_offset_sgpr != -1) {
            vertex_offset = info.UserData()[fetch_shader->vertex_offset_sgpr];
        }
        if (fetch_shader->instance_offset_sgpr != -1) {
            instance_offset = info.UserData()[fetch_shader->instance_offset_sgpr];
        }
    }
    return {vertex_offset, instance_offset};
}
} // namespace

void RasterizerCore::DrawRecord(const PipelineBase* pipeline, const PreparedDraw* used_prepared,
                                bool is_indexed, u32 index_offset) {
    BB_SECTION(DrawRecord);
    buffer_cache.NewPacket();
    if (DrawPipe::OnStageB()) {
        FrameCapture::Poll();
        backend.PopPendingOperations();
    }
    // bbport (BB_PREUPLOAD): once a frame, guest GPU memory the CPU wrote and then left alone is
    // uploaded ahead of its use, BB_PREUPLOAD_MB (16) at most per frame.
    if (const u32 frame = BbStats::frame_number.load(std::memory_order_relaxed);
        frame != preupload_frame) {
        preupload_frame = frame;
        BB_SECTION(Preupload);
        static const u64 budget = [] {
            const char* env = std::getenv("BB_PREUPLOAD_MB");
            return (env ? std::strtoull(env, nullptr, 10) : 16ull) << 20;
        }();
        buffer_cache.Preupload(budget);
    }
    const auto& regs = Regs();
    // bbport: the pass copying a finished frame to a display buffer; the previous draw's
    // target is that frame. (On Vulkan the upscaler being enabled implies camera motion is; a
    // backend with an upscaler and no camera motion still needs the frame start.)
    if ((camera_enabled || upscaler_enabled) && regs.color_buffers[0] &&
        FrameCapture::IsDisplayBuffer(regs.color_buffers[0].Address())) {
        scene_started = false;
        camera_motion->OnDisplayPass(cb_descs[0].first);
        if (upscaler->OnFrameStart()) {
            object_motion->InvalidateHistory();
            camera_motion->InvalidateHistory();
        }
        camera_motion->SetJitter(upscaler->Jitter());
        object_motion->OnFrameStart();
        NoteFrameStart();
        static const char* scene_debug = std::getenv("BB_SCENE_DEBUG");
        scene_debug_frame = scene_debug && std::remove(scene_debug) == 0;
        scene_targets->SetDebug(scene_debug_frame);
    }
    bind_prepared = used_prepared;
    motion_draw = pipeline->GetGraphicsKey().motion_vectors;
    motion_geometry = 0;

    PrepareRenderState(pipeline);
    if (upscaler_enabled && std::popcount(pipeline->GetGraphicsKey().mrt_mask) == 1) {
        const auto& viewport = Regs().viewports[0];
        upscaler->OnDraw(pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                         cb_descs[0].first, db_desc.first,
                         UiComposition::NativeViewport(viewport.xscale * 2, viewport.yscale * 2));
    }
    const PreparedDraw* draw_prepared = bind_prepared;
    // bbport: vertex and index buffers are resolved while the helper binds textures (their
    // commands are recorded after BeginRendering, as before).
    draw_inputs = {pipeline, draw_prepared, index_offset, is_indexed, true, false};
    const bool bound = BindResources(pipeline);
    bind_prepared = nullptr; // indirect draws and dispatches bind without prepared sharps
    const bool inputs_resolved = draw_inputs.resolved;
    draw_inputs.pending = false;
    if (!bound) {
        return;
    }
    const auto state = BeginRendering(pipeline);
    if (DrawsNothing(pipeline, state)) {
        // Keeps the image layouts the binding noted in step with the command stream.
        if (needs_barrier) {
            backend.FlushBarriers();
        }
        ResetBindings(false);
        queue.KickRecording();
        return;
    }

    if (!inputs_resolved) {
        ResolveVertexBuffers(pipeline, draw_prepared);
        if (is_indexed) {
            ResolveIndexBuffer(index_offset);
        }
    }
    EmitDrawBuffers(true, is_indexed);

    if (needs_barrier) {
        backend.FlushBarriers();
    }

    // bbport: screen-space (clip disabled) draws into the upscaler's output-size images.
    push_data.xscale *= target_scale[0];
    push_data.xoffset *= target_scale[0];
    push_data.yscale *= target_scale[1];
    push_data.yoffset *= target_scale[1];
    if (motion_draw && motion_geometry) {
        BB_SECTION(Motion);
        const auto& vs = pipeline->GetStage(Shader::SwStage::Vertex);
        const auto [base_vertex, first_instance] =
            GetDrawOffsets(regs, vs, pipeline->GetFetchShader());
        const u32 index_size =
            regs.index_buffer_type.index_type == AmdGpu::IndexType::Index16 ? 2u : 4u;
        const VAddr index_address = is_indexed
            ? regs.index_base_address.Address<VAddr>() + u64(index_offset) * index_size : 0;
        // Without a character-size skeleton the draw is gated on its bone palette changing
        // since last frame (model constants follow the camera, the palette does not). The
        // check comes first: most gated draws are static world pieces with long index lists.
        static const bool all_motion = [] {
            const char* value = std::getenv("BB_OBJECT_MOTION_ALL");
            return value && value[0] == '1';
        }();
        bool gated = !all_motion;
        u64 palette = 0;
        for (const auto& resource : vs.buffers) {
            if (resource.IsSpecial()) continue;
            const auto buffer = resource.GetSharp(vs);
            const u32 size = buffer.GetSize();
            if (!buffer.Valid() || buffer.GetStride() != 16) continue;
            const auto role = Motion::ClassifyBuffer(size);
            if (role == Motion::BufferRole::Skeleton) {
                gated = false;
                break;
            }
            const VAddr address = buffer.base_address;
            if (role == Motion::BufferRole::SmallSkeleton &&
                memory->IsValidGpuMapping(address, 0) &&
                memory->ClampRangeSize(address, size) == size) {
                palette = XXH3_64bits_withSeed(reinterpret_cast<const void*>(address), size,
                                               palette);
            }
        }
        push_data.motion_param = 0;
        if (!gated || object_motion->Moving({.shader = vs.pgm_hash,
                                             .geometry = motion_geometry,
                                             .indices = index_address,
                                             .index_count = regs.num_indices,
                                             .instances = regs.num_instances.NumInstances(),
                                             .first_instance = first_instance},
                                            palette)) {
            Motion::VertexRange range{base_vertex, regs.num_indices};
            u64 topology = 0;
            if (is_indexed) {
                range = {};
                const u64 bytes = u64(regs.num_indices) * index_size;
                if (index_address && memory->IsValidGpuMapping(index_address, 0) &&
                    memory->ClampRangeSize(index_address, bytes) == bytes) {
                    const bool restart = regs.enable_primitive_restart != 0;
                    const u32 count = regs.num_indices;
                    const auto scanned = object_motion->IndexRange(
                        {index_address, count, index_size, s32(base_vertex), restart}, [&] {
                            const auto* data = reinterpret_cast<const void*>(index_address);
                            const auto scan_range =
                                index_size == 2
                                    ? Motion::IndexedRange(
                                          std::span(static_cast<const u16*>(data), count),
                                          s32(base_vertex), restart)
                                    : Motion::IndexedRange(
                                          std::span(static_cast<const u32*>(data), count),
                                          s32(base_vertex), restart);
                            return Motion::IndexRangeCache::Result{scan_range,
                                                                   XXH3_64bits(data, bytes)};
                        });
                    range = scanned.range;
                    topology = scanned.topology;
                }
            }
            push_data.motion_param = object_motion->PrepareDraw({
                .shader = vs.pgm_hash,
                .geometry = motion_geometry,
                .indices = index_address,
                .topology = topology,
                .index_count = regs.num_indices,
                .instances = regs.num_instances.NumInstances(),
                .first_instance = first_instance,
                .vertices = range,
            });
        }
    }
    // bbport: jitter geometry drawn with the scene depth, not full-screen passes (a shifted
    // full-screen quad leaves an edge column unwritten).
    draw_jitter = {};
    if (upscaler_enabled && db_desc.first && db_desc.first == camera_motion->Depth() &&
        (regs.num_indices > 6 || regs.num_instances.NumInstances() > 1)) {
        draw_jitter = upscaler->Jitter();
    }

    const auto& vs_info = pipeline->GetStage(Shader::SwStage::Vertex);
    const auto& fetch_shader = pipeline->GetFetchShader();
    const auto [vertex_offset, instance_offset] = GetDrawOffsets(regs, vs_info, fetch_shader);

    const u32 num_indices = regs.num_indices;
    const u32 num_instances = regs.num_instances.NumInstances();
    // BBDIAG BB_DRAW_DIAG=<vs hash>: the draws of that vertex shader.
    static const u64 draw_diag = [] {
        const char* env = std::getenv("BB_DRAW_DIAG");
        return env ? std::strtoull(env, nullptr, 16) : 0ull;
    }();
    static const bool geo_diag = std::getenv("BB_GEO_DIAG") != nullptr;
    if (geo_diag) {
        // BBDIAG: indices drawn per frame by the number of color targets of the pass.
        static std::array<std::atomic<u64>, 9> idx{}, cnt{};
        static std::atomic<u64> last_frame{0};
        static std::atomic<s64> last_ns{0};
        const u32 c = std::min<u32>(state.num_color_attachments, 8);
        idx[c].fetch_add(u64(num_indices) * num_instances, std::memory_order_relaxed);
        cnt[c].fetch_add(1, std::memory_order_relaxed);
        const s64 now = std::chrono::steady_clock::now().time_since_epoch().count();
        s64 last = last_ns.load();
        if (now - last > 5'000'000'000ll && last_ns.compare_exchange_strong(last, now)) {
            const u64 frame = g_bb_flip_count.load();
            const double frames = double(std::max<u64>(1, frame - last_frame.exchange(frame)));
            std::printf("BBDIAG geometry per frame:");
            for (u32 i = 0; i < 9; ++i) {
                std::printf(" C%u %.0f draws %.0fk idx;", i, cnt[i].exchange(0) / frames,
                            idx[i].exchange(0) / frames / 1000.0);
            }
            std::printf("\n");
        }
    }
    if (draw_diag && vs_info.pgm_hash == draw_diag) {
        static std::atomic<int> logged{0};
        if (logged.fetch_add(1) % 97 == 0) {
            const auto* ps_stage = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
            std::printf("BBDIAG draw vs %016llx: %ux%u indices %u instances %u prim %u indexed %d "
                        "colors %u depth %d ps %016llx stages %d%d%d%d%d\n",
                        (unsigned long long)draw_diag, state.width, state.height, num_indices,
                        num_instances, u32(regs.primitive_type), is_indexed,
                        state.num_color_attachments, state.depth_stencil_attachment.has_depth,
                        (unsigned long long)(ps_stage ? ps_stage->pgm_hash : 0),
                        pipeline->GetStages()[0] != nullptr, pipeline->GetStages()[1] != nullptr,
                        pipeline->GetStages()[2] != nullptr, pipeline->GetStages()[3] != nullptr,
                        pipeline->GetStages()[4] != nullptr);
        }
    }
    const DrawCall draw{
        .num_indices = num_indices,
        .num_instances = num_instances,
        .first_vertex = vertex_offset,
        .first_instance = instance_offset,
        .is_indexed = is_indexed,
    };
    DynamicDrawState dynamic;
    UpdateDynamicState(dynamic, is_indexed);
    RecordDraw(pipeline, state, draw, dynamic);
    if (FrameCapture::Active()) {
        const auto* ps = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
        FrameCapture::Draw(vs_info.pgm_hash, ps ? ps->pgm_hash : 0, num_indices, num_instances);
        if (FrameCapture::TextureBindingsEnabled() && FrameCapture::NativeRecording())
            FrameCapture::Native(fmt::format("{{\"op\":\"texture_draw\",\"core\":{},\"vs\":{},\"ps\":{},\"indices\":{},\"instances\":{}}}",
                uintptr_t(this),vs_info.pgm_hash,ps?ps->pgm_hash:0,num_indices,num_instances));
        if (const auto& bc = regs.blend_control[0]; bc.enable && regs.color_buffers[0]) {
            char note[128];
            std::snprintf(note, sizeof(note), "\n  blend ps %08x idx %u: src %u dst %u func %u z %u%u",
                          u32(ps ? ps->pgm_hash : 0), num_indices, u32(bc.color_src_factor),
                          u32(bc.color_dst_factor), u32(bc.color_func),
                          u32(regs.depth_control.depth_enable),
                          u32(regs.depth_control.depth_write_enable));
            FrameCapture::Note(note);
        }
    }
    DebugState.IncDrawCall();

    BB_SECTION(Kick);
    ResetBindings(false);
    queue.KickRecording();
}

bool RasterizerCore::HoistDispatch(const PipelineBase* pipeline, const Shader::Info& cs,
                                   const AmdGpu::ComputeProgram& cs_program) {
    // The held pass's draws so far are all in its sets (ResetBindings after each); images are
    // not checked here (none bound), nor anything bound without a known range.
    // BB_HOIST_DIAG=1 (diagnostics): why dispatches stay in the pass, every 5 s.
    static const bool diag = std::getenv("BB_HOIST_DIAG") != nullptr;
    static std::atomic<u64> why[9];
    static std::atomic<s64> why_last{0};
    const auto fail = [&](int reason) {
        if (!diag) {
            return false;
        }
        why[reason].fetch_add(1, std::memory_order_relaxed);
        const s64 now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
        s64 last = why_last.load();
        if (now - last >= 5 && why_last.compare_exchange_strong(last, now)) {
            std::printf("BBDIAG hoist: ok=%llu nohold=%llu serial=%llu untracked=%llu images=%llu "
                        "pending=%llu raw=%llu war=%llu pass_image=%llu\n",
                        (unsigned long long)why[0].load(), (unsigned long long)why[1].load(),
                        (unsigned long long)why[2].load(), (unsigned long long)why[3].load(),
                        (unsigned long long)why[4].load(), (unsigned long long)why[5].load(),
                        (unsigned long long)why[6].load(), (unsigned long long)why[7].load(),
                        (unsigned long long)why[8].load());
            std::printf("BBDIAG hoist last fail: shader %016llx reason %d\n",
                        (unsigned long long)cs.pgm_hash, reason);
        }
        return false;
    };
    if (!queue.HoldingPass()) {
        return fail(1);
    }
    if (pass_serial != queue.HeldPassSerial()) {
        return fail(2);
    }
    if (pass_untracked) {
        return fail(3);
    }
    // Images: only ones the pass's draws have not bound (nor render to); their layout changes
    // go before the pass with the dispatch.
    static const bool hoist_images = [] {
        const char* env = std::getenv("BB_HOIST_IMAGES");
        return !(env && env[0] == '0');
    }();
    if (!bound_images.empty()) {
        if (!hoist_images) {
            return fail(4);
        }
        // An image the pass's draws only sampled: the dispatch may sample it too, unchanged
        // (no layout change pending, the dispatch writes no image).
        bool writes_images = false;
        for (const auto& desc : cs.images) {
            writes_images |= desc.is_written;
        }
        for (const auto image_id : bound_images) {
            if (!InPassImages(image_id.index)) {
                continue;
            }
            if (writes_images || backend.HasPendingImageBarriers() ||
                !InPassReadImages(image_id.index)) {
                static std::atomic<int> logged{0};
                if (diag && logged.fetch_add(1) < 200) {
                    const auto& image = texture_cache.GetImage(image_id);
                    std::printf("BBDIAG pass_image: cs %016llx writes=%d pending=%d passread=%d "
                                "image %u %ux%u fmt %s layout %s groups %ux%ux%u nimg %zu\n",
                                (unsigned long long)cs.pgm_hash, writes_images,
                                backend.HasPendingImageBarriers(),
                                InPassReadImages(image_id.index),
                                image_id.index, image.info.size.width, image.info.size.height,
                                vk::to_string(image.info.pixel_format).c_str(),
                                vk::to_string(image.backing ? image.backing->state.layout
                                                            : vk::ImageLayout::eUndefined).c_str(),
                                cs_program.dim_x, cs_program.dim_y, cs_program.dim_z,
                                bound_images.size());
                }
                return fail(8);
            }
        }
    }
    if (pending_untracked) {
        // The copy shader reaches the game's memory paged: its control list has the ranges.
        const u64 copy_shader = Game::BufferCopyShader();
        if (copy_shader == 0 || cs.pgm_hash != copy_shader ||
            !NoteCopyShaderAccesses(cs, cs_program)) {
            return fail(5);
        }
    }
    for (const auto& access : pending_accesses) {
        const auto range =
            boost::icl::interval<VAddr>::right_open(access.address, access.address + access.size);
        if (boost::icl::intersects(pass_writes, range)) {
            return fail(6);
        }
        if (access.write && boost::icl::intersects(pass_reads, range)) {
            return fail(7);
        }
    }
    if (diag) {
        why[0].fetch_add(1, std::memory_order_relaxed);
    }
    if(FrameCapture::NativeRecording())for(const auto& b:bound_buffers)
        FrameCapture::NativeBufferUse(uintptr_t(b.buffer),b.buffer?uintptr_t(b.buffer->buffer.native):0,
            b.offset,b.size,b.guest_address,b.logical_stage,b.shader,b.is_written,"hoisted_shader");
    RecordHoistedDispatch(pipeline, cs, cs_program);
    DebugState.IncDispatch();
    static std::atomic<u64> hoisted{0};
    if (const u64 n = hoisted.fetch_add(1, std::memory_order_relaxed); n == 0) {
        std::printf("GPU: first dispatch recorded before its render pass (shader %016llx)\n",
                    (unsigned long long)cs.pgm_hash);
    }
    BbStats::hoisted_dispatches.fetch_add(1, std::memory_order_relaxed);
    for (auto& image_id : bound_images) {
        texture_cache.GetImage(image_id).binding = {};
    }
    bound_images.clear();
    bound_buffers.clear();
    needs_barrier = false;
    pending_accesses.clear();
    pending_untracked = false;
    queue.KickRecording();
    return true;
}

bool RasterizerCore::CanHoistDrawWork() {
    // BB_HOIST_DIAG=1 (diagnostics): why draw work stays in the pass, every 5 s.
    static const bool diag = std::getenv("BB_HOIST_DIAG") != nullptr;
    static std::atomic<u64> why[8];
    static std::atomic<s64> why_last{0};
    const auto result = [&](int reason) {
        if (diag) {
            why[reason].fetch_add(1, std::memory_order_relaxed);
            const s64 now = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
            s64 last = why_last.load();
            if (now - last >= 5 && why_last.compare_exchange_strong(last, now)) {
                std::printf("BBDIAG draw hoist: ok=%llu nohold=%llu untracked=%llu "
                            "pass_untracked=%llu image=%llu raw=%llu war=%llu\n",
                            (unsigned long long)why[0].load(), (unsigned long long)why[1].load(),
                            (unsigned long long)why[2].load(), (unsigned long long)why[3].load(),
                            (unsigned long long)why[4].load(), (unsigned long long)why[5].load(),
                            (unsigned long long)why[6].load());
            }
        }
        return reason == 0;
    };
    if (!queue.HoldingPass()) {
        return result(1);
    }
    if (pending_untracked) {
        return result(2);
    }
    // The pass sets belong to an earlier pass until the first draw of this one is reset
    // (ResetBindings): then nothing of this pass was drawn yet.
    if (pass_serial != queue.HeldPassSerial()) {
        return result(0);
    }
    if (pass_untracked) {
        return result(3);
    }
    for (const auto image_id : bound_images) {
        if (!InPassImages(image_id.index)) {
            continue;
        }
        // The draw's own render targets are among its images: the work hoisted for it (vertex
        // and control stages) does not touch them. A target the draw also samples is in the
        // general (feedback) layout and stays a conflict.
        const auto& bound = texture_cache.GetImage(image_id);
        const auto layout = bound.backing ? bound.backing->state.layout : vk::ImageLayout::eUndefined;
        if (layout == vk::ImageLayout::eColorAttachmentOptimal ||
            layout == vk::ImageLayout::eDepthStencilAttachmentOptimal ||
            layout == vk::ImageLayout::eDepthAttachmentOptimal ||
            layout == vk::ImageLayout::eStencilAttachmentOptimal) {
            continue;
        }
        if (backend.HasPendingImageBarriers() ||
            !InPassReadImages(image_id.index)) {
            static std::atomic<int> logged{0};
            if (diag && logged.fetch_add(1) < 12) {
                const auto& image = texture_cache.GetImage(image_id);
                std::printf("BBDIAG draw hoist image: %u %ux%u %s layout %s pending=%d\n",
                            image_id.index, image.info.size.width, image.info.size.height,
                            vk::to_string(image.info.pixel_format).c_str(),
                            vk::to_string(image.backing ? image.backing->state.layout
                                                        : vk::ImageLayout::eUndefined).c_str(),
                            backend.HasPendingImageBarriers());
            }
            return result(4);
        }
    }
    for (const auto& access : pending_accesses) {
        const auto range =
            boost::icl::interval<VAddr>::right_open(access.address, access.address + access.size);
        if (boost::icl::intersects(pass_writes, range)) {
            return result(5);
        }
        if (access.write && boost::icl::intersects(pass_reads, range)) {
            return result(6);
        }
    }
    return result(0);
}

} // namespace VideoCore
