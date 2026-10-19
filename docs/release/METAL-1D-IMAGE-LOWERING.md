# Metal 1D image lowering (0.2.43)

Adapter 0040 ports the 1D-lowering change from [shadPS4 #5133](https://github.com/shadps4-emu/shadPS4/pull/5133), commit [`ef144307c5a265b19248a87735090c4c3cdc7102`](https://github.com/shadps4-emu/shadPS4/commit/ef144307c5a265b19248a87735090c4c3cdc7102), onto the pinned Metal engine. The PR's unrelated Cube representation refactor is not included. Existing Cube handling and specialization layouts remain unchanged.

## Resource and shader contract

Logical 1D and 1D-array images retain their guest identity but use 2D and 2D-array backing. SPIR-V declarations and sampling/read/write/atomic coordinates change together. Integer texel operations use row zero; normalized sampling uses row **0.5**, the center of the sole row. Using row zero for linear border sampling mixed in border color in the native regression, so simply copying the upstream coordinate constant would change pixels on Metal.

Spatial offsets gain a zero Y component; scalar gradients gain zero Y derivatives. Runtime integer offsets are explicitly bitcast to the signed offset type. Dimension queries return the original logical width/layer results. Array LOD queries discard the incoming array layer and supply only spatial coordinates. Tests use the array coordinate vector actually produced by the resource patching pass.

Adapter **0038 is unchanged**: invalid speculative numeric conversions reject preparation; the ordered path remains strict. This release does not adopt the `None` conversion fallback from bbport #166.

The necessary checks from **0039 remain**:

- Exact matching does not treat every 1×1 resource as interchangeable.
- Same-address 1D/2D reuse requires identical extent, byte size, pixel format, mip/layer counts, pitch, tiling, bank swizzle, padding and sample count. The memoized path rechecks the same layout contract.
- Compatible aliases share their existing normalized backing; no dimensional copy/replacement is needed. Different layouts remain separate resources under the existing lifecycle.
- A native 3D backing is not promoted into the new 1D/2D alias group. Invalid direct views still fail before entering Metal; no draw is silently discarded.

Budgets, cache sizes, eviction thresholds, guest allocation tracking, deferred GPU retirement, two-level page tables, velocity, motion-blur bypass and occlusion policy are unchanged. The host runtime, networking library, language integration and game executable are unchanged.

## Cache behavior

Core metadata/SPIR-V archives use an `img2d-v2` namespace, separately for strict/relaxed math and flat/two-level BDA profiles. Old archives remain on disk. The serialization format is unchanged; the namespace prevents loading a shader whose old image declaration conflicts with new backing. MSL files continue to use content hashes, so unchanged modules remain reusable. The first run may populate missing core records and pipelines; interrupted/cold runs are not accepted as steady-state FPS results.

The private test retained all 2,584 files in its old core namespace byte-for-byte. Formal save, quicksave, preferences and both cache locations were also checked independently before promotion.

## Native validation

Eleven native regression groups pass, including actual texture-cache lookup and Metal views, speculative descriptors, runtime, scene targets, two-level BDA, scheduler, motion shaders, pipeline core, cache collection and guest-resource lifetime. The image test emits **28 SPIR-V modules** covering 1D/array explicit and implicit sampling, offsets, gradients, gather, runtime gather offsets, LOD/dimension queries, reads, writes, atomics and depth comparison/gather. All validate for Vulkan 1.3 and compile to MSL.

Actual Metal draws compare lowered 2D sampling against native 1D sampling for both array/non-array textures, with nearest, linear and linear border filtering. Pixel differences are zero in all six comparisons. Cache regressions verify two-layer array views, rejection of native-3D aliasing, changed bank-swizzle descriptors through the memo, and same-address 1×1 versus 38×14 layouts. Thirty-two alternating logical aliases preserve GPU-only byte patterns with unchanged image/backing count. A deliberately incompatible direct view exits 23 before Metal validation. Ten networking Python tests also pass.

```sh
cmake --build out/gpu-metal --target image-1d-lowering-test texture-cache-views-test speculative-descriptor-test --parallel 2
mkdir -p /private/tmp/bb-image-test-user
BB_GPU_USER_DIR=/private/tmp/bb-image-test-user MTL_DEBUG_LAYER=1 out/gpu-metal/image-1d-lowering-test /private/tmp/bb-image-test-output
BB_GPU_USER_DIR=/private/tmp/bb-image-test-user MTL_DEBUG_LAYER=1 out/gpu-metal/texture-cache-views-test
# Optional, with SPIRV-Tools installed:
for shader in /private/tmp/bb-image-test-output/*.spv; do spirv-val --target-env vulkan1.3 "$shader"; done
```

## Isolated gameplay and performance

On 2026-10-19 the owner performed two enemy deaths in `cummmfpk` using an isolated save/config/cache copy. Both returned to the same operable lamp camera in the same process. Loading intervals were 147.23–159.36 s and 309.35–321.57 s. No numeric-format assertion, incompatible-view assertion or Metal error was logged. Footprint settled around 5.37 GiB after the second respawn. No dimensional-guard message appeared in this run; it therefore does not prove that the original crashing resource was encountered. This was offline QA; no new live online death test was performed.

A separate four-run **ABBA** comparison used the unchanged save seed, fixed lamp camera, native 1920×1080, MetalFX off and identical options/mod selections. Each scored 60 seconds after 45 seconds of warmup. All four had zero measured shader compilations and no lock/sleep/critical-pressure event. No stage profiler or capture ran during scored frames. Loading and the one post-respawn diagnostic snapshot are excluded.

| Metric | 0.2.42 A1 | 0.2.43 B1 | 0.2.43 B2 | 0.2.42 A2 |
| --- | ---: | ---: | ---: | ---: |
| Average FPS | 59.79 | 57.85 | 58.30 | 54.15 |
| 1% low FPS | 32.83 | 35.89 | 46.20 | 29.78 |
| Median footprint, MiB | 5,424.5 | 5,276.9 | 5,202.9 | 5,262.5 |
| Owned Metal allocation capacity, MiB | 1,013.2 | 1,173.0 | 1,157.6 | 1,173.4 |
| System swap change during score, MiB | 0 | −40 | −24 | −8 |

The mean FPS is **56.97 → 58.08 (+1.94%)**, but the baseline's own spread is larger than that change: **no FPS gain is established**. Mean per-run median footprint is about 104 MiB lower; no sustained memory-pressure increase was observed. Owned GPU capacity varies with existing staging/render-target pools and startup timing in both versions. Different cache histories/prewarm sets also affect CPU residency. These numbers do not establish a lasting memory saving or a new GPU budget. No pool or cache was enlarged for this fix.

The production app is promoted only after these checks. Isolated progress is never imported into formal saves.
