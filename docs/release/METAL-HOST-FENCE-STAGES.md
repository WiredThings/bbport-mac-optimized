# GPU fence and source-access investigation — 2026-10-21

Status: **diagnostics retained; host-fragment-wait prototype withdrawn**. No new production
performance setting is enabled. The installed application, normal saves/settings/cache and
all rendering/effect selections are unchanged. No FPS gain is established by the preliminary
fence trials.

## What the captures establish

A warmed native 1920x1080, MetalFX OFF capture initially contains 192 actual fence waits:
146 compute, 44 render and two blit. These are counts, not durations or proof that compute
execution is the dominant cost. Guest shaders may use device addresses in vertex work;
`guest=false` also includes velocity replay and cannot prove vertex independence.

A second bounded capture with source provenance has 64 Runtime buffer-access epochs. In 35
of them the same resource-ID, offset and **192-byte range** is written by compute, alongside
graphics reads. The original graphics source flag is **AllGraphics**, not a distinction
between vertex and fragment. `RasterizerCore` itself chooses AllGraphics when forwarding
non-compute bound-buffer accesses. Runtime then reduces synchronization requests to its
`memory_pending` Boolean. Narrowing these barriers needs more precise provenance, not an
assumption that every read is a fragment texture read.

Two source epochs contain unknown-range global device-address accesses; seven exceed the
eight-range diagnostic limit and explicitly report overflow. Repeated writes are not proven
redundant: values may differ, readers may intervene, aliases are not unified and incomplete
range lists cannot demonstrate absence of a reader. The records contain metadata only,
not guest data, textures or raw shader bodies. They are not a complete dependency graph.

## Retained source changes

0045 records actual fence waits in the existing bounded native frame capture. Render waits
remain before vertex, compute/blit waits and all fence updates remain unchanged. It also
links QuartzCore in the standalone scheduler test: 0042's submission timestamps use
CACurrentMediaTime, so a fresh relink needs that framework.

0046 adds source-access provenance to existing Runtime epochs only when native capture is
requested. One fixed CPU struct retains at most eight integer resource-ID, original
range, stage and access records, plus aggregate read/write stage masks, global unknown
counts and overflow. It retains no resource ownership or payload and allocates no GPU
object. Normal launches do not allocate this diagnostic struct. Existing FlushBarriers
resets it with the original access tracker. It never changes synchronization decisions.

The source flags describe caller-reported logical accesses, not hardware cycle counters.
Texture transitions and scheduler-internal requests are outside the Runtime source epoch.
Unknown ranges, physical aliases and subsequent consumers still require explicit handling.

## Withdrawn experiment and excluded timing

An explicit fullscreen-host API was prototyped to move eligible fence waits from vertex to
fragment. Source-reviewed shaders had vertex_id / CPU-copied setVertexBytes inputs only.
Untracked-resource pixel tests passed, but source review then required a conservative full
handoff to a subsequent vertex/compute/blit consumer. The first ABBA (62.186 vs 63.043 FPS)
and incomplete reverse order are excluded because they preceded that handoff fix.

The revised diagnostic application recorded **no actual eligible fragment-stage wait** in
this native-resolution run, including the diagnostic counter covering work outside the guest
capture window. A classified host encoder without an outstanding wait cannot save work by
moving that wait. The apparent preliminary difference is therefore not evidence of a GPU
optimization. Scoring was stopped through the benchmark's own SIGINT cleanup. The prototype,
its flag/API/call-site changes and temporary tests were removed from retained source. The
raw trial logs remain private as exclusion evidence; no launcher option is added.

Apple's [stage-specific fence API](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/waitforfence(_:before:))
permits independent vertex work to proceed while fragment waits. It does not prove eligibility
or benefit in this workload. All production render waits keep their original stage.

## Next measured candidate

The repeated 192-byte updates use the existing optional inline-compute path. Compare that
path with its existing staged/blit fallback under identical frozen save/camera/settings,
then inspect memory as well as frame times. This changes no user preference and does not
assume the writes can be deleted or combined. Diagnostic capture and scored windows remain
separate. No source compile or GPU test runs during scored sampling.

## Accepted same-binary comparison of the existing write path

The final retained renderer is `d9f64939d53db76c66a3b87e33f6417630ac78f2629b380c43805286607aecd6`.
It has no host-fragment API/flag. The same old frozen save/camera cohort is used, **not a new
Central Yharnam measurement**. Native 1080p, MetalFX OFF, unchanged effects; only the existing
BB_MTL_INLINE_UPDATES flag differs. Fragment uniforms stay ON and native clear batching OFF.
Every score uses 45 seconds warmup plus the existing quiet window and 45 seconds measurement,
without native capture, stage counters, submission capture, CPU timing or concurrent compilation.

| Order | Path | FPS | 1% low | Median process footprint MiB |
| --- | --- | ---: | ---: | ---: |
| A1 | staged/blit | 61.801 | 47.08 | 5406.2 |
| B1 | inline compute | 61.240 | 41.07 | 5561.0 |
| B2 | inline compute | 60.901 | 48.58 | 5559.5 |
| A2 | staged/blit | 61.904 | 40.59 | 5409.6 |

Staged/blit mean **61.853 FPS**, inline-compute mean **61.070 FPS**
(-1.265% for inline). Mean 1% lows are 43.83 vs 44.83;
mean p95 is 17.430 vs 17.455 ms. This small timing difference is not an
established optimization gain. All four runs have matching scene/draw validation and no
scored swap increase. Cleanup verifies normal save hashes unchanged and no launcher preferences
written. The game's own shader/PSO creation is checked separately in the raw score metrics.

Mean per-run median footprint is 5407.9 vs
5560.3 MiB (+152.4 MiB inline).
The first pair's category snapshots differ mainly in staging (+75 MiB) and render targets
(about +69.4 MiB), rather than normal buffer capacity. Resource populations and retirement
histories differ; these snapshots do not establish that a 192-byte dispatch directly allocates
that much memory or that resources leak. They warrant follow-up, not smaller caches or
shorter lifetimes. No production preference or write-path default is changed from this suite.

The staged diagnostic confirms 35 **192-byte** buffer copies in one frame, replacing the
observed small compute-write path. Thus this comparison exercises real work; it does not
assume a lower encoder count is faster. A safer next step is to preserve per-buffer shader
stage provenance before AllGraphics aggregation, and correlate exact write generations with
all subsequent readers/aliases before considering a scoped dependency or overwrite optimization.

## Final verification and rejected build

Final retained source passes 11 Metal invocations: the bounded source test, nine existing
runtime/scheduler/scene/BDA/motion/texture/lifetime/descriptor regressions, and an independent
staged-inline fallback test, under Metal API validation. 79 Python checks pass. All 46 adapters
reconstruct and reapply; diff whitespace checks pass. Installed renderer hash remains
`f408e615d804194fdd97124a2a2d02ea4a3dc1812cacc6ec3f01312d68ef4ce7`.
No isolated game remains running; final system pressure is normal. chronod was small and was
not restarted. Cache sizes, retirement rules, two-level BDA, velocity, occlusion and format /
dimensional validation are unchanged.

One rebuild after prototype removal initially reused objects because restored files retained
old timestamps. The standalone scheduler relink exposed the removed API, and a runtime test
failed before fresh verification. Those results were rejected. Touching the restored headers /
sources rebuilt their dependants; all final checks above were rerun and passed before the
final private renderer was staged. The stale library was never installed or used for scoring.
