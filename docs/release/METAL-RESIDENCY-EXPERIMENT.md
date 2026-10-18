# Precise indirect-resource residency experiment

**Later update (0.2.19):** the owner authorized a warning-tolerant guard and combined A/B,
then accepted the observed ~105 MiB footprint increase. The combined launch profile is now
enabled; individual-stage reports below retain their historical status and evidence limits.
See [current guard, results and activation decision](METAL-BENCHMARK-GUARD-V3.md).

Status: `BB_MTL_PRECISE_RESIDENCY=1` is **experimental and OFF by default**. The installed
0.2.18 app and original launcher settings remain unchanged. No accepted gameplay FPS result
or 60 FPS claim is available. The default may change only after matching performance and
memory/slow-frame regression gates pass.

## Scope and correctness

Previously each render/compute encoder declared every imported guest chunk read/write and all
two-level address-table leaves readable, even for fixed host copies/clears/postprocessing that
only use directly bound textures/buffers. Apple's [argument-buffer residency documentation](https://developer.apple.com/documentation/metal/tracking-the-resource-residency-of-argument-buffers)
and [useResource API](https://developer.apple.com/documentation/metal/mtlcomputecommandencoder/useresource%28_%3Ausage%3A%29)
explain that declarations also affect tracked-resource dependency hazards. A declaration count
is not a measured cost, and fewer declarations alone do not establish better FPS.

Fixed host shaders are explicitly classified with HostRenderEncoder or the bound-only compute
parameter. Ordinary plain graphics shaders can omit the broad indirect set only when their
SPIR-V capability scan proves neither vertex nor fragment stage uses physical storage-buffer
addresses. This capability is read from SPIR-V even on MSL disk-cache hits. Tessellation,
geometry, rect-list synthesis, unknown APIs and game compute keep the conservative original
behavior. Direct bindings, visibility-result buffers, resource lifetimes, fences and GPU
submission order remain intact. Shader code, precision, sampling and pipeline cache keys do
not change.

Residency is the union of all shaders in a pass. A later raw-pointer stage upgrades a recorded
pass before replay; the replay job carries its captured classification. A physical render pass
is upgraded before encoding the raw-pointer draw. A reused or hoisted compute encoder is also
upgraded before a guest indirect dispatch and never downgraded. This preserves guest pointers
and both levels of the page table. No new texture/buffer, staging pool or persistent GPU cache
is allocated. Blit encoders never made actual indirect declarations; their redundant empty
helper call is simply skipped.

Tests use a raw-address source buffer deliberately absent from the residency set. They verify
host-to-guest upgrades in the same compute encoder, mixed/hoisted work, direct/held/replayed
raw-pointer fragment draws and exact output pixels, with Metal API validation. Strong test
pipeline/library references remain alive through command-buffer retirement. Original whole-set
behavior remains available for every unknown or unsupported case.

## Current game observations

A private Central Yharnam diagnostic frame classified 114 of 155 render encoders as directly
bound and retained full guest declarations for 41 render and 61 compute encoders; 30 compute
encoders were bound-only. Physical encoder counts vary with animation/startup and are not an
A/B performance claim. The captured frame had no reported GPU fault. Two bounded final-display
snapshots, original and precise policy, showed the scene, character, shadows and HUD intact.
They were adjacent-time images with animation differences, not a synchronized pixel-equivalence
or whole-game visual guarantee.

Short captures still reached system warning pressure. Reference and experimental long runs
that hit the strict memory gate were stopped without accepted FPS results. Startup-dependent
staging counts and footprint varied, so no RAM saving or non-regression is claimed. The
candidate remains OFF; resolution, effects and cache capacities are unchanged.

## Bounded final-display diagnostics

`benchmark.py run --capture-native --capture-display --mute --minimal-stats` optionally captures
one final game display alongside attachment metadata. The renderer rejects readbacks larger
than 32 MiB before allocating a buffer. The current display is 1920x1080 RGBA, 8,294,400 bytes.
The harness rejects older apps without that bound, validates dimensions/file size/hash and
rejects logged GPU faults. Snapshots remain local with the private run; game assets/saves are
not exported to GitHub. This is neither a complete resource snapshot nor a replayable trace.
The diagnostic mode cannot produce or compare an FPS score. Normal Play clears its triggers.

A separate offscreen vertex-parameter experiment kept strict arithmetic and changed only
statically bounded uniform read address spaces. All tested output bits, including NaN payloads,
matched on three hot shaders, but timing did not improve consistently. It remains a rejected
local probe and is not included in the renderer.

Validation checkpoint: 71 serial native groups pass against the current backend, including
original modes, raw-pointer residency unions in all replay/hold modes, two-level BDA and
combined velocity/occlusion/transfer/memory paths. There are also 32 benchmark checks and
16 packaging/report/patch checks; the 21-patch source series reconstructs and reapplies
idempotently. Both private display runs completed with unchanged original-save hashes and
no reported GPU fault. No accepted scored FPS run is included in these counts.
