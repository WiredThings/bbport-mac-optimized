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

## Gameplay measurement, M4 Mac mini (2026-10-17)

Two separate runs of the same 0.2.15 renderer (175a865), Hunter's Dream at the Yharnam
headstone, 960x540 MetalFX input to 1920x1080 output. Existing settings/effects unchanged.
Both runs enabled the same stage sampling (every 8 command buffers), frame statistics and
CPU sections. Each recording lasted 60 seconds; initial intervals were omitted. This is one
paired comparison, not a randomized multi-run benchmark or an all-scene claim.

| Metric | Full usage | Lean colour proxies |
|---|---:|---:|
| Game FPS, stable reports | 45.94 | 49.60 |
| Guest fragment-stage span, sampled ms/frame | 7.09 | 5.48 |
| Guest vertex-stage span, sampled ms/frame | 4.41 | 4.12 |
| CPU recording time, last 35 seconds, ms/frame | 9.54 | 9.18 |
| Present/tick waits, ms/frame | 0 / 0 | 0 / 0 |
| Process footprint, average GiB | 4.34 | 4.15 |
| Scene proxy objects | 18 | 18 |
| Scene proxy allocated MiB | 43.80 | 44.27 |

FPS rose about 8%; guest fragment-stage spans fell about 23%. Player/camera-position
coordinates matched, draw counts differed by 0.22%, and neither steady interval compiled
new pipelines. Stage spans include memory, dependencies and tile work; these are not isolated
ALU timings. Bandwidth/cache-miss percentages were not measured. The earlier partial-vertex
A/B also left other vertex passes optimized, so it was not a whole-version comparison.

The total footprint difference is observational: staging pools and retained native render
targets differed across boots. Do not attribute the entire ~0.19 GiB reduction to compression.
The 18 proxies gained only ~0.47 MiB of allocation metadata and created no duplicate backing
cache. In-game format promotions were zero. System memory pressure temporarily reached the
warning level during the optimized run and returned to normal at its end; this does not prove
the system's overall memory pressure is solved.

Velocity merge, both blur bypass copies, occlusion, two-level BDA and direct proxy clears
remained active. No new GPU faults, invalid/capacity-dropped timing samples or additional
null-texture drops were observed (both boots reported the same three startup null-texture
drops). The 37 native regression groups and 12 packaging tests passed. Temporary diagnostics
were removed and original launcher preferences restored; lean proxies remain the default.
