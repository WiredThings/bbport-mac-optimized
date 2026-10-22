# Native-resolution attachment clear batching — experiment, 2026-10-21

Later source update (2026-10-22): this candidate is now exposed as a default-OFF experimental launcher control; the measurements below are historical. The installed application is still unchanged.

Status at original evaluation: implemented and tested, **OFF by default; no production application replacement**.
There is no established gameplay FPS or memory benefit. The current installed application,
normal saves, settings and normal shader cache are unchanged. This experiment is not a new
production launcher checkbox or an enabled release feature.

## What was found

Three bounded, warmed metadata frames at native 1920×1080, MetalFX OFF, show six consecutive
independent full-size color clears, each in its own encoder. The existing color-clear queue
was connected to reduced scene-proxy clears, while `Runtime::ClearImage` still emitted each
native clear immediately. The same queue can combine this observed six-pass sequence into
one six-attachment clear pass. It does **not** eliminate the six necessary attachment clears
or stores; it removes five encoder boundaries.

A separate metadata-comparison prototype tested ignoring unused clear values/unbound slots.
Actual captures reported zero matching opportunities. That runtime prototype was removed;
it is not included in patch 0041. Active layout transitions remain conservative because they
can signal attachment feedback dependencies even though Metal has no Vulkan-style layouts.

The snapshots are bounded decoded guest/native metadata, not complete GPUTools traces or
resource-content captures. They do not cover all shader, CPU or aliased readers. Therefore
arbitrary main-pass color/depth/stencil Store actions are not converted to DontCare.

## Exact change and dependencies

Adapter 0041 connects complete native color clears to `Scheduler::QueueColorClear` after the
existing Transit/FlushBarriers and scene-proxy handling. It retains the original clear value
conversion and image dirty/GPU-modified bookkeeping. It requires a full single-level,
single-layer clear and matching guest/backing formats. The existing queue additionally
rejects nonprivate, aliased, heap/buffer/IOSurface-backed, parent-view, multisample, mipmapped
or layered textures and depth/stencil formats. Ineligible cases use the original code.

`BB_MTL_NATIVE_CLEAR_BATCH=1` enables the candidate. `BB_MTL_BATCH_CLEARS=1` is also required;
the existing parent OFF remains authoritative. No normal launch profile enables the new
flag. `BB_MTL_FOLD_CLEARS` retains its existing matching and hoisted-compute fallback rules.
Other consumers and submission flush queued clears through the existing scheduler ordering.

No texture, buffer, PSO, cache entry or larger budget is introduced. This is not a cache-size
reduction, shorter retirement grace period or deferred-frame history. The final attachment
stores remain. Velocity/blur bypass, occlusion queries, BDA addressing, invalid-resource
reclamation, speculative-format rejection (0038), dimensional view validation (0039) and 1D
lowering (0040) are not modified. The generic queue label now says `Scene color clear batch`.

## Correctness checks

- Real ClearImage OFF / ON / parent-OFF runs with Metal API validation: eight mixed color
  attachments, repeated clears, download visibility and copy visibility. All **3,584** recorded
  pixel bytes are exactly equal, including sRGB, RGBA16F and packed float formats. The test also
  exercises existing integer, array, mip, depth/copy cases and verifies complete texture-object
  retirement. This is a bounded synthetic comparison, not whole-game image equivalence.
- Nine regression executables pass with the candidate and existing clear-folding enabled:
  runtime, scheduler, scene targets, BDA, motion shaders, texture views, texture GC, guest
  resource lifetime and speculative descriptors.
- 61 benchmark/control tests, 10 attachment-report tests and two launch-profile tests pass.
  The launch-profile fixtures require shell process substitution; their initial sandbox
  `/dev/fd` denial was rerun successfully outside that filesystem restriction.
- All 41 serialized adapters reconstruct and reapply idempotently. The source prototype that
  had no actual hits is not retained in the build.

Three actual candidate captures each show one six-target 1920×1080 clear batch. Aggregate
encoder counts vary with unrelated frame work; they are not used to claim an overall five-
encoder or FPS reduction from unlike captured frames.

## Interleaved gameplay comparison

M4 Mac mini, 16 GiB; one private signed application and renderer/probe pair; the same existing
frozen save/settings/cache cohort and camera (13.000, 22.420, 134.979). This is not a new
Central Yharnam measurement. Native 1080p, MetalFX OFF, unchanged effects and the owner's
other experimental selections. Only `BB_MTL_NATIVE_CLEAR_BATCH` differs. Fragment uniforms
remain explicitly ON in both groups, matching the cohort.

Each run uses 45 seconds warmup plus the existing quiet window and 45 seconds scoring.
Detailed GPU timing and capture are OFF. Every accepted score has zero shader compiles and
matching stationary-camera/draw-count validation. Both ABBA and reverse BAAB completed.

| Suite / run | Candidate | FPS | 1% low | Median footprint MiB |
| --- | --- | ---: | ---: | ---: |
| ABBA A1 | OFF | 62.08 | 50.60 | 5554.0 |
| ABBA B1 | ON | 62.21 | 50.09 | 5543.0 |
| ABBA B2 | ON | 63.13 | 40.09 | 5392.8 |
| ABBA A2 | OFF | 61.65 | 49.62 | 5534.1 |
| BAAB B1 | ON | 62.08 | 51.92 | 5561.9 |
| BAAB A1 | OFF | 62.73 | 41.71 | 5394.5 |
| BAAB A2 | OFF | 63.03 | 49.31 | 5396.5 |
| BAAB B2 | ON | 61.90 | 50.05 | 5573.5 |

Pooled means of per-run results:

| Metric | OFF | ON |
| --- | ---: | ---: |
| Mean FPS | 62.3691 | 62.3301 |
| Mean 1% low FPS | 47.8085 | 48.0390 |
| Mean p95 frame ms | 17.3350 | 17.2425 |
| Mean median footprint MiB | 5469.77 | 5517.80 |
| Mean median owned Metal MiB | 1089.11 | 1134.47 |

Mean FPS differs by **−0.0625%**. The first suite's +1.31% direction reverses in BAAB. Bad
low-frame runs occur in both groups. There is no repeatable gameplay gain. ON footprint is
about **48.03 MiB higher**, but the approximately 160–181 MiB within-group ranges and resource
retirement differences prevent attributing that small delta to the candidate. A zero-memory-
regression or memory-saving claim is not established. Resource capacities and footprint are
separate ledgers and must not be added together.

All scored windows recorded stable warning pressure and zero positive swap growth; no
accepted run hit a memory/session guard. All eight cleanups verified unchanged original saves
and stopped only owned processes; the benchmark wrote no normal launcher preferences. Early
failed diagnostics remain excluded: critical pressure, then total swap growth after the owner
disconnected UU. A private diagnostic packaging dependency-path error was also fixed before
the successful captures. UU causality was not established. Chronod measured about 645 MiB,
so the owner's conditional restart authorization was not exercised.

Decision: retain a default-OFF developer experiment and the evidence, without replacing the
installed application or publishing a release. A lower operation count alone does not prove
a useful frame-rate improvement. Further main-attachment elimination requires explicit reader,
alias and dependency evidence, rather than speculative Store/load suppression.

Local evidence: `outputs/main-attachment-audit-20261021/`; original private run traces remain
outside the source repository. No saves, game textures, generated game shaders or credentials
are included in this report or adapter.

The follow-up [CPU/GPU timing audit](METAL-FRAME-TIMING.md) reproduces undercounting in the
legacy callback-order GPU statistic. Sorted bounded traces show essentially unchanged draw
interval coverage with this candidate. A lower legacy `gpu_ms` is not evidence of a GPU win.
