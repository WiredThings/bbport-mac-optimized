// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_metal/mtl_index_expand.h"

namespace Metal::IndexExpand {

namespace {

/// The sequence first, first+1, ... as an index source.
struct Sequence {
    u32 base;
    u32 operator[](u32 i) const {
        return base + i;
    }
};

/// A (sub)array of u8, u16 or u32 indices.
template <typename T>
struct Array {
    const T* data;
    u32 operator[](u32 i) const {
        return data[i];
    }
};

/// Expands one run of `n` indices without restart; returns the number of indices written.
template <typename Src>
u32 ExpandRun(Topology t, Src s, u32 n, u32* dst) {
    u32* const begin = dst;
    switch (t) {
    case Topology::QuadList:
        for (u32 i = 0; i + 4 <= n; i += 4) {
            const u32 a = s[i], b = s[i + 1], c = s[i + 2], d = s[i + 3];
            dst[0] = a;
            dst[1] = b;
            dst[2] = c;
            dst[3] = a;
            dst[4] = c;
            dst[5] = d;
            dst += 6;
        }
        break;
    case Topology::TriangleFan:
    case Topology::Polygon:
        if (n >= 3) {
            const u32 v0 = s[0];
            u32 prev = s[1];
            for (u32 i = 2; i < n; i++) {
                const u32 cur = s[i];
                dst[0] = v0;
                dst[1] = prev;
                dst[2] = cur;
                dst += 3;
                prev = cur;
            }
        }
        break;
    case Topology::LineLoop:
        if (n >= 2) {
            u32 prev = s[0];
            for (u32 i = 1; i < n; i++) {
                const u32 cur = s[i];
                dst[0] = prev;
                dst[1] = cur;
                dst += 2;
                prev = cur;
            }
            dst[0] = prev;
            dst[1] = s[0];
            dst += 2;
        }
        break;
    case Topology::LineListAdjacency:
        for (u32 i = 0; i + 4 <= n; i += 4) {
            dst[0] = s[i + 1];
            dst[1] = s[i + 2];
            dst += 2;
        }
        break;
    case Topology::LineStripAdjacency:
        if (n >= 4) {
            for (u32 i = 1; i + 1 < n; i++) {
                *dst++ = s[i];
            }
        }
        break;
    case Topology::TriangleListAdjacency:
        for (u32 i = 0; i + 6 <= n; i += 6) {
            dst[0] = s[i];
            dst[1] = s[i + 2];
            dst[2] = s[i + 4];
            dst += 3;
        }
        break;
    case Topology::TriangleStripAdjacency:
        // n/2 main vertices (the vertices 0, 2, 4, ...); an odd trailing vertex is dropped.
        if (n >= 6) {
            for (u32 i = 0; i + 1 < n; i += 2) {
                *dst++ = s[i];
            }
        }
        break;
    }
    return static_cast<u32>(dst - begin);
}

bool IsStripOutput(Topology t) {
    const OutputPrimitive out = OutputOf(t);
    return out == OutputPrimitive::LineStrip || out == OutputPrimitive::TriangleStrip;
}

/// Splits `src` at restart indices and expands every run.
template <typename T>
u32 ExpandRestart(Topology t, const T* src, u32 count, u32* dst) {
    constexpr T Restart = static_cast<T>(~T{0});
    const bool strips = IsStripOutput(t);
    u32 written = 0;
    u32 i = 0;
    while (i < count) {
        u32 end = i;
        while (end < count && src[end] != Restart) {
            end++;
        }
        if (end > i) {
            // Strip outputs separate the runs with a restart; runs without output add none.
            const u32 sep = (strips && written != 0) ? 1 : 0;
            const u32 n = ExpandRun(t, Array<T>{src + i}, end - i, dst + written + sep);
            if (n != 0) {
                if (sep) {
                    dst[written] = OutputRestart;
                }
                written += sep + n;
            }
        }
        i = end + 1;
    }
    return written;
}

template <typename T>
u32 ExpandArray(Topology t, const T* src, u32 count, bool restart, u32* dst) {
    if (restart) {
        return ExpandRestart(t, src, count, dst);
    }
    return ExpandRun(t, Array<T>{src}, count, dst);
}

} // namespace

std::optional<Topology> FromGuest(AmdGpu::PrimitiveType type) {
    using AmdGpu::PrimitiveType;
    switch (type) {
    case PrimitiveType::QuadList:
        return Topology::QuadList;
    case PrimitiveType::TriangleFan:
        return Topology::TriangleFan;
    case PrimitiveType::Polygon:
        return Topology::Polygon;
    case PrimitiveType::LineLoop:
        return Topology::LineLoop;
    case PrimitiveType::AdjLineList:
        return Topology::LineListAdjacency;
    case PrimitiveType::AdjLineStrip:
        return Topology::LineStripAdjacency;
    case PrimitiveType::AdjTriangleList:
        return Topology::TriangleListAdjacency;
    case PrimitiveType::AdjTriangleStrip:
        return Topology::TriangleStripAdjacency;
    default:
        return std::nullopt;
    }
}

OutputPrimitive OutputOf(Topology t) {
    switch (t) {
    case Topology::QuadList:
    case Topology::TriangleFan:
    case Topology::Polygon:
    case Topology::TriangleListAdjacency:
        return OutputPrimitive::Triangle;
    case Topology::LineLoop:
    case Topology::LineListAdjacency:
        return OutputPrimitive::Line;
    case Topology::LineStripAdjacency:
        return OutputPrimitive::LineStrip;
    case Topology::TriangleStripAdjacency:
        return OutputPrimitive::TriangleStrip;
    }
    return OutputPrimitive::Triangle;
}

u32 OutputCount(Topology t, u32 count, bool restart) {
    switch (t) {
    case Topology::QuadList:
        return count / 4 * 6;
    case Topology::TriangleFan:
    case Topology::Polygon:
        return count >= 3 ? (count - 2) * 3 : 0;
    case Topology::LineLoop:
        return count >= 2 ? count * 2 : 0;
    case Topology::LineListAdjacency:
        return count / 4 * 2;
    case Topology::LineStripAdjacency:
        return count >= 4 ? count - 2 : 0;
    case Topology::TriangleListAdjacency:
        return count / 6 * 3;
    case Topology::TriangleStripAdjacency:
        // Strips of at least 6 indices, one output restart between two of them.
        return count >= 6 ? count / 2 + (restart ? count / 6 : 0) : 0;
    }
    return 0;
}

u32 Expand(Topology t, const void* src, u32 src_size, u32 count, u32 first, bool restart,
           u32* dst) {
    if (count == 0) {
        return 0;
    }
    if (!src) {
        return ExpandRun(t, Sequence{first}, count, dst);
    }
    switch (src_size) {
    case 1:
        return ExpandArray(t, static_cast<const u8*>(src), count, restart, dst);
    case 2:
        return ExpandArray(t, static_cast<const u16*>(src), count, restart, dst);
    case 4:
        return ExpandArray(t, static_cast<const u32*>(src), count, restart, dst);
    default:
        return 0;
    }
}

void WidenU8(const u8* src, u32 count, bool restart, u16* dst) {
    if (restart) {
        for (u32 i = 0; i < count; i++) {
            dst[i] = src[i] == 0xff ? u16{0xffff} : u16{src[i]};
        }
    } else {
        for (u32 i = 0; i < count; i++) {
            dst[i] = src[i];
        }
    }
}

} // namespace Metal::IndexExpand
