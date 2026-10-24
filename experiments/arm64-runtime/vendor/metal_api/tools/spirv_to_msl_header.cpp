// SPDX-License-Identifier: GPL-2.0-or-later

// Build-time tool of the Metal backend (docs/METAL_BACKEND_PLAN.md D11): SPIR-V (glslang's output
// for a host shader) -> MSL (SPIRV-Cross) -> a C++ header with the MSL text and the resource
// layout (renderer_metal/msl_host_shader.h).
//
//   spirv_to_msl_header <in.spv> <out.h> <symbol> <vert|frag|comp>
//
// The conversion mirrors renderer_metal/mtl_shader.mm (same MSL options, same direct slots: push
// constants at buffer 0, buffers from 1, textures and samplers from 0, in (set, binding) order),
// so a host kernel binds like a game shader. Keep both in step.

#include <spirv_cross/spirv_msl.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "video_core/renderer_metal/msl_host_shader.h"

namespace sc = spirv_cross;
using Kind = Metal::HostShaders::Binding::Kind;
using Stage = Metal::HostShaders::Stage;
using SpecType = Metal::HostShaders::SpecConstant::Type;

namespace {

// The direct slots of mtl_shader.h.
constexpr uint32_t kPushConstantBuffer = 0;
constexpr uint32_t kFirstResourceBuffer = 1;
constexpr uint32_t kMaxBuffers = 31;
constexpr uint32_t kMaxTextures = 128;
constexpr uint32_t kMaxSamplers = 16;
constexpr size_t kChunkBytes = 8000; // string literals stay far below any compiler limit

struct Binding {
    uint32_t set, binding;
    Kind kind;
    uint32_t msl_index, count;
};

struct Spec {
    uint32_t id;
    SpecType type;
    uint32_t default_value;
};

struct Converted {
    std::string msl;
    std::string entry;
    Stage stage{};
    std::vector<Binding> bindings;
    std::vector<Spec> specs;
    uint32_t local_size[3]{};
    uint32_t push_constant_size{};
    uint32_t buffer_size_buffer{~0u};
    uint32_t num_buffers{}, num_textures{}, num_samplers{};
};

const char* KindName(Kind kind) {
    switch (kind) {
    case Kind::Buffer:
        return "Buffer";
    case Kind::Texture:
        return "Texture";
    case Kind::StorageTexture:
        return "StorageTexture";
    case Kind::Sampler:
        return "Sampler";
    case Kind::TexelBuffer:
        return "TexelBuffer";
    case Kind::PushConstants:
        return "PushConstants";
    }
    return "?";
}

const char* SpecTypeName(SpecType type) {
    switch (type) {
    case SpecType::Bool:
        return "Bool";
    case SpecType::U32:
        return "U32";
    case SpecType::I32:
        return "I32";
    case SpecType::F32:
        return "F32";
    }
    return "?";
}

Converted Convert(const std::vector<uint32_t>& spirv) {
    Converted out;
    sc::CompilerMSL comp(spirv.data(), spirv.size());
    const auto entry_points = comp.get_entry_points_and_stages();
    if (entry_points.empty()) {
        throw std::runtime_error("no entry point");
    }
    const auto model = entry_points[0].execution_model;
    switch (model) {
    case spv::ExecutionModelVertex:
        out.stage = Stage::Vertex;
        break;
    case spv::ExecutionModelFragment:
        out.stage = Stage::Fragment;
        break;
    case spv::ExecutionModelGLCompute:
        out.stage = Stage::Compute;
        break;
    default:
        throw std::runtime_error("unsupported execution model");
    }

    const sc::ShaderResources res = comp.get_shader_resources();
    if (!res.push_constant_buffers.empty()) {
        const sc::Resource& pc = res.push_constant_buffers[0];
        out.push_constant_size = static_cast<uint32_t>(
            comp.get_declared_struct_size(comp.get_type(pc.base_type_id)));
    }

    // One slot per (set, binding) per Metal resource class, as in mtl_shader.mm Convert().
    struct Slot {
        sc::SPIRType::BaseType basetype{};
        Kind kind{};
        bool buffer{}, texture{}, sampler{};
        uint32_t count{1};
    };
    std::map<std::pair<uint32_t, uint32_t>, Slot> slots;
    auto array_count = [&](const sc::Resource& r) -> uint32_t {
        const sc::SPIRType& type = comp.get_type(r.type_id);
        if (type.array.empty()) {
            return 1;
        }
        if (type.array.size() != 1 || !type.array_size_literal[0] || type.array[0] == 0) {
            throw std::runtime_error("unsupported resource array");
        }
        return type.array[0];
    };
    auto collect = [&](const sc::SmallVector<sc::Resource>& list, sc::SPIRType::BaseType basetype,
                       Kind kind, bool buffer, bool texture, bool sampler) {
        for (const sc::Resource& r : list) {
            const uint32_t set = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
            const uint32_t binding = comp.get_decoration(r.id, spv::DecorationBinding);
            if (slots.contains({set, binding})) {
                continue;
            }
            Kind slot_kind = kind;
            if (kind == Kind::Texture || kind == Kind::StorageTexture) {
                if (comp.get_type(r.type_id).image.dim == spv::DimBuffer) {
                    slot_kind = Kind::TexelBuffer;
                }
            }
            slots[{set, binding}] = {.basetype = basetype,
                                     .kind = slot_kind,
                                     .buffer = buffer,
                                     .texture = texture,
                                     .sampler = sampler,
                                     .count = array_count(r)};
        }
    };
    collect(res.uniform_buffers, sc::SPIRType::Struct, Kind::Buffer, true, false, false);
    collect(res.storage_buffers, sc::SPIRType::Struct, Kind::Buffer, true, false, false);
    collect(res.storage_images, sc::SPIRType::Image, Kind::StorageTexture, false, true, false);
    collect(res.sampled_images, sc::SPIRType::SampledImage, Kind::Texture, false, true, true);
    collect(res.separate_images, sc::SPIRType::Image, Kind::Texture, false, true, false);
    collect(res.separate_samplers, sc::SPIRType::Sampler, Kind::Sampler, false, false, true);
    collect(res.subpass_inputs, sc::SPIRType::Image, Kind::Texture, false, true, false);

    uint32_t next_buffer = kFirstResourceBuffer, next_texture = 0, next_sampler = 0;
    out.bindings.push_back({~0u, ~0u, Kind::PushConstants, kPushConstantBuffer, 1});
    for (auto& [key, slot] : slots) { // std::map: sorted by (set, binding)
        const auto [set, binding] = key;
        sc::MSLResourceBinding b;
        b.stage = model;
        b.basetype = slot.basetype;
        b.desc_set = set;
        b.binding = binding;
        b.count = slot.count;
        if (slot.buffer) {
            b.msl_buffer = next_buffer;
            out.bindings.push_back({set, binding, Kind::Buffer, next_buffer, slot.count});
            next_buffer += slot.count;
        }
        if (slot.texture) {
            b.msl_texture = next_texture;
            out.bindings.push_back({set, binding, slot.kind, next_texture, slot.count});
            next_texture += slot.count;
        }
        if (slot.sampler) {
            b.msl_sampler = next_sampler;
            out.bindings.push_back({set, binding, Kind::Sampler, next_sampler, slot.count});
            next_sampler += slot.count;
        }
        comp.add_msl_resource_binding(b);
    }
    {
        sc::MSLResourceBinding b;
        b.stage = model;
        b.basetype = sc::SPIRType::Struct;
        b.desc_set = sc::kPushConstDescSet;
        b.binding = sc::kPushConstBinding;
        b.msl_buffer = kPushConstantBuffer;
        comp.add_msl_resource_binding(b);
    }

    const uint32_t size_buffer_slot = next_buffer;
    sc::CompilerMSL::Options options;
    options.platform = sc::CompilerMSL::Options::macOS;
    options.set_msl_version(3, 1);
    options.argument_buffers = false;
    options.buffer_size_buffer_index = size_buffer_slot;
    comp.set_msl_options(options);

    out.msl = comp.compile();

    out.entry = comp.get_cleansed_entry_point_name(entry_points[0].name, model);
    out.num_buffers = next_buffer;
    out.num_textures = next_texture;
    out.num_samplers = next_sampler;
    if (comp.needs_buffer_size_buffer()) {
        out.buffer_size_buffer = size_buffer_slot;
        ++out.num_buffers;
    }
    if (out.num_buffers > kMaxBuffers || out.num_textures > kMaxTextures ||
        out.num_samplers > kMaxSamplers) {
        throw std::runtime_error("resource limit exceeded");
    }
    std::stable_sort(out.bindings.begin() + 1, out.bindings.end(),
                     [](const Binding& a, const Binding& b) {
                         return std::tie(a.set, a.binding, a.kind) <
                                std::tie(b.set, b.binding, b.kind);
                     });

    if (out.stage == Stage::Compute) {
        sc::SpecializationConstant wg_x, wg_y, wg_z;
        comp.get_work_group_size_specialization_constants(wg_x, wg_y, wg_z);
        if (wg_x.id != 0 || wg_y.id != 0 || wg_z.id != 0) {
            throw std::runtime_error("workgroup size from a specialization constant");
        }
        for (uint32_t i = 0; i < 3; ++i) {
            out.local_size[i] = comp.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
        }
    }

    for (const sc::SpecializationConstant& s : comp.get_specialization_constants()) {
        const sc::SPIRConstant& c = comp.get_constant(s.id);
        const sc::SPIRType& type = comp.get_type(c.constant_type);
        Spec spec{s.constant_id, SpecType::U32, c.scalar()};
        switch (type.basetype) {
        case sc::SPIRType::Boolean:
            spec.type = SpecType::Bool;
            break;
        case sc::SPIRType::UInt:
            spec.type = SpecType::U32;
            break;
        case sc::SPIRType::Int:
            spec.type = SpecType::I32;
            break;
        case sc::SPIRType::Float:
            spec.type = SpecType::F32;
            break;
        default:
            throw std::runtime_error("specialization constant of an unsupported type");
        }
        out.specs.push_back(spec);
    }
    std::sort(out.specs.begin(), out.specs.end(),
              [](const Spec& a, const Spec& b) { return a.id < b.id; });
    return out;
}

std::string ToUpper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

/// The MSL as adjacent raw string literals, cut at line ends.
void WriteMsl(std::ostream& o, const std::string& msl) {
    constexpr const char* kOpen = "R\"BBMSL(";
    constexpr const char* kClose = ")BBMSL\"";
    if (msl.find(kClose) != std::string::npos) {
        throw std::runtime_error("MSL contains the raw string delimiter");
    }
    size_t pos = 0;
    do {
        size_t end = std::min(msl.size(), pos + kChunkBytes);
        if (end < msl.size()) {
            const size_t nl = msl.find('\n', end);
            end = nl == std::string::npos ? msl.size() : nl + 1;
        }
        o << "    " << kOpen << msl.substr(pos, end - pos) << kClose << '\n';
        pos = end;
    } while (pos < msl.size());
}

void WriteHeader(std::ostream& o, const Converted& c, const std::string& symbol,
                 const std::string& in_name, const char* stage_name) {
    char hex[16];
    o << "// SPDX-License-Identifier: GPL-2.0-or-later\n"
         "// Generated by gpu/tools/spirv_to_msl_header from "
      << in_name << " (" << stage_name << "); do not edit.\n"
      << "#pragma once\n#include \"video_core/renderer_metal/msl_host_shader.h\"\n\n"
      << "namespace Metal::HostShaders {\n\n"
      << "inline constexpr std::string_view " << symbol << "_MSL =\n";
    WriteMsl(o, c.msl);
    o << "    ;\n\n";
    o << "inline constexpr Binding " << symbol << "_BINDINGS[] = {\n";
    for (const Binding& b : c.bindings) {
        o << "    {" << (b.set == ~0u ? "~0u" : std::to_string(b.set)) << ", "
          << (b.binding == ~0u ? "~0u" : std::to_string(b.binding)) << ", Binding::Kind::"
          << KindName(b.kind) << ", " << b.msl_index << ", " << b.count << "},\n";
    }
    o << "};\n\n";
    if (!c.specs.empty()) {
        o << "inline constexpr SpecConstant " << symbol << "_SPEC_CONSTANTS[] = {\n";
        for (const Spec& s : c.specs) {
            std::snprintf(hex, sizeof(hex), "%#x", s.default_value);
            o << "    {" << s.id << ", SpecConstant::Type::" << SpecTypeName(s.type) << ", "
              << hex << "},\n";
        }
        o << "};\n\n";
    }
    o << "inline constexpr Shader " << symbol << " = {\n"
      << "    .stage = Stage::"
      << (c.stage == Stage::Vertex ? "Vertex" : c.stage == Stage::Fragment ? "Fragment" : "Compute")
      << ",\n    .msl = " << symbol << "_MSL,\n    .entry = \"" << c.entry << "\",\n"
      << "    .bindings = " << symbol << "_BINDINGS,\n";
    if (c.specs.empty()) {
        o << "    .spec_constants = {},\n";
    } else {
        o << "    .spec_constants = " << symbol << "_SPEC_CONSTANTS,\n";
    }
    o << "    .local_size = {" << c.local_size[0] << ", " << c.local_size[1] << ", "
      << c.local_size[2] << "},\n"
      << "    .push_constant_size = " << c.push_constant_size << ",\n"
      << "    .buffer_size_buffer = "
      << (c.buffer_size_buffer == ~0u ? "~0u" : std::to_string(c.buffer_size_buffer)) << ",\n"
      << "    .num_buffers = " << c.num_buffers << ",\n"
      << "    .num_textures = " << c.num_textures << ",\n"
      << "    .num_samplers = " << c.num_samplers << ",\n};\n\n"
      << "} // namespace Metal::HostShaders\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s <in.spv> <out.h> <symbol> <vert|frag|comp>\n", argv[0]);
        return 2;
    }
    const std::string in_path = argv[1], out_path = argv[2], symbol = ToUpper(argv[3]),
                      stage_arg = argv[4];
    const Stage expected = stage_arg == "vert"   ? Stage::Vertex
                           : stage_arg == "frag" ? Stage::Fragment
                                                 : Stage::Compute;
    if (stage_arg != "vert" && stage_arg != "frag" && stage_arg != "comp") {
        std::fprintf(stderr, "unknown stage '%s'\n", stage_arg.c_str());
        return 2;
    }
    try {
        std::ifstream file(in_path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("cannot read " + in_path);
        }
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        if (bytes.size() % 4 != 0 || bytes.empty()) {
            throw std::runtime_error("not a SPIR-V binary");
        }
        std::vector<uint32_t> words(bytes.size() / 4);
        std::memcpy(words.data(), bytes.data(), bytes.size());

        const Converted converted = Convert(words);
        if (converted.stage != expected) {
            throw std::runtime_error("the module's stage is not " + stage_arg);
        }
        std::ostringstream text;
        WriteHeader(text, converted, symbol, in_path.substr(in_path.find_last_of('/') + 1),
                    stage_arg.c_str());
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        out << text.str();
        if (!out) {
            throw std::runtime_error("cannot write " + out_path);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s: %s\n", argv[0], in_path.c_str(), e.what());
        return 1;
    }
    return 0;
}
