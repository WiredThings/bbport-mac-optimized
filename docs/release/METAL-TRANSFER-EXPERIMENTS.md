# Lossless transfer experiments and unattended measurement

**Later update (0.2.19):** the owner authorized a warning-tolerant guard and combined A/B,
then accepted the observed ~105 MiB footprint increase. The combined launch profile is now
enabled; individual-stage reports below retain their historical status and evidence limits.
See [current guard, results and activation decision](METAL-BENCHMARK-GUARD-V3.md).

Status: development candidates, **all three renderer optimizations default OFF**. The installed
0.2.18 app remains unchanged. No accepted gameplay FPS improvement or 60 FPS result is claimed.
Resolution, effects, sampler quality, texture/cache capacities and game saves are unchanged.
The source is pinned to Metal engine 11992198df6306dacf6ec37de57ee920db1acc11 plus patches 1–20.

## Texture directly to guest tiled memory

`BB_MTL_TEXTURE_TILING=1` fuses texture download and the original PS4 tiling kernel. Its strict
source adapter preserves the generated macro-address calculation and replaces only the linear
RGBA8 reads with exact texel reads and UNORM8 packing. Unsupported generated text/compiler
combinations keep the original path. Only single-layer/mip/sample 2D RGBA8 UNORM is accepted,
with eight-texel aligned width and matching native/guest row pitch. Integer, float, sRGB, depth,
compressed, layered and mipmapped formats retain the original implementation. Pipeline creation
is bounded to 32 variants; no additional texture or staging allocation is introduced.

When an existing dirty proxy is exactly half the native extent in both axes and has a separate
private nonaliasing backing, native pixels are synthesized by the same nearest 2x resolve.
The native texture remains stale/dirty; a later real native access resolves it normally. There
is no new proxy, cache, readback, CPU copy or shortened resource retention policy. The regular
image-access hook drains pending velocity/render-tail work before selecting the proxy.

The original tiler visits padded rows whose temporary linear bytes are unspecified. The fused
kernel skips these rows and preserves their guest bytes. Tests compare every defined pixel,
padding preservation and allocation canaries; they do not report unspecified old padding as a
visual difference. Nine native cases cover padded height, 1080p, random channel bytes and three
bank swizzles. A proxy test compares against materialized native output, confirms the native
backing stays untouched, then verifies subsequent native reads. API validation is enabled.

In a balanced 40-sample isolated command-buffer test on the M4, with existing buffers and warmed
pipelines, old download+tiling averaged 0.811 ms (median 0.771), versus 0.409 ms (median 0.418)
for native texture tiling. This is **one isolated transfer**, not total frame time or FPS.

Private fixed-scene metadata captures confirm one fused copy per gameplay frame. The proxy
version also removes its preceding native resolve. A short capture retained 272 MiB of staging
in 14 objects and ended near 5.35 GiB process footprint. The earlier native-only capture retained
352 MiB in 19 objects and ended near 5.55 GiB. These different startup trajectories are preliminary
observations, not a controlled memory-saving claim. No scored FPS run has passed the memory gate.

## Direct source proxy to native copy destination

`BB_MTL_DIRECT_COPY=1` uses the existing nearest resolve straight into a complete disjoint copy
destination, avoiding the source-native resolve plus native blit. The source stays dirty and
materializes only if later read. Exact 2x extent, same format, single-level/layer/sample private
standalone resources and nonoverlapping guest ranges are required. Four-format output and
coherence tests pass; real calls are observed. No additional resource allocation is made.
Gameplay benefit is not established, so the default remains OFF.

## Fitting GPU-only staging blocks

`BB_MTL_ADAPTIVE_STAGING=1` rounds newly required device-local ring blocks above 8 MiB to 64 KiB
instead of allocating a full 16 MiB block. CPU-upload/readback pools and existing objects are
unchanged. The retained **byte** budget remains 512 MiB per pool; no cache-size cut or shorter
idle deadline. Eight simultaneous 9 MiB requests use 72 instead of 128 MiB in the unit test,
reuse identical objects after GPU completion and wake correctly after idle backing discard.
Actual mixed game requests did not establish a reliable saving, so this remains OFF.

## Measurement and safety

The frozen cohort f443903ad7a2b0f974434b93d76e5d80924a1321b534f57759383db0ad5dfff2 loads directly
into Central Yharnam at camera (-190.784, -27.226, 71.116). It is separate from the previous
save that died and respawned. Each run uses private save/config/cache paths and verifies unchanged
original save hashes after stopping only its own process group. Local game resources and saves
are not distributed.

`benchmark.py run --mute --minimal-stats` uses the original CoreAudio stream at zero stream gain
and buffered lightweight flip/camera/draw/compile/GPU-busy telemetry. Full per-stage statistics
remain off. Mute requires scripted private input and never changes system/app volume preferences.
Normal Play clears benchmark mute and lightweight telemetry flags. Comparison rejects different
audio or telemetry modes; missing full-stat fields are reported as unavailable, not zero. Previous
full-stat scores cannot serve as the lightweight baseline.

The memory guard requires normal initial pressure, stops on critical pressure or bounded swap
growth, stops after 30 seconds of sustained warning during startup/warmup, and rejects any warning
in a scored window. Several reference and candidate runs hit warning pressure and were cleaned
up without accepting FPS results. Protection was not relaxed to obtain a score. Builds use two
jobs and run after the game has stopped. Large Apple resource-snapshot captures are not retried.

Rejected probes: packed vertex loads were byte-exact but generally slower; presentation/gamma
reordering introduced small pixel differences. Neither is integrated into the renderer.

A later milestone must obtain matching valid A/B repeats, unchanged quality and no memory/slow-frame
regression before enabling a candidate and replacing the installed application.

Validation checkpoint: all 19 native test executables were relinked against the current backend;
58 serial native regression groups passed (including replay/held/direct combinations and
combined transfer flags), plus 29 benchmark and 16 packaging/report/patch tests. The complete
20-patch series reconstructs the current source exactly and reapplies idempotently. These
gates establish tested correctness and compatibility, not a gameplay speedup.
