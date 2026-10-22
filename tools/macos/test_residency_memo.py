#!/usr/bin/env python3
"""Replay actual NoteUse source against its legacy behavior, with synthetic residency state.

CPU-only differential check. No game, graphics device, saves, settings or caches are opened.
The fixture uses the production interval container and memo, but does not validate GPU timing
or concurrent callers; NoteUse retains the original recording-thread ownership.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

LEGACY = r'''
void Legacy::NoteUse(VAddr address, u64 size) {
    const u32 now = BbStats::coarse_second.load(std::memory_order_relaxed);
    const u64 last = std::min<u64>((address + size - 1) >> USE_GROUP_BITS, group_use.size() - 1);
    for (u64 group = address >> USE_GROUP_BITS; group <= last; ++group) {
        if (group_use[group] != now) group_use[group] = now;
        if (group_demoted[group]) {
            group_demoted[group] = 0;
            const u64 first = (group << USE_GROUP_BITS) >> block_shift;
            const u64 end = ((group + 1) << USE_GROUP_BITS) >> block_shift;
            idle_demoted.ForEachInRange(first, end, [&](const Interval& range) {
                promote_candidates.Add({std::max(first, range.start), std::min(end, range.end)});
            });
            idle_demoted.Subtract(first, end);
        }
    }
}
'''
PREFIX = r'''
#include "common/interval_set.h"
#include "video_core/buffer_cache/residency_use_memo.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <format>
#include <string>
#include <utility>
namespace BbStats { std::atomic<u32> coarse_second{1}; }
namespace FrameCapture {inline bool NativeRecording(){return false;}inline void Native(const std::string&) {}}
struct State {
    static constexpr u64 USE_GROUP_BITS=21, block_shift=14;
    std::vector<u32> group_use=std::vector<u32>(512);
    std::vector<u8> group_demoted=std::vector<u8>(512);
    IntervalList<> idle_demoted, promote_candidates;
    VideoCore::ResidencyUseMemo residency_use_memo;
    void Demote(u64 block, bool memo) {
        if(memo)residency_use_memo.Invalidate();
        idle_demoted.Add({block,block+1});group_demoted[(block<<block_shift)>>USE_GROUP_BITS]=1;
    }
    void Reset(bool memo) {
        if(memo)residency_use_memo.Invalidate();
        group_use.assign(512,0);group_demoted.assign(512,0);
        idle_demoted.Clear();promote_candidates.Clear();
    }
};
struct Legacy:State {void NoteUse(VAddr,u64);};
struct Current:State {void NoteUse(VAddr,u64);};
static auto Pairs(const IntervalList<>& list) {
    std::vector<std::pair<u64,u64>> result;
    for(const auto& range:list)result.emplace_back(range.start,range.end);
    return result;
}
static void Same(const State& a,const State& b) {
    assert(a.group_use==b.group_use);assert(a.group_demoted==b.group_demoted);
    assert(Pairs(a.idle_demoted)==Pairs(b.idle_demoted));
    assert(Pairs(a.promote_candidates)==Pairs(b.promote_candidates));
}
'''
MAIN = r'''
int main() {
    Legacy old;Current current;std::mt19937 random(640064);
    u64 calls=0,demotions=0;
    for(unsigned step=0;step<30000;++step) {
        const unsigned op=random()%12;
        if(op<=5) {
            const u64 first=random()%520;const u64 count=random()%500;
            const VAddr address=(first<<21)+(random()%1024);
            const u64 bytes=count<<21;
            old.NoteUse(address,bytes);current.NoteUse(address,bytes);++calls;Same(old,current);
            // Exercise memo hits and overlapping requests within the same second.
            for(unsigned n=0;n<3;++n){old.NoteUse(address,bytes);current.NoteUse(address,bytes);++calls;Same(old,current);}
        } else if(op<=7) {
            const u64 block=random()%(512*128);
            old.Demote(block,false);current.Demote(block,true);++demotions;
        } else if(op==8) {
            // Includes backwards time, repeated seconds and 32-bit wrap boundaries.
            const u32 times[]={0,1,2,3,100,99,0xfffffffeu,0xffffffffu};
            BbStats::coarse_second.store(times[random()%8],std::memory_order_relaxed);
        } else if(op==9) {
            old.promote_candidates.Clear();current.promote_candidates.Clear();
        } else if(op==10) {
            const u64 block=random()%(512*128);
            old.idle_demoted.Subtract(block,block+7);current.idle_demoted.Subtract(block,block+7);
        } else {
            old.Reset(false);current.Reset(true);
        }
        Same(old,current);
    }
    std::printf("NoteUse differential: 30000 operations, %llu calls and %llu demotions; timestamps, flags, idle and promotion sets identical\n",(unsigned long long)calls,(unsigned long long)demotions);
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[2]
    gpu=repo/'engines/metal/gpu'
    source=(gpu/'shadps4/video_core/buffer_cache/buffer_cache.cpp').read_text()
    start=source.index('void BufferCache::NoteUse(')
    end=source.index('\nvoid BufferCache::ReleaseResidencySlot',start)
    actual=source[start:end].replace('void BufferCache::NoteUse','void Current::NoteUse',1)
    compiler=shutil.which('clang++') or shutil.which('c++')
    if not compiler:raise SystemExit('C++ compiler required')
    def verify(out):
        out.mkdir(parents=True,exist_ok=True)
        cpp=out/'note_use_equivalence.cpp';exe=out/'note_use_equivalence'
        cpp.write_text(PREFIX+LEGACY+actual+MAIN)
        subprocess.run([compiler,'-std=c++23','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
                        '-isystem',str(gpu/'shim'),'-isystem',str(gpu/'shadps4'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run(['codesign','--force','--sign','-',str(exe)],check=True)
        subprocess.run(['codesign','--verify','--strict',str(exe)],check=True)
        result=subprocess.run([str(exe)],env=os.environ|{'BB_LAYER_USE_CACHE':'1'},capture_output=True,text=True,check=True)
        (out/'result.txt').write_text(result.stdout);print(result.stdout,end='')
    if args.output:verify(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='bb-note-use-equivalence-') as out:verify(Path(out))

if __name__=='__main__':main()
