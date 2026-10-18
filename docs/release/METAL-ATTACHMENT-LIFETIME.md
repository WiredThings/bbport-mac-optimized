# Attachment lifetime audit and pending-clear folding

Scope: current direct-entry Central Yharnam cohort, unchanged output/internal resolution,
quality, effects, filtering, shader precision, cache capacities and the nine accepted flags.
Velocity/blur bypass, occlusion queries, two-level BDA and resource retirement stay intact.
**Owner decision after review:** enable `BB_MTL_FOLD_CLEARS=1` by default in 0.2.22,
alongside the previous nine flags. The owner explicitly accepts noisy isolated FPS/low-frame
results and prefers combined evaluation. The measurements below are unchanged; they do NOT
establish an isolated FPS gain or zero memory regression. Explicit zero override remains.

## What the trace proves, and what it does not

The three warmed reference metadata frames have 251/255/262 physical encoders, 291/293/297
attachment Load actions and 322/324/328 Store actions (the sorted ranges are not paired rows).
There are 69 observed queue/object/level/slice/aspect keys per frame. A depth/stencil pair is two
aspect records, not two independent allocations. A repeated native attachment identity does
not prove that shader, compute, transfer, guest-address, CPU or aliased readers are absent.
The report explicitly exposes `complete_reader_graph=false`; no arbitrary guest Store action
is changed to DontCare from inferred last attachment use.

A six-target 960x540 scene-clear batch precedes game compute and is retained. The velocity
clear has intervening packing/transfer work and is retained. Depth/stencil loads and stores,
shader sampling, native-copy coherence and all actual retirement fences remain unchanged.
One separate 960x540 RGBA16F color clear is immediately followed by a drawing pass using the
same texture (hot shaders VS 8cbc58fa / PS 2eacb983). This provides a narrowly provable
clear-store / drawing-load opportunity, rather than permission to discard final draw output.

## Safe transformation

The scheduler already owns at most eight independent queued full-color clears. When the next
render pass matches EVERY queued texture exactly, its Load becomes a Clear with the queued
color; an existing Clear keeps its own value. This removes the intermediate clear-only pass
and its stores, and replaces matching next-pass Loads. The next drawing pass still Stores.
Read-only depth testing still needs initialized depth; a DontCare store would not promise
preservation for following passes, so disabling depth writes alone is not a discard proof.
No shader math, sampling, render coverage, draw command, depth/stencil state or GPU resource
is changed. There is no new texture/buffer/history image/cache entry or budget adjustment.
Only a CPU pass-descriptor copy and bounded scalar state are added while a pass is pending.

Matching requires standalone private non-aliasable 2D, single-sample, single-level, single-layer
textures admitted by the existing clear queue. Parent, heap, IOSurface and buffer-backed
textures never enter that queue. Subresource, resolve, duplicate-target, smaller MRT,
explicitly reduced render extent, layered pass, unmatched target and unsupported load actions
fall back. No render tail may intervene. A single unmatched clear preserves the entire batch.

Held passes complicate ordering: a later hoisted compute can read the queued colors before
the pass's load actions. On the FIRST actual hoisted dispatch the scheduler restores the
original pass load/clear actions and flushes the original independent clear batch before
compute. Both sides retain fence ordering. BeginHoist without an actual dispatch needs no
fallback. Other encoder changes, transfer/native readers and submission release the held pass
under the existing rules. Finalized descriptors enter replay only after the fold is committed;
no replay-thread descriptor mutation or resource-lifetime inference is introduced.

## Validation

Six focused actual-Metal cases cover OFF/ON in held, direct and no-replay modes, with exact
RGBA pixels across all texels, repeated targets, MRTs, original clears, immutable caller
descriptors, hoisted read/write visibility, partial extent, smaller MRT, unmatched texture,
render-tail and intervening blit fallbacks. Twelve all-nine-flags combination groups pass with
Metal API validation (runtime, scene targets/velocity, BDA and scheduler across all modes).
The initial fixture released its temporary next-pass texture too soon even with the flag OFF;
the fixture now retains its textures to GPU completion. This was not accepted as a renderer pass.

The 27-patch series reconstructs and re-applies idempotently. Benchmark/analysis checks cover
the existing safety policy and reject malformed clear-lifetime metadata. These are correctness
checks, not a whole-game visual equivalence or FPS guarantee.

Three warmed candidate captures each finalize one folded clear target and zero hoist
fallbacks. Their total physical encoder counts vary more widely than the reference captures;
no aggregate encoder-count reduction or performance benefit is inferred from those frames.
The bounded metadata captures copy no texture/buffer payload and are never scored as FPS.

## Automatic gameplay comparison

The suite uses one signed private app and toggles ONLY `BB_MTL_FOLD_CLEARS` in OFF/ON/ON/OFF
order. The previous nine optimizations remain ON. Each run warms for 45 seconds, requires the
existing compile-free quiet window, and scores 45 seconds with minimal telemetry and real
muted CoreAudio timing. Detailed stage timers and frame capture are OFF. Current warning-
tolerant, critical/sustained-swap/session guards remain unchanged. Original saves/settings
are hash-checked and only owned test processes are stopped.

```
python3 tools/macos/benchmark_candidates.py --workspace /path/to/frozen-cohort \
  --app '/path/to/private/Bloodborne Optimized.app' \
  --name attachment-lifecycle-abba --candidate BB_MTL_FOLD_CLEARS
```

The completed results and rejection decision are below. No new gameplay speedup or memory
non-regression is claimed. Private evidence lives in
`outputs/metal-attachment-lifecycle-*` and the benchmark workspace; game assets, shader text,
saves and traces are excluded from published source.

## First ABBA

| Order | Fold | Mean FPS | 1% low FPS | p95 ms | Median process MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| A1 | OFF | 48.5887 | 38.9312 | 22.17 | 5497.01 |
| B1 | ON | 48.8690 | 38.3409 | 22.02 | 5489.83 |
| B2 | ON | 47.7616 | 31.5421 | 22.62 | 5560.24 |
| A2 | OFF | 47.0220 | 34.3401 | 23.14 | 5618.77 |

Mean FPS is 47.8053 -> 48.3153 (+1.07%), while mean 1% low is 36.6356 -> 34.9415
(-4.62%). Mean per-run median process footprint is 5557.89 -> 5525.03 MiB (-32.86 MiB);
owned Metal capacity is 1284.11 -> 1289.70 MiB (+5.60 MiB, a separate ledger).
All scored samples reported warning pressure. B2's maximum swap increase was 17.56 MiB,
with zero score-window growth; other runs had no positive peak growth. The two OFF runs
alone differ by 3.22% FPS and 121.76 MiB footprint. No stable FPS or zero-memory-regression
claim follows from a 1% difference inside this variation. A second same-binary BAAB sequence
is run before deciding the normal default. Its order changes only the selected flag:
`--candidate BB_MTL_FOLD_CLEARS --order BAAB`.

The first diagnostic startup stopped at 9.2 seconds on actual critical pressure before scene
entry; no FPS was accepted. After the owner exited Safari, the retry completed using the
same guard policy. That failed attempt is retained as a failure, not retroactively scored.

## Reverse BAAB and final decision

| Order | Fold | Mean FPS | 1% low FPS | p95 ms | Median process MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| B1 | ON | 47.4547 | 37.0152 | 22.77 | 5599.08 |
| A1 | OFF | 48.9697 | 39.0819 | 21.90 | 5495.78 |
| A2 | OFF | 47.7311 | 35.4433 | 22.64 | 5594.45 |
| B2 | ON | 46.9812 | 31.6032 | 23.31 | 5616.40 |

Reverse-order mean FPS is 48.3504 -> 47.2180 (-2.34%), reversing the first suite's direction.
Pooling four OFF and four ON runs gives:

| Metric | OFF | ON |
| --- | ---: | ---: |
| Mean of run-average FPS | 48.0779 | 47.7666 |
| Mean 1% low FPS | 36.9491 | 34.6254 |
| Mean p95 frame time, ms | 22.4625 | 22.6800 |
| Mean of process-footprint medians, MiB | 5551.50 | 5566.39 |
| Mean of owned-Metal capacity medians, MiB | 1283.70 | 1321.61 |
| Scored warning-pressure fraction | 76.67% | 100% |

Observed pooled FPS is -0.65%, 1% low -6.29%, p95 +0.97%, and process median +14.88 MiB.
Owned capacity is a distinct ledger (+37.90 MiB) and must not be added to process footprint.
Changing pressure/retirement phases and baseline variability prevent a precise causal
regression claim, but they also prevent treating the removed pass as an established gain.
Every score has zero shader compiles, identical camera, renderer/probe/cohort identities,
and draw means between 1188.84 and 1189.53 (<0.06% spread). Detailed GPU timers and captures
were OFF. No scored run hit a guard or logged a GPU fault. No score window had positive
swap growth; the largest startup/run peak was 39.94 MiB. All eight cleanups confirmed original
saves unchanged, no launcher-preference writes, and only owned processes stopped.

**Initial measurement-based recommendation:** keep the candidate OFF because there is no
repeatable isolated benefit. **Subsequent owner authorization supersedes that recommendation:**
0.2.22 defaults it ON together with the previous nine flags, preserving explicit zero overrides
and all correctness fallbacks. Cache limits, quality switches and launcher controls are unchanged.
This activation is the owner's tradeoff decision, not a newly proven speedup. No >=60 FPS result
or whole-game memory saving is claimed.

The measured private renderer SHA-256 is
`7bfec87167cc7ac624ef2614712ac234820f349d83cbad92f0cbbbdc9039fa99`; both suites used it for
all comparisons. Follow-up changes affect tests/reporting only, not renderer instructions.
The blend-equivalence fixture compares separate clear/store/load with folded clear plus
actual alpha blending for RGBA8, sRGB RGBA8, RGBA16F, RG11B10F and RGB10A2: four clear
values per format (including fractional values near rounding boundaries), 20 image pairs,
all exact output bytes. Together with the six scheduling cases and twelve compatibility
groups, 19 native groups pass. This bounded precision check does not claim arbitrary shader
or scene replay equivalence.

Local complete aggregate: `outputs/benchmark-m4-attachment-audit/attachment-lifecycle-summary.json`.
Both raw interleaved suites remain separate. The benchmark tool now accepts explicit selected
flags and ABBA/BAAB order while retaining the historical nine-flag default and all guards.

## Audio / new-scene follow-up

The owner reports short new-scene hitches and occasional audible crackle. The audio runtime
has not been changed in 0.2.22. Both active ports open as 48 kHz / 8-channel float / 256 frames
(5.33 ms per guest output block). The existing queue controller targets two blocks and can
prefill silence; guest thread priorities are recorded rather than mapped to a PS4 scheduler.
Neither that source observation nor the static scene test identifies the actual crackle cause.

A separate muted real-CoreAudio diagnostic enables existing BB_AUDIO_STATS, keeps the owner
quality/profile, warms the private scene, then records one bounded metadata frame. It is NOT
an FPS score. Fourteen five-second main-port windows observed one initially empty queue,
no subsequent empty SDL queue, and maximum feed gap 11.50 ms. Later minimum queued data was
at least 16 KiB. No PCM was dumped, no audio device/app volume was changed, and original
saves/preferences were preserved. Queue counts do not measure audible output, stale game
mixing slots, clipping, or downstream CoreAudio/FineTune behavior; an empty-queue count of
zero cannot rule those out. The fixed scene did not reproduce a transition into a new area.
No claim is made that either symptom is fixed or that shader compilation is its confirmed cause.
The existing audio stats flag is now accepted only for diagnostic benchmark captures, never
for scored FPS comparisons; normal audio behavior and queue capacity remain unchanged.
