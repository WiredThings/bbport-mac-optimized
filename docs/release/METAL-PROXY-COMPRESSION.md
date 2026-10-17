# Lossless-compression eligibility for live scene proxies (0.2.15)

The texture cache already creates lean Metal images and promotes their usage on first need
(upstream P10.1). The live-resolution path's scene proxies still unconditionally requested
PixelFormatView, including their colour targets, even when every view had the base format.
This prevented the lean policy from covering the G-buffer/post-process proxies.

Apple documents that shaderWrite and pixelFormatView usage can prevent lossless texture
compression: [Optimizing texture data](https://developer.apple.com/documentation/metal/optimizing-texture-data).
Patch 0016 applies the existing lean-usage policy to colour proxies. Depth/stencil retains
its original usages. Same-format swizzles and linear/sRGB twins need no PixelFormatView.
No shader math, sampling filter, geometry, resolution, effect, cache capacity or shader-cache
contents change. Compression remains lossless; the flags permit it rather than prove the
hardware compressed every texture.

A real compatible format reinterpretation first ends pending rendering and velocity tails,
creates the same-format backing with PixelFormatView, and performs one ordered raw blit.
Old views/backing retire after the GPU tick; they are not a second persistent cache.
Descriptor-memo generation changes invalidate old proxy views; a set resolved across a
promotion is not memoized. Subsequent format views reuse the promoted backing. There is
no periodic rebuilding, duplicated working set or reduced resource budget. A rare first-use
promotion temporarily overlaps one old/new allocation until the GPU finishes its copy.

`BB_MTL_PROXY_LEAN=0` restores full-usage proxies. `BB_MTL_FULL_USAGE=1` also preserves
that fallback. Policy is fixed for the process, so gameplay comparisons require separate
runs with the same scene/settings. No diagnostic reference shader libraries or PSOs are made.

Validation includes RGBA8 and RGBA16F integer reinterpretation after pending GPU clears,
bit-identical copied contents, sRGB views without promotion, stable views after later writes,
native resolves, generation invalidation and weak-reference proof of actual old-backing
destruction. Existing proxy clear/copy, velocity, blur, occlusion, two-level page-table and
resource-lifetime tests also run under the Metal validation layer and full-usage fallbacks.

The existing upstream metal_compress_probe on this M4 mini (2048-square, 16 repetitions)
showed RGBA16F tiled write/read median 0.7157 ms with PixelFormatView vs 0.2625 ms lean;
RGBA8 0.3425 vs 0.2589 ms. These are synthetic traffic probes, not gameplay FPS claims.
Actual game bandwidth/ALU percentages remain unmeasured. Public MTLCounterSet exposes
only timestamps here; an Xcode system trace provides channel activity but its captured
GPU interval in this run is short and does not establish sustained occupancy. Gameplay
results must be recorded separately from these probes.
