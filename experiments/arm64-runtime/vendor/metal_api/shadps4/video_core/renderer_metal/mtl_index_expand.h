// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include "common/types.h"
#include "video_core/amdgpu/regs_vertex.h"

/// Index expansion for the guest topologies Metal has no equivalent of (docs/METAL_BACKEND_PLAN.md
/// P4.3d, D12). Plain C++ on index arrays: the rasterizer calls Expand while recording a draw and
/// draws the result with the OutputPrimitive of the topology and 32-bit indices.
namespace Metal::IndexExpand {

enum class Topology : u8 {
    QuadList,              ///< (a b c d) -> (a b c) (a c d)
    TriangleFan,           ///< (0 i i+1)
    Polygon,               ///< same as the fan
    LineLoop,              ///< line list including the closing line
    LineListAdjacency,     ///< (a b c d) -> (b c)
    LineStripAdjacency,    ///< strip of the vertices 1 .. n-2
    TriangleListAdjacency, ///< (0 1 2 3 4 5) -> (0 2 4)
    TriangleStripAdjacency ///< strip of the even vertices
};

enum class OutputPrimitive : u8 { Triangle, Line, LineStrip, TriangleStrip };

/// Index written between two strips of a strip output (the restart value of 32-bit indices).
inline constexpr u32 OutputRestart = 0xffffffffu;

/// Which Topology a guest AmdGpu::PrimitiveType needs (nullopt: native or not handled here;
/// RectList is handled elsewhere).
std::optional<Topology> FromGuest(AmdGpu::PrimitiveType type);

OutputPrimitive OutputOf(Topology t);

/// Number of output indices for `count` input vertices/indices. Exact without restart; with
/// `restart` an upper bound of what Expand writes (the output restarts of strips included).
u32 OutputCount(Topology t, u32 count, bool restart = false);

/// Expands `count` input indices (`src`: u8, u16 or u32 elements, `src_size` bytes each; null
/// src = the sequence first, first+1, ...; `first` is ignored with a src) into 32-bit indices at
/// `dst` (OutputCount(t, count, restart) entries at most). Primitive restart (`restart`: the
/// all-ones value of the source width) starts a new primitive and is not emitted where the output
/// has no strips (strip outputs get OutputRestart between strips); returns the number written.
/// Incomplete trailing primitives are dropped.
u32 Expand(Topology t, const void* src, u32 src_size, u32 count, u32 first, bool restart,
           u32* dst);

/// u8 -> u16 widening with restart (0xff -> 0xffff when `restart`).
void WidenU8(const u8* src, u32 count, bool restart, u16* dst);

} // namespace Metal::IndexExpand
