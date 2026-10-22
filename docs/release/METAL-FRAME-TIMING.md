# Frame supply, GPU intervals and final-overlay experiment — 2026-10-21

This is a local diagnostic/developer experiment. The installed Blood Echoes 0.2.47,
normal saves, settings and normal cache have not been replaced or cleared. Private apps,
frozen saves and a private cache were used. Native 1080p, MetalFX OFF and the owner's
existing effect selections are retained. The old frozen camera (13.000, 22.420, 134.979)
is the same cohort as the native-clear experiment, **not a new Central Yharnam result**.

## What the lightweight CPU records establish

Adapter 0042 adds opt-in `BB_FRAME_TIMING=1` fields to the minimal CSV. It reads existing
cumulative counters, without enabling `BB_FRAME_STATS`, per-draw scopes or GPU counters.
It reports recording-thread packet wall time, scheduler/replay waits, frame-throttle
waits, guest submission waits, flip ages and texture-find calls. The normal profile has
none of these flags. Comparison rejects unlike CPU-timing instrumentation.

A first 45-second window, after warmup, recorded:

| Metric | Mean |
| --- | ---: |
| Frame interval | 15.418 ms |
| Recorder packet wall time | 6.315 ms |
| Recorder/replay wait counter | 1.068 ms |
| GPU scheduler wait counter | 0.00056 ms |
| Frame-throttle wait counter | 0.00001 ms |
| Guest submission wait counter | 13.368 ms |
| Submit-to-flip age | 0.088 ms |
| Ready-to-flip age | 0.0089 ms |

These are overlapping asynchronous intervals, not an additive frame-time breakdown.
Recorder packet time includes descheduling and waits; it is not thread CPU usage. A
3-second stack sample was taken during warmup and ended before scoring. Its small sample
count is corroborating path evidence, not a precise percentage profile. The 64.86 FPS of
this one run is not an optimization gain or a whole-game result.

The guest's previous-frame gate protects resource reuse. It is not removed, advanced or
given more frames in flight. No resource lifetime/format/dimensional checks are weakened.

## Why the old GPU statistic looked better without higher FPS

The existing `BbStats::NoteGpuInterval` advances a high-water timestamp in completion
callback order. Across draw and present queues this is not GPU timestamp order. A later
interval arriving first can hide earlier intervals reported afterwards. A directed test
using the actual function reproduces this: [20,25] followed by [10,21] records 5 seconds;
the true union is 15 seconds. `gpu_ms` has always been command-buffer interval coverage,
including dependencies, rather than shader execution time or hardware utilization.

0042 also provides `--capture-submit-timing`: after warmup it records 2,048 CPU enqueue,
GPU start/end and callback timestamps, then atomically publishes a metadata CSV. The
record buffer is at most 128 KiB CPU memory and exists only when explicitly requested.
There are no texture copies, GPU readbacks, counter buffers or extra GPU operations.
The capture is a diagnostic and never a scored FPS result. Normal launches do not
allocate this record buffer. Offline analysis sorts intervals before merging them.

| Bounded trace | Native clear OFF | Native clear ON |
| --- | ---: | ---: |
| Records (draw / present) | 1803 / 245 | 1803 / 245 |
| Complete interior host frames | 243 | 243 |
| Sorted interval union, whole trace | 3907.65 ms | 3888.05 ms |
| Legacy callback-order estimate | 3523.88 ms | 3287.24 ms |
| Legacy missed coverage | 9.82% | 15.45% |
| Mean draw interval coverage / frame | 15.996 ms | 15.941 ms |
| Mean draw interval span / frame | 16.091 ms | 15.943 ms |

The apparent GPU-time improvement is predominantly changed undercounting. Sorted draw
coverage differs by only 0.055 ms in these short captures. This is not a statistical
speedup claim. Draw command-buffer intervals are almost continuous and submitted well
ahead of GPU starts (about 11 ms on average), so long CPU supply gaps are not supported
in this cohort. GPU execution/dependencies remain the stronger lead. This does not
distinguish fragment arithmetic, texture bandwidth, tile traffic or GPU-internal waits.
Draw and present intervals overlap and must not be summed.

A further interleaved CPU-timing comparison found native clear OFF 62.90 vs ON 61.19 FPS,
with no repeatable gain. Its original final A overlapped a 2-second standalone diagnostic
test compile, was excluded, and was replaced by a clean A repeat. Raw results and the
exclusion are retained. Native clear batching remains default OFF, as in adapter 0041.

## Safe CPU cleanup

0043 changes only the actually used `gpu/shim/common/debug.h` TRACE_HINT stub. It used to
evaluate discarded text, including the `fmt::format` in `TextureCache::RefreshImage`.
The disabled stub now does not evaluate the argument. The only two callers are a queue
name getter and that texture diagnostic; neither has gameplay side effects. A regression
test verifies no evaluation with both temporary-string and string-view call forms.

The similarly named upstream debug header is shadowed by the shim. An initial edit there
was reverted after the test caught the wrong include path; it is not in the adapter.
The active shim previously evaluated once, not twice. Short text may use SSO, so this is
an eliminated formatting/temporary-string operation, not proof of one heap allocation
per update. No measured gameplay FPS gain is attributed to this small cleanup.

## Final present plus overlay candidate

Source update (2026-10-22): merged presentation is retained as a default-OFF experimental launcher control. No new FPS gain is claimed; the comparison below remains historical.

0044 adds a default-OFF `BB_MTL_MERGE_OVERLAY=1` developer experiment. The present draw
and ImGui overlay can share the same single-sample BGRA8 drawable encoder. ImGui still
runs through `BbOverlay::Render` and its existing mutex, backend NewFrame, dynamic texture
updates, viewport/scissor/pipeline setup and callbacks. The caller closes the borrowed
encoder. With the flag OFF, the old two-pass path remains. Invisible overlays still use
the existing early return; interpolated and real output frames follow the same logic.

There are no new textures, buffers, PSOs, longer-lived frame resources or cache budgets.
Only the final host presentation path is touched. Guest draws, velocity/blur bypass,
occlusion queries, two-level BDA, 0038/0039/0040 validation and memory reclamation remain
unchanged. This experiment does not change any launcher preference.

The real Metal/ImGui glue passes API-validation comparison of **114,688 identical pixel
bytes**, including game content (explicitly checked nonblank), letterboxing, changing
target dimensions, clipped alpha UI, a dynamic font-size/glyph update, hidden/reopened UI
and a render-state reset callback. This is synthetic coverage, not whole-game pixel
equivalence or an end-to-end manual settings/IME test.

Four same-binary runs use 45 seconds warmup plus the existing quiet window, then 45 seconds
scoring. GPU stage counters, submit capture and CPU-timing exports are OFF. No compilation
or GPU tests ran in these scored windows. The owner's fragment-uniform preference remains
ON in both groups; native clear batching stays OFF in both.

| ABBA run | Merged overlay | FPS | 1% low | Median footprint MiB |
| --- | --- | ---: | ---: | ---: |
| A1 | OFF | 63.033 | 48.724 | 5416.3 |
| B1 | ON | 62.518 | 50.517 | 5544.9 |
| B2 | ON | 62.118 | 38.215 | 5562.8 |
| A2 | OFF | 63.341 | 52.915 | 5409.2 |

Means: OFF **63.187 FPS**, ON **62.318 FPS** (−1.375%). Mean p95 is 17.015 vs 17.300 ms.
Mean per-run median footprint is 5412.73 vs 5553.81 MiB (+141.08 MiB); owned Metal capacity
also differs by about 144 MiB. No new GPU resource allocation was introduced by the merge;
these resource-retirement phase differences do not establish its causal memory cost.
Nevertheless there is neither repeatable FPS benefit nor demonstrated memory benefit.
Every score recorded stable warning pressure and nonpositive swap growth. All four runs
have zero shader compiles and matching scene/draw validation. Original saves hash-identical;
only owned test processes were stopped. The flag remains **default OFF**, unshipped.

Nine real native regressions pass: runtime, scheduler, scene targets, two-level BDA, motion,
texture views, texture GC, guest resource lifetime and speculative descriptors. These run
after the game suite, with private user/cache/config paths and clear-folding/merge candidates
enabled where applicable. Concurrent recorder tests cover atomic complete publication,
unique records, saturation and a zero-allocation disabled record buffer. Disabled TRACE_HINT
and the real overlay pixel tests pass. 76 Python tests pass (61 benchmark/control, three
new timing checks, ten attachment-report and two launch-profile checks). All 44 adapters
reconstruct and reapply idempotently; the ordinary launch profile does not enable the new
diagnostics or the merged overlay. Existing memory budgets, grace periods and caches remain.

Reproduction against a private frozen cohort and private rebuilt app:

```sh
python3 tools/macos/benchmark.py run --workspace PRIVATE_COHORT --app QA_APP \
  --name cpu-timing --minimal-stats --env BB_FRAME_TIMING=1 --mute
python3 tools/macos/benchmark.py run --workspace PRIVATE_COHORT --app QA_APP \
  --name submit-timing --minimal-stats --capture-submit-timing --mute
python3 tools/macos/analyze_gpu_submits.py PRIVATE_RUN/gpu-submits.csv
cmake --build METAL_BUILD --target submit-timing-test trace-hint-test mtl-overlay-test --parallel 1
METAL_BUILD/submit-timing-test
METAL_BUILD/submit-timing-test enabled
METAL_BUILD/trace-hint-test
MTL_DEBUG_LAYER=1 METAL_BUILD/mtl-overlay-test
```

For an interleaved CPU-timing experiment, use `--fixed-env BB_FRAME_TIMING=1` in
`benchmark_candidates.py`. Instrumentation itself is not a score candidate. The raw
legacy field is retained for historical compatibility and its limitation is documented;
normal FPS comes from independent frame intervals and is unaffected by the coverage bug.
Callback-order reconstruction uses recorded callback timestamps and may differ slightly
from the concurrent counter's exact atomic operation order; sorted GPU intervals are the
reference for these bounded captures, not proof of hardware occupancy.

Local ledgers: `outputs/frame-timing-audit-20261021/`. Private raw traces and applications
are outside the repository. No game resources, saves, generated game shaders or credentials
are included in these adapters, tests or report. Neither an application replacement nor a
GitHub release is justified by these candidate results; this work remains local.
