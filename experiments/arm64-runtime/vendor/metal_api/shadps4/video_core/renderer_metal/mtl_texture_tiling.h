// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>
namespace Metal {
// Reuse the proven PS4 address calculation. Only substitute its two linear texel reads;
// dead detiler writes are removed because this pipeline specializes IS_TILER=true.
inline std::string TextureTilingSource(std::string_view original, bool half = false) {
    std::string out(original);
    auto replace=[&](const std::string& from,const std::string& to,unsigned expected) {
        unsigned count=0;size_t pos=0;
        while((pos=out.find(from,pos))!=std::string::npos) {out.replace(pos,from.size(),to);pos+=to.size();++count;}
        return count==expected;
    };
    const std::string texture=half?"texture2d<half>":"texture2d<float>";
    if(!replace("device OutputBuf& _794 [[buffer(2)]]", texture+" _794 [[texture(0)]]",1) ||
       !replace("device OutputBuf& _794", texture+" _794",3) ||
       !replace("_794.linear_data[linear_index + i_1] = texels[i_1];", "/* unused detiler write */",1) ||
       !replace(half?"_794.linear_data[linear_index] = _788.tiled_data[tiled_offset / 8u];":"_794.linear_data[linear_index] = _788.tiled_data[tiled_offset / 4u];", "/* unused detiler write */",1) ||
       !replace("_794.linear_data[linear_index + i]", "TextureWord(_794, linear_index + i)",1) ||
       !replace("_794.linear_data[linear_index]", "TextureWord(_794, linear_index)",1)) return {};
    if(out.find("_794.linear_data")!=std::string::npos) return {};
    // The existing kernel visits padded rows as well. They contain no image data;
    // leave their guest bytes unchanged instead of reading outside the native texture.
    for(const auto name:{"void CopyWholeRow(","void Copy("}) {
        auto at=out.find(name);if(at==std::string::npos)return {};
        at=out.find("{",at);if(at==std::string::npos)return {};
        out.insert(at+1,"\n    if(linear_index >= _794.get_width()*_794.get_height()*TEXTURE_TILING_SCALE*TEXTURE_TILING_SCALE) return;\n");
    }
    const auto at=out.find("using namespace metal;");if(at==std::string::npos)return {};
    const auto helper=half?R"(
constant uint TEXTURE_TILING_SCALE [[function_constant(12)]];
// No floating point arithmetic or precision reduction: preserve all four half bit patterns.
inline uint2 TextureWord(texture2d<half> image, uint index) {
    uint w=image.get_width()*TEXTURE_TILING_SCALE;
    ushort4 words=as_type<ushort4>(image.read(uint2(index%w,index/w)/TEXTURE_TILING_SCALE));
    if(TEXTURE_TILING_SCALE==2u) {
        // The old proxy resolve writes through an RGBA16F render target. On the
        // validated Apple path that canonicalizes every NaN to positive quiet NaN.
        // Native texture exports must instead preserve their original NaN payload.
        bool4 nan=((words & ushort4(0x7c00u))==ushort4(0x7c00u)) &
                  ((words & ushort4(0x03ffu))!=ushort4(0u));
        words=select(words,ushort4(0x7e00u),nan);
    }
    return as_type<uint2>(words);
}
)":R"(
constant uint TEXTURE_TILING_SCALE [[function_constant(12)]];
// All 256 UNORM8 values round-trip exactly through Metal's float texture read.
inline uint TextureWord(texture2d<float> image, uint index) {
    uint w=image.get_width()*TEXTURE_TILING_SCALE;
    uint4 c=uint4(round(image.read(uint2(index%w,index/w)/TEXTURE_TILING_SCALE)*255.0f));
    return c.x | (c.y<<8u) | (c.z<<16u) | (c.w<<24u);
}
)";
    out.insert(at+std::string("using namespace metal;").size(),helper);
    return out;
}
} // namespace Metal
