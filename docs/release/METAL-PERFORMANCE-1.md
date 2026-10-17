# Metal rendering efficiency review and sampler memo (0.2.2-metal-perf1)

Reviewed on 2026-10-17 for a base M4 Mac mini with 16 GB RAM. The installed renderer remains
Huax-1839/bloodborne-macos at 11992198df6306dacf6ec37de57ee920db1acc11 plus our four patches.
Resolution, effects, LOD, MetalFX preset, shader arithmetic and shader cache location are unchanged.

## Adopted

Adapted the sampler memo from [Awakenchan/BloodBorneButPc 4f93643e](https://github.com/Awakenchan/BloodBorneButPc/blob/4f93643eb573ae445a24db1d64eabf663a5d5147/engine/bbhost-changes.patch).
The texture-binding thread caches 1024 sampler references, keyed by the complete sampler descriptor,
depth-comparison flag and exact LOD-bias bits. This avoids redundant descriptor hashing, mutex and
map lookup work for repeated samplers. Hash collisions compare the complete key.

The original texture cache still owns every sampler. A memo hit refreshes its LRU once per GC tick;
a collection increments a generation before erasing a sampler, invalidating remembered references.
This adds a fixed small CPU table, does not pin GPU allocations and does not reduce any cache budget.
Both the Vulkan handle and the native Metal object are carried in the reference. BB_SAMPLER_MEMO=0
restores the original lookup path. GC runs with the draw pipe drained (RasterizerCore::OnSubmit).
The lookup key matches the existing GetSampler behavior, including its handling of border colors.

This reduces CPU work in rendering submission. It does not remove game draws or make individual
GPU shaders cheaper. No gameplay FPS improvement is claimed before a matched-scene comparison.

## Reviewed, already present or inapplicable

| Source and change | Current Metal compatibility |
|---|---|
| Huax dd6f3577: lean texture usage / promote on first write | Already in pinned backend |
| Huax ea029d9: V1 shader float rules | Already default; no further precision changes |
| Huax d9619bc / b0a3d62: replay thread and frame-thread QoS | Already present |
| Huax 3ad06d52: pass-image membership stamps | Already present |
| Livio21/metalborne f7b5286c, 5c8676bd, 7401d979: retain batches across idle copy/signal checks | Current direct-Metal scheduler uses no-op copy/deferred waits; these patches address the hybrid Vulkan/Metal path |
| Wooloomooloo2 f921675f: hoist copy-shader copies with range checks | Current core already hoists eligible original translated dispatches, with pass read/write and image dependency checks |
| Wooloomooloo2 ddfe537b / 39eef6dd: MoltenVK prefill / argument buffers off | MoltenVK-specific; does not apply to direct Metal |
| Awakenchan: real occlusion queries with stabilized visibility | Vulkan query-pool backend; current Metal CreateOcclusion returns null. Requires a new Metal implementation and moving-scene correctness checks, not a direct transplant |
| Awakenchan: retained mip hashes / parallel hashing | Its guest-versus-VRAM split differs from in-process no-copy Metal memory; retaining stale guest hashes is unsafe without a separate coherence audit |
| Awakenchan: skip uncompiled draws | Can temporarily remove geometry; excluded from this visual-preserving change |
| Huax 70e7fdf: hoist WRITE_DATA via compute | Reverted upstream by 63432a5c after GPU costs outweighed the saved encoder work |

Primary references: [Huax backend plan](https://github.com/Huax-1839/bloodborne-macos/blob/11992198df6306dacf6ec37de57ee920db1acc11/docs/METAL_BACKEND_PLAN.md),
[Metalborne packet-loop fix](https://github.com/Livio21/metalborne/commit/7401d9793194f6f34f3a3e3801503c25f400e1c1),
[Wooloomooloo2 copy hoisting](https://github.com/Wooloomooloo2/bloodborne_mac_port/commit/f921675f7ed416f1d4300120937a26278434731f),
[Awakenchan engine notes](https://github.com/Awakenchan/BloodBorneButPc/blob/4f93643eb573ae445a24db1d64eabf663a5d5147/engine/README.md).

## Compatibility and validation

Patch 0004 follows the existing ABI, menu-shortcut and memory-budget/retirement patches.
It changes shared resource selection and sampler-GC invalidation only; CPU runtime, quicksave,
Chinese localization, texture/staging budgets, retirement fences and frame settings are retained.

The sampler-memo regression checks forced hash collisions, depth/LOD separation, repeated-hit
lookup counts, active LRU retention, actual LRU-slot reuse and invalidation after collection.
The existing offscreen Metal runtime test samples a real texture through the memoized native
sampler and checks the bilinear pixel value, in addition to its resource-retirement tests.
GPU tests do not start the game. Full-game visual/performance validation remains outstanding.
