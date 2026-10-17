# Blur bypass correction and velocity batching (0.2.5-metal-motion2)

The 0.2.4 user-driven gameplay run confirmed velocity draws and MetalFX merges, but logged
`unsupported final blur permutation`. Its two-minute playable-scene sample at 960x540 ->
1920x1080 averaged 29.9 FPS with 98–99% device GPU utilization and 6.71–7.03 GiB physical
footprint. These are historical measurements of motion1, not motion2 performance results.

## Blur bypass

For the exact known read-only fragment shader `e0305cef`, texture 0 is copied to attachment 0
using texture dimensions. The former exact viewport/scissor equality checks are removed: game
registers may be oversized, and the IFreemz route replaces the image rather than the draw's
register-defined rectangle. The old log does not identify which equality/texture check failed,
so no exact rejected field is claimed. New fallback messages include the rejection reason,
attachment count/layers, pass size, source/destination extents, pixel formats and source usage.

Equal-size/format/swizzle 2D single-sample textures use a Metal blit, preserving all color and
alpha bits and accepting lean RenderTarget-only source images. Equal-format images of different
host sizes use the existing scene-target nearest scaler when the source is sampleable. Missing,
multisampled, mismatched-format/swizzle, and overlapping views/buffer/IOSurface aliases are rejected
with a diagnostic. Exact source==destination is a safe no-op. Two successful bypasses are logged,
covering the known pair of final blur passes. The startup motion-blur setting remains authoritative;
velocity constructor generation is retained and the conflicting old disable-constructor patch is
still excluded only for the selected Metal game-velocity route.

## Performance and depth correctness

Motion1 interrupted the guest render pass after every velocity draw, encoded a mirror render pass,
and closed it with a fence. This causes repeated tile stores/loads and CPU encoder replay joins.
Motion2 records the read-only mirror commands of consecutive eligible guest draws, then replays
one host render pass when the guest pass ends. A 64-draw test verifies one additional mirror pass,
not 64. Up to 128 draws are queued; the component itself flushes at that bound. Variant PSOs and
read-only eligibility are memoized per immutable graphics pipeline, avoiding per-draw key copying
and PSO-cache locking. GPU geometry and fragment work are still duplicated; there is no claimed
same-scene FPS gain until another user-driven run. Periodic logs now report draws, actual mirror
batches, depth seeds and merged frames, so real batching effectiveness can be measured.

The source depth/stencil is now captured before the original velocity geometry writes depth.
Original pending clears are encoded first, then the original draw resumes with load actions.
Capturing post-draw depth could make mirror geometry self-occlude with strict LESS. The private
mirror depth evolves in original draw order and is seeded once per source/size per frame; an
explicit original depth/stencil clear invalidates the seed even when the source UID is unchanged.

## Ordering, occlusion and memory

A recording-thread render-tail hook drains the mirror before the next encoder, memory barrier,
hoisted work, indirect or non-velocity draw, ZPASS snapshot, frame boundary, resize or submission.
A guest pass with pending mirror work is joined before host replay rather than posted unchecked
to the encoding thread. Host replay uses the scheduler's host encoder entry point, so it never
acquires a visibility query. The hook is detached only after draining pending work on destruction.

No GPU completion wait, per-frame CPU texture readback, shader-cache reset, cache-size reduction,
additional worker, or vertex-history allocation is introduced. Texture reuse, the auxiliary
128-MiB estimate cap, residency accounting, no-copy alias correction and completed-handler
retirement remain. Deferred command lists hold pointers to resources already protected through
the original draw's GPU tick, and are drained before that tick is submitted. Command metadata is
bounded by the 128-draw limit. Shadow/effect/LOD/resolution settings and the Vulkan fallback are
unchanged. Installation preserves the user's current preset, including Performance x2.

## Verification and remaining validation

Offscreen Metal tests check exact RGBA/alpha blits from lean images, scaled copies and conservative
rejection, 64 draws to one batch, strict-LESS depth, source-texture overwrite ordering, memory
barriers, hoists, frame/size/submission boundaries and the 128-draw cap. Actual ZPASS begin/end
snapshots verify an empty guest pass becomes hidden after three zero intervals even when each
interval includes 64 visible host mirror triangles. Existing motion, scheduler, occlusion,
budget, cache-GC and sampler tests run in held/replay, direct and Metal validation modes.

No game is launched automatically. The next gameplay run must confirm the two successful blur
copy messages and inspect draw/batch ratios, visual motion correctness and same-scene frame time.
These tests do not establish that the runtime's previously rejected permutation is now accepted;
that remains a live validation requirement.

Source route: [IFreemz 149f63645bbb](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR/tree/149f63645bbb40fa587daa68fbf003b1f6dbcd7d),
including `vk_bb_temporal_dlss.cpp`, `vk_bb_velocity_mirror.cpp` and `vk_rasterizer.cpp`.
