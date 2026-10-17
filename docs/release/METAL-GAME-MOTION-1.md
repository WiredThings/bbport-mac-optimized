# Game velocity for MetalFX (0.2.4-metal-motion1)

This ports the velocity-mirror route from IFreemz's Bloodborne temporal upscaler to the existing
native Metal backend. The launcher adds **Advanced > Upscaler > Game object motion vectors
(experimental)**, on by default for MetalFX. It has its own persistent preference; the older
Vulkan vertex-history object-motion switch remains separate and disabled by default. Vulkan is
unchanged. Switching this feature requires restarting the game, with no cache deletion.

Primary sources, pinned to IFreemz/shadPS4-Bloodborne-DLSS-FSR commit
`149f63645bbb40fa587daa68fbf003b1f6dbcd7d`:

- [Velocity mirror](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR/blob/149f63645bbb40fa587daa68fbf003b1f6dbcd7d/src/video_core/renderer_vulkan/vk_bb_velocity_mirror.cpp)
- [Pipeline eligibility](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR/blob/149f63645bbb40fa587daa68fbf003b1f6dbcd7d/src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp)
- [Packing decode](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR/blob/149f63645bbb40fa587daa68fbf003b1f6dbcd7d/src/video_core/host_shaders/bb_dlss_motion.comp)
- [Constants and final blur bypass](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR/blob/149f63645bbb40fa587daa68fbf003b1f6dbcd7d/src/video_core/renderer_vulkan/vk_bb_temporal_dlss.cpp)

## Rendering and effect compatibility

Recognized fragment hashes: `34bc187c` (packing buffer 0), `749e4f9e` and `b25e4fae` (buffer 1).
The original game draw remains; eligible direct, read-only velocity draws are replayed into a
scene-input-sized RGBA16F target. One-color RGBA8, single-sample/layer, full viewport and D32S8
or D16 depth are required. Shader buffer/image writes and atomics, tessellation, geometry and
indirect replay are excluded. Depth/stencil is snapshotted from the original velocity pass into
an owned source-sized texture, then scaled into private mirror depth. This avoids promoting the
shared cache's lean textures just to create a stencil view. Viewport/scissor scale follows IFreemz;
there is no additional jitter offset.

Eight bytes of game packing constants are captured once per frame by GPU blit, never by CPU
readback. Covered pixels decode `(rg - .5) * (-width/sx, +height/sy)` into the existing RG16F
camera-motion texture before MetalFX. Missing coverage, invalid coefficients or non-finite /
out-of-range vectors preserve camera motion. Unlike upstream's final zero clamp, invalid object
vectors retain the camera result. No vertex-history buffers are introduced. A frame without an
eligible pre-upscale velocity draw stays camera-only; old frame data is never reused.

The built-in **Disable Motion Blur (perf increase)** patch writes zero into the constructor at
`0x026C2549`, which also suppresses the velocity map. With `BB_RENDERER=metal`,
`BB_METAL_OBJECT_MOTION=1` and MetalFX selected, that patch is excluded (including explicit built-in
selection), while `effect_motion_blur=0` is kept. The two known final blur passes (`e0305cef`) are
replaced with scene-texture-0 copies when blur was off at startup. Unexpected permutations are
logged and use the normal renderer path; full-game validation is needed to establish coverage.
Conflicting external writes to the known constructor byte stop patch compilation with a clear
error. Other effects and LOD selections are preserved. Turning the experimental toggle off restores
the existing performance patch at the next launch. No game files are modified in place.

## Occlusion and memory compatibility

Mirror passes carry an immutable host/guest tag through direct, held and threaded replay. They,
the depth seeding and the blur-copy passes never contribute to guest ZPASS counts. Existing
max-of-three occlusion history, native query limits and conservative fallback are retained.

Textures are reused across frames. Size/format changes and shutdown retire resources through the
existing completed-command-buffer queue, including residency removal and pipeline lifetime.
No new GPU-completion wait, CPU readback or cache-size reduction is added. The scheduler can
join its encoder replay thread when crossing between guest and host passes. Producer reactivation,
extra velocity draws, depth copying and the merge dispatch do cost GPU work; this improves temporal
inputs and is not an FPS optimization. At the user's current 1280x720 input the RGBA16F plus D32S8
mirror is roughly 14 MiB before the small original-size depth snapshot and allocation alignment.
Auxiliary texture dimensions have a 128 MiB estimated cap; larger cases use camera motion.
The original memory accounting, idle retirement and sampler memo remain active.

## Resolution and verification

The last user run before this update logged **1280x720 scene input -> 1920x1080 output**, MetalFX
**Quality x1.5**, with 640x360 half-resolution passes and native-output-size UI. No resolution,
effect, LOD or existing preference is changed by installation. Output size is in **Advanced >
Display**; input scale is **Advanced > Upscaler > Preset**. At 1080p output Performance x2 means
960x540 input, Balanced x1.7 means 1130x636, Ultra Performance x3 means 640x360.

Offscreen Metal tests cover real RGBA16F draws with depth/stencil tests, D32S8/D16 seeding from
cache-like textures without PixelFormatView, coefficient capture before a subsequent GPU mutation,
coverage/sign/scale, zero and non-finite coefficients, stale-frame fallback, resize retirement,
auxiliary budget rejection and host/guest tag propagation through replay. Metal API validation
and direct/held modes are checked. Existing occlusion, budget, idle-GC, sampler, scheduler and
runtime tests are rerun; Swift tests check preference persistence/backend isolation and Python
checks cover patch conflicts and exact-prefix series replay.

Tests do not launch the game. Full-game character/combat ghosting, velocity-pass order/coverage,
final blur bypass and same-scene frame time remain unverified. The runtime logs the first mirrored
draw, first merged frame, first final-blur copy, late velocity fallback and periodic totals, so the
next user-driven run can distinguish actual velocity use from camera-only fallback.
