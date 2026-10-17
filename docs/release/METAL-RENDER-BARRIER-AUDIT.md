# Render-only duplicate barrier experiment

Status: `BB_MTL_COALESCE_BARRIERS=1` is experimental, OFF by default. No accepted gameplay
FPS, memory non-regression or 60 FPS result is claimed. The installed 0.2.18 app is unchanged.

## Exact scope

Within one physical guest render encoder only, the optional RenderList filter retains the
first buffer/texture memory barrier since a draw and every first barrier after a draw. It may
skip subsequent identical barriers separated only by bindings, fixed-function state or labels.
Every draw API, including indexed, indirect, patch and zero-count draws, invalidates the flag.
BeginDirect and Replay reset it for the new physical encoder. Recorded HasBarrier and initial
fence waits remain unchanged. Guest direct-mode barriers use the same list as their draws;
unknown/host raw encoders, hoisted compute barriers and all outstanding-fence waits keep their
original behavior. No barriers move across render/compute/blit encoders, load/store actions,
shader math, sampling, resource retirement, cache keys or cache budgets.

No GPU resource, alternate PSO or cache entry is allocated. The list adds bounded CPU scalar
state and counters. [Apple's render barrier API](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/memorybarrier%28scope%3Aafter%3Abefore%3A%29)
describes ordering between earlier and subsequent draws. This experiment does not relax any
synchronization between draws.

## Evidence and decision

The first private Central Yharnam frame reported 11 recorded-list barriers, zero dropped. A
second frame, including direct guest encoders and contextual request metadata, reported 10
render-list barriers, also zero dropped. The second frame's 175 scheduler requests were:

| Path | Requests |
| --- | ---: |
| No open encoder: establish a wait for the next encoder | 53 |
| Compute encoder | 14 |
| Hoisted compute while a render pass is held | 56 |
| Held render-list barrier | 10 |
| Blit encoder: end it and establish a wait | 42 |

These are requests, not GPU durations. Metadata adjacency hides actual dispatches, draws and
copies; it is not proof of redundant work. No-open/blit requests already collapse to scheduler
wait state or encoder ordering. The evidence does not identify redundant main-fragment
barriers and does not justify enabling this experiment or removing compute dependencies.
Both diagnostic runs stopped their own process and verified original save hashes unchanged.
System pressure reached warning, so neither is an accepted FPS or memory comparison.

The analyzer now distinguishes request paths from RenderList barriers actually filtered;
older captures report unavailable counters as null. A hardware-counter inventory on this M4
exposes only the timestamp set, without public per-stage utilization/statistics counters.
No fragment-cycle, attachment-write-cycle or texture-cache-miss measurement is inferred.

## Validation

15 serial native groups pass: flag off/on, all five draw families, direct/recorded/moved lists,
new-encoder reset, real vertex and fragment atomic producer/consumer dependencies with exact
pixels, plus scheduler/BDA/scene/runtime combinations. Existing velocity, occlusion, two-level
page tables and memory/transfer candidates are exercised together. Direct scheduler mode uses
its dedicated test entry; the legacy full scheduler fixture explicitly requires held passes
and is invalid with BB_MTL_HOLD=0. The invalid invocation log is retained separately.
There are also 33 benchmark checks and 18 Metal report/packaging/patch checks. The incremental
23-patch series reconstructs and reapplies idempotently. These checks are correctness evidence,
not a performance gain. Traces, shaders, caches and saves remain private.
