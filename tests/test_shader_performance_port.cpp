// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the real SPIR-V emitter and specialization keys without a game or GPU device.
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/specialization.h"

int main(int argc, char** argv) {
    using namespace Shader;
    const std::filesystem::path output = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(output);
    for (const auto stage : {SwStage::Vertex, SwStage::Fragment, SwStage::Compute}) {
        for (const bool precise : {false, true}) {
            Info info{};
            info.sw_stage = stage;
            info.hw_stage = stage == SwStage::Vertex ? HwStage::Vertex :
                            stage == SwStage::Fragment ? HwStage::Fragment : HwStage::Compute;
            RuntimeInfo runtime{};
            runtime.Initialize(info.hw_stage, stage);
            if (stage == SwStage::Compute) runtime.hw.cs.workgroup_size = {1, 1, 1};
            if (stage == SwStage::Fragment)
                runtime.hw.fs.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;
            Common::ObjectPool<IR::Inst> pool;
            IR::Block block(pool);
            IR::IREmitter ir(block);
            const auto sum = ir.FPAdd(ir.Imm32(1.25f), ir.Imm32(2.5f));
            if (stage != SwStage::Compute) {
                const auto target = stage == SwStage::Vertex ? IR::Attribute::Position0 :
                                                              IR::Attribute::RenderTarget0;
                for (u32 component = 0; component < 4; ++component) {
                    info.stores.Set(target, component);
                    ir.SetAttribute(target, IR::F32{sum}, component);
                }
            }
            ir.Epilogue();
            IR::Program program(info);
            program.blocks.push_back(&block);
            program.syntax_list.push_back({.data = {.block = &block},
                                           .type = IR::AbstractSyntaxNode::Type::Block});
            program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
            Profile profile{};
            profile.supported_spirv = 0x00010600;
            profile.precise_fragment_compute = precise;
            Backend::Bindings bindings{};
            const auto code = Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
            unsigned no_contraction = 0, additions = 0;
            for (size_t i = 5; i < code.size();) {
                const auto size = code[i] >> 16, op = code[i] & 0xffff;
                assert(size && i + size <= code.size());
                if (op == 71 && size >= 3 && code[i + 2] == 42) ++no_contraction;
                if (op == 129) ++additions; // OpFAdd
                i += size;
            }
            assert(additions == 1);
            assert(no_contraction == unsigned(precise || stage == SwStage::Vertex));
            const auto name = std::to_string(unsigned(stage)) + (precise ? "-precise.spv" : "-fast.spv");
            std::ofstream file(output / name, std::ios::binary);
            file.write(reinterpret_cast<const char*>(code.data()), code.size() * sizeof(u32));
            assert(file.good());
        }
    }
    Profile strict{}, fast{};
    fast.precise_fragment_compute = false;
    assert(strict != fast); // warm-up must invalidate shaders when switching precision policy

    Info info{};
    StageSpecialization a{}, b{};
    a.info = b.info = &info;
    b.start.unified = 4;
    assert(a == b); // truly resource-free vertex shaders may share a module
    a.buffers.emplace_back();
    b.buffers.emplace_back();
    assert(!(a == b)); // null resource sharps still reserve numbered SPIR-V bindings
    b.start = a.start;
    assert(a == b);
    std::puts("Performance port: PASS (6 real SPIR-V modules, vertex precision, cache policy, null-resource layouts)");
}
