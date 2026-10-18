# Same-packet cross-chunk snapshot reuse — 0.2.26 candidate

## Scope and ordering

The 0.2.25 fix gathers a read spanning imported guest chunks into the existing 128 MiB stream
ring. Patch 0035 optionally reuses an exact or wholly contained, correctly aligned snapshot
within one draw/dispatch resource-preparation packet. It never caches across packets, frames,
or submissions. Formatted/texel bindings retain their full synchronization/gather path.

Validity includes packet ID, submission tick, ring-wrap generation, announced CPU/source-write
sequence, runtime mapping sequence, layer-generation and the backend's buffer/global-write
sequence. Mapping sequences in the odd/in-progress state reject reuse. All recorded buffer
writes and unknown paged shader writes advance the Metal sequence, even across barrier resets.
Announced writes, fresh allocations, unmaps and write ticks invalidate the source sequence.

Only the gather's own writes into newly reserved stream-ring space may advance earlier entries
to the new backend sequence. A wrap or tick change discards older entries; a simultaneous CPU,
mapping or layer change prevents storing the new entry. Ring wrap is observable even if a
nonblocking reservation later cannot proceed. Required descriptor alignment and complete range
coverage are checked; partial resources, zero-fill and writable guest aliases are not substituted.

There are 16 CPU metadata entries (less than 512 bytes), without Metal resource ownership or
new GPU buffers. A hit reserves no ring bytes and records no copies. Cache capacities, image
lifetime rules, motion/velocity, occlusion, BDA shape, quality and floating-point choices are
unchanged. Correctness relies on the existing guest ordering/announced-write protocol; it does
not add protection traps or make arbitrary unsynchronized guest writes safe.

The switch is `BB_MTL_GATHER_REUSE=1`. It is exposed in Advanced / Metal optimizations as an
experimental, default-OFF option until repeatable relevant workload benefit is established.
Five-second cumulative counters distinguish requests, gathers, reused requests, avoided bytes,
and gathers/reuse while a render pass was open. Open-pass counts indicate potential boundaries,
not timestamp measurements of actual transition duration.

## Native validation

The real Metal runtime suite passed with API validation enabled: exact observed crash-size
copies, exact and contained snapshot reuse, unchanged full output bytes, earlier/later GPU
updates, no additional Metal allocation over 1000 lookups, every independent validity boundary,
odd mappings, entry capacity, global writes surviving barrier resets and real ring wrap.
The existing import, tiling/texture/format, occlusion, memory lifecycle and staging tests passed.
All 35 overlay patches reconstruct idempotently. Benchmark guard/metrics tests passed (43).
Gameplay A/B results are recorded after completion, separately from native correctness.

## Packaging regression found during preflight

Updates 0.2.24 and 0.2.25 copied the base `bb-probe` over the enhanced `bb-probe-metal` unless
an explicit probe argument was supplied. Thus isolated input, per-stream benchmark mute and
the memory snapshot ABI were lost. Initial benchmark preflight refused before launching a game;
that failed attempt has no accepted performance result.

The candidate restores the byte-identical enhanced probe from the signed 0.2.23 rollback.
Both A and B use that same probe. Future renderer/launcher overlays preserve the baseline's
Metal probe by default, falling back to the base probe only for legacy apps with no Metal
probe. An explicit recovery remains available and is checked for the memory ABI. Four new
packaging regression tests cover preservation, explicit recovery, legacy fallback and refusing
missing overrides. Recovery changes are recorded rather than represented as unchanged CPU code.

Evidence: local `outputs/metal-gather-reuse/` and `outputs/benchmark-m4-gather-reuse/`.

## Completed four-run comparison and activation decision

Same candidate renderer and recovered probe, latest frozen private save, unchanged 1920x1080
output / 960x540 MetalFX input, 45 s warmup plus 45 s scoring per run, minimal telemetry,
per-stream mute, no stage timer/capture during scores. The four owner-selected optional flags
(fragment uniforms, V2 math, inline updates, archive persistence) remain ON in both groups.
Only `BB_MTL_GATHER_REUSE` changes, in OFF/ON/ON/OFF order.

| Run | Reuse | FPS | 1% low FPS | Process median MiB |
| --- | --- | ---: | ---: | ---: |
| a1 | OFF | 56.82 | 38.14 | 4708.70 |
| b1 | ON | 56.34 | 34.51 | 4667.16 |
| b2 | ON | 58.40 | 38.21 | 4497.74 |
| a2 | OFF | 58.11 | 39.24 | 4525.16 |

Means of the run means: OFF 57.4630 FPS / 4616.93 MiB, ON 57.3705 FPS / 4582.45 MiB.
The FPS difference is -0.16%, but **all four runs recorded zero cross-chunk requests** (no
miss, gather or reuse report). Therefore neither the FPS/slow-frame differences nor the
-34.47 MiB process difference is accepted as a causal effect of snapshot reuse. This scene
cannot establish target-path benefit, pass-transition savings or memory savings. The runtime
native tests establish correctness when the path is exercised; the gameplay suite establishes
no general benefit. The new control remains experimental and **default OFF**.

All four score windows were warning-level pressure, with no positive score-window swap growth;
normal saves and launcher preferences were unchanged, and all owned processes were stopped.
A separate bounded display capture confirms the frozen save loads the Hunter's Dream graveyard
view with the controllable character and gameplay HUD, not a menu. That diagnostic capture is
excluded from FPS scores. This is not the older Central Yharnam cohort; comparisons use
matching camera/workload checks within this cohort.
