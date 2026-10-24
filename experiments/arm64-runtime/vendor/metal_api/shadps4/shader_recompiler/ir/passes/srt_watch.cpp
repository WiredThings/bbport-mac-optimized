// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader_recompiler/ir/passes/srt_watch.h"
#include "common/arch.h"
#include "video_core/frame_capture.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>
#ifdef ARCH_X86_64
#include <xbyak/xbyak.h>
#endif
#include "common/decoder.h"

namespace Shader {
#ifdef ARCH_X86_64
namespace {
using namespace Xbyak::util;
constexpr size_t PoolBytes=1024*1024;
struct Load {size_t offset{},length{};u32 bytes{};s64 displacement{};bool indexed{};u32 scale{1};};
std::atomic<uintptr_t> pool_begin{},pool_end{};
thread_local u64 current_shader{};
thread_local u32 current_stage{};
void Observe(u64 address,u64 bytes) {
    VideoCore::FrameCapture::NativeCpuAccess(address,bytes,false);
    VideoCore::FrameCapture::NativeBufferUse(0,0,0,bytes,address,current_stage,current_shader,
                                            false,"srt_cpu");
}
void EmitObserver(Xbyak::CodeGenerator& c,const Load& load,u64 watch,u64 bytes,
                  void (*observer)(u64,u64)) {
    // Save flags and the filter temporaries. Relative overlap is unsigned and
    // includes reads beginning just before the watched interval.
    Xbyak::Label skip;
    c.pushfq();c.push(rax);c.push(r11);
    if(load.indexed)c.lea(rax,ptr[rdi+r10*load.scale+load.displacement]);
    else c.lea(rax,ptr[rdi+load.displacement]);
    c.mov(r11,watch-(load.bytes-1));c.sub(rax,r11);
    c.cmp(rax,bytes+load.bytes-2);c.ja(skip,c.T_NEAR);
    // Protect every other caller-saved GPR used by the original straight-line
    // walker. It has no live vector state; the declared SysV function ABI makes
    // vector registers caller-saved. Align independently of its pointer stack.
    c.push(rcx);c.push(rdx);c.push(rdi);c.push(rsi);
    c.push(r8);c.push(r9);c.push(r10);c.push(rbp);
    c.mov(rbp,rsp);c.and_(rsp,-16);
    if(load.indexed)c.lea(rdi,ptr[rdi+r10*load.scale+load.displacement]);
    else c.lea(rdi,ptr[rdi+load.displacement]);
    c.mov(rsi,load.bytes);c.mov(rax,reinterpret_cast<u64>(observer));c.call(rax);
    c.mov(rsp,rbp);c.pop(rbp);c.pop(r10);c.pop(r9);c.pop(r8);
    c.pop(rsi);c.pop(rdi);c.pop(rdx);c.pop(rcx);
    c.L(skip);c.pop(r11);c.pop(rax);c.popfq();
}
}

PFN_SrtWalker BuildSrtWatchClone(Xbyak::CodeGenerator& c,const u8* source,size_t size,
    u64 watch,u64 bytes,void (*observer)(u64,u64),u32& encoded_reads,size_t capacity) {
    encoded_reads=0;
    if(!source || !size || size>16384 || !observer || watch<8 || !bytes ||
       bytes>192 || watch>~u64{0}-bytes)return nullptr;
    std::vector<Load> instructions;
    for(size_t at=0;at<size;) {
        ZydisDecodedInstruction ins{};ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT]{};
        if(!ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(ins,ops,
                const_cast<u8*>(source+at),std::min<size_t>(15,size-at))) ||
           !ins.length || at+ins.length>size)return nullptr;
        // The walker generator uses integer/stack operations only. A closed
        // mnemonic set also rules out hidden vector state, system operations
        // and unfamiliar cached-code conventions instead of guessing liveness.
        switch(ins.mnemonic) {
        case ZYDIS_MNEMONIC_MOV:case ZYDIS_MNEMONIC_PUSH:case ZYDIS_MNEMONIC_POP:
        case ZYDIS_MNEMONIC_RET:case ZYDIS_MNEMONIC_ADD:case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_SHL:case ZYDIS_MNEMONIC_SHR:case ZYDIS_MNEMONIC_AND:
        case ZYDIS_MNEMONIC_OR:case ZYDIS_MNEMONIC_XOR:case ZYDIS_MNEMONIC_IMUL:
        case ZYDIS_MNEMONIC_NEG:case ZYDIS_MNEMONIC_NOT:case ZYDIS_MNEMONIC_DEC:
        case ZYDIS_MNEMONIC_CMP:case ZYDIS_MNEMONIC_CMOVB:case ZYDIS_MNEMONIC_CMOVNBE:
        case ZYDIS_MNEMONIC_ADC:case ZYDIS_MNEMONIC_STC:case ZYDIS_MNEMONIC_LEA:break;
        default:return nullptr;
        }
        if(ins.meta.category==ZYDIS_CATEGORY_CALL || ins.meta.category==ZYDIS_CATEGORY_COND_BR ||
           ins.meta.category==ZYDIS_CATEGORY_UNCOND_BR ||
           (ins.mnemonic==ZYDIS_MNEMONIC_RET && (at+1!=size || source[at]!=0xc3)))return nullptr;
        Load load{at,ins.length};
        for(unsigned i=0;i<ins.operand_count;++i) {
            const auto& op=ops[i];
            if(op.type==ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative)return nullptr;
            if(op.type!=ZYDIS_OPERAND_TYPE_MEMORY)continue;
            if(op.mem.base==ZYDIS_REGISTER_RIP || op.mem.base==ZYDIS_REGISTER_EIP)return nullptr;
            if(op.mem.segment==ZYDIS_REGISTER_FS || op.mem.segment==ZYDIS_REGISTER_GS)return nullptr;
            if(!(op.actions&(ZYDIS_OPERAND_ACTION_MASK_READ|ZYDIS_OPERAND_ACTION_MASK_WRITE)))continue;
            // The generated walkers access only guest pointers, user data,
            // the flattened output and their own stack. Reject other layouts.
            if(op.mem.base==ZYDIS_REGISTER_RSP || op.mem.base==ZYDIS_REGISTER_RSI)continue;
            if(op.mem.base!=ZYDIS_REGISTER_RDI || ins.mnemonic!=ZYDIS_MNEMONIC_MOV || i!=1 ||
               (op.actions&ZYDIS_OPERAND_ACTION_MASK_WRITE) || load.bytes ||
               (op.size!=32 && op.size!=64) || ops[0].type!=ZYDIS_OPERAND_TYPE_REGISTER ||
               (ops[0].reg.value!=ZYDIS_REGISTER_RDI && ops[0].reg.value!=ZYDIS_REGISTER_R10D) ||
               (op.mem.index!=ZYDIS_REGISTER_NONE && op.mem.index!=ZYDIS_REGISTER_R10))return nullptr;
            load.bytes=op.size/8;load.displacement=op.mem.disp.value;
            load.indexed=op.mem.index==ZYDIS_REGISTER_R10;load.scale=op.mem.scale;
            if(load.indexed && load.scale!=1 && load.scale!=2 && load.scale!=4 && load.scale!=8)return nullptr;
            ++encoded_reads;
        }
        instructions.push_back(load);if(instructions.size()>2048)return nullptr;
        at+=ins.length;
    }
    if(source[size-1]!=0xc3 || encoded_reads>512 || c.getSize()>capacity ||
       size+size_t(encoded_reads)*160>capacity-c.getSize())return nullptr;
    const auto fn=c.getCurr<PFN_SrtWalker>();
    for(const auto& load:instructions) {
        if(load.bytes)EmitObserver(c,load,watch,bytes,observer);
        c.db(source+load.offset,load.length);
    }
    c.ready();return fn;
}

bool IsSrtWatchCode(const void* code) {
    const auto value=reinterpret_cast<uintptr_t>(code);
    return value>=pool_begin.load(std::memory_order_acquire) &&
           value<pool_end.load(std::memory_order_acquire);
}
bool TryRunWatchedSrt(const PersistentSrtInfo& info,const u32* ud,u32* flat,u64 shader,u32 stage) {
    if(!VideoCore::FrameCapture::NativeRecording())return false;
    struct Range {u64 address{},size{};};
    static const Range watch=[] {
        const auto* flag=std::getenv("BB_CAPTURE_SRT_READS");
        const auto* text=std::getenv("BB_WATCH");
        if(!flag || flag[0]!='1' || !text)return Range{};
        char* end=nullptr;u64 address=std::strtoull(text,&end,0);
        u64 size=end && *end==','?std::strtoull(end+1,&end,0):0;
        return end && !*end && address>=8 && address<(u64{1}<<40) && size==192 &&
            size<=(u64{1}<<40)-address?Range{address,size}:Range{};
    }();
    if(!watch.size)return false;
    struct Entry {PFN_SrtWalker original{},clone{};size_t size{};u32 reads{};};
    static std::mutex mutex;
    static std::unique_ptr<Xbyak::CodeGenerator> pool;
    static std::array<Entry,256> entries{};static size_t count{};
    Entry selected{};
    {
        std::scoped_lock lock{mutex};
        for(size_t i=0;i<count;++i)if(entries[i].original==info.walker_func &&
            entries[i].size==info.walker_func_size){selected=entries[i];break;}
        if(!selected.original && count<entries.size()) {
            if(!pool) {
                pool=std::make_unique<Xbyak::CodeGenerator>(PoolBytes);
                pool_begin.store(reinterpret_cast<uintptr_t>(pool->getCode()),std::memory_order_release);
            }
            selected={info.walker_func,nullptr,info.walker_func_size,0};
            selected.clone=BuildSrtWatchClone(*pool,reinterpret_cast<const u8*>(info.walker_func),
                info.walker_func_size,watch.address,watch.size,&Observe,selected.reads,PoolBytes);
            pool_end.store(reinterpret_cast<uintptr_t>(pool->getCurr()),std::memory_order_release);
            entries[count++]=selected;
        }
    }
    VideoCore::FrameCapture::NativeSrtWalk(bool(selected.clone),selected.clone?selected.reads:0);
    if(selected.clone)EnsureSrtAccessHandler();
    const auto old_shader=current_shader;const auto old_stage=current_stage;
    current_shader=shader;current_stage=stage;
    (selected.clone?selected.clone:info.walker_func)(ud,flat);
    current_shader=old_shader;current_stage=old_stage;
    return true;
}
#else
bool IsSrtWatchCode(const void*) {return false;}
bool TryRunWatchedSrt(const PersistentSrtInfo&,const u32*,u32*,u64,u32) {return false;}
#endif
}
