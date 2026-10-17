# Native Metal occlusion experiment (0.2.3-metal-occlusion1)

The launcher now has an experimental occlusion toggle under Advanced > Display, localized in
English, Simplified Chinese and Traditional Chinese. It defaults on for Metal and can be disabled
for the next game launch. Vulkan never receives BB_METAL_OCCLUSION=1. Resolution, effects, LOD,
shader precision, MetalFX preset, guest runtime, quicksave and shader-cache directories are unchanged.

## Implementation

This is a new Metal backend implementation of existing PS4 ZPASS_DONE events, using native
MTLVisibilityResultModeCounting. It follows the PS4 running depth-block counter convention and
adapts the sample-density compensation, per-pipe sharing and maximum-of-three result history from
[Awakenchan/BloodBorneButPc 4f93643e](https://github.com/Awakenchan/BloodBorneButPc/blob/4f93643eb573ae445a24db1d64eabf663a5d5147/engine/bbhost-changes.patch).
The hysteresis prevents an isolated zero at a jittered, reduced-resolution silhouette from instantly
hiding a light/object. Positive results are visible immediately; three consecutive zeros can hide.

A scheduler hook counts physical guest render passes, including passes encoded by the replay
thread and direct encoding with held passes disabled. Host image copies, MSAA conversion,
upscaling and presentation do not contribute. At each event a bounded single-thread compute
kernel adds the ended segments, compensates their resolution relative to 1920x1080, stabilizes
results, and writes valid begin/end counters directly into the existing in-place guest buffer.
Every depth block receives a share so the game's sum of end-minus-begin equals the total.

No CPU readback, asynchronous guest-memory writes, query-result polling, new GPU wait, or flush is
added to Event. The event does end a guest render pass and encode a compute dispatch, with the
existing fence mechanism before/after it. This has a cost; net GPU/frame-rate improvement must be
measured in the game. Existing command-processor predication waits are retained for correctness.
The game decides which work to skip from these counters; this does not add a second visibility
heuristic that arbitrarily discards host draws.

Fixed query/history storage is 224 KiB plus an eight-byte total. Scale metadata uses the existing
staging pool and its retirement rules. No new large cache or worker thread is added. A 4096-segment
limit and a 4096-entry history table bound memory. Exhausted segments, detected dropped work,
history collisions and unmatched end events report visible, rather than an unsafe hidden result.
History collisions can reduce culling effectiveness. On shutdown the replay hook detaches first;
query buffers and pipeline are released through the existing completed-command-buffer retirement
queue. The memory-budget/no-copy accounting and sampler-memo patches remain in force.

## Verification and limits

Offscreen tests use real depth-tested triangles, not synthetic query numbers. They cover a fully
visible/fully occluded object, sample-density scaling, four/sixteen depth blocks, multiple draws in
one pass, multiple passes on the replay thread, three-zero hysteresis and immediate reappearance,
GPU counter-buffer reuse inside one unsubmitted batch, exclusion of host rendering, an unmeasured
interval, full-ring conservative fallback, history collisions, and destruction with unsubmitted query work. Direct and held/replay modes and the Metal
API validation layer are checked. Existing runtime, texture/staging retirement, budget, sampler,
scheduler and scene-target tests are retained. Launcher tests cover persistence and backend isolation.

A full-game moving-camera/combat visual comparison and same-scene performance measurement are
still required. Tests do not start the game. Reduced-resolution subpixel geometry remains an area
to watch even with conservative history. If objects or lamps flicker, disable the experimental
launcher toggle and restart the game; no cache reset or save conversion is needed.

Primary API references: [Metal counting visibility results](https://developer.apple.com/documentation/metal/mtlvisibilityresultmode/counting),
[Apple's visibility-buffer sample](https://developer.apple.com/documentation/metal/culling-occluded-geometry-using-the-visibility-result-buffer).
