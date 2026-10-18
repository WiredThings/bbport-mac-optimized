# Small GPU writes, pipeline persistence and fragment audit

Release configuration: 0.2.23 (0223), validated with the original no-binary-archive budget.
Only startup compilation-object release is enabled; persistence and inline writes stay opt-in. Audio code and queue sizes are unchanged.
Resolution, effects, shader math, filtering, cache capacities and the ten accepted defaults stay fixed.

## What the remaining copies actually contain

Patch 0028 adds bounded native metadata for each buffer copy: existing native resource identity,
offsets, byte length, allocation bounds, labels and whether dependency handling requested a flush.
It uses the existing 8192-record / 2 MiB single-frame capture limit and copies no payloads.
The reader rejects invalid ranges or copy records outside a blit encoder. Diagnostic compilation
timing is opt-in (`BB_MTL_COMPILE_AUDIT=1`), bounded and barred from scored benchmarks.
Normal Play clears that diagnostic control.

The first current Central Yharnam reference frame has 43 `Runtime::CopyBuffer` regions totaling 7,316 bytes:
38 staging-to-guest writes of 192 bytes, plus five four-byte GDS/guest synchronization operations.
The largest region is only 192 bytes, and no exact source/destination/range tuple repeats.
These calls do not carry a large-buffer bandwidth payload. This count does not cover texture
transfers, CPU uploads, direct native copies or implicit driver transfers. Most 192-byte writes overwrite the same guest
parameter range between post-processing passes. Every pass needs its own values; overwriting
the same destination does not make those writes redundant. No write has been deleted.

## Optional ordered inline GPU writes

Patch 0029 adds an OFF-by-default `BB_MTL_INLINE_UPDATES=1` experiment. At most 256 bytes,
word-multiple length and a 16-byte-aligned in-bounds destination can be written with one direct
bound compute kernel and Metal's immediate `setBytes` snapshot. The write remains in GPU stream
order and keeps the backend's read/write dependency handling. There is no CPU write into live
guest backing, no BDA indirection or resource-residency restriction inferred from guest addresses.
Other sizes/alignments and failed kernel creation keep the original staging/blit path.

This replaces staging transfers; it does not claim to eliminate GPU work or encoder switches.
The experiment lazily owns one small compute pipeline and uses the driver's bounded inline-data
mechanism. It adds no texture, persistent data buffer, history surface or cache budget; the real
process/Metal footprint still needs measurement rather than a zero-allocation claim.

Six OFF/ON held/direct/no-replay cases compare every byte of GPU-consumed snapshots, surrounding
canaries, later overlapping writes, caller payload mutation after submission, empty updates and
unaligned/oversized fallback. Full runtime and pipeline-core checks pass. Twelve further real
Metal/API-validation groups combine the candidate with the ten defaults, velocity/depth scene
targets, two-level BDA and scheduler/occlusion behavior across those three modes.

Three warmed candidate captures prove the 192-byte blits disappear. Residual copy counts are
10/5/5 regions (40/20/20 bytes). Total physical encoders are 280/278/292; this is higher than the
earlier reference range. A smaller copy count is therefore not a performance acceptance test.

## Persist the existing binary pipeline archive

Patch 0030 provides the existing `PsoCache::Flush()` after `WarmUp()` and before gameplay.
After the budget results below, patch 0032 gates that call with opt-in
`BB_MTL_PERSIST_WARM_ARCHIVE=1`; normal launch defaults it OFF.
Previously the archive was serialized only by the PSO cache destructor; process termination
could leave all prewarmed entries absent from disk. The private benchmark cache had no binary
archive and the first audit observed zero binary-archive hits. After the change the actual private
cache contains a 19,604,656-byte archive. A separate subsequent startup confirms 688 of 690
PSO creations hit that archive, with zero creation failures; their summed API time is 0.099 s.
The initial 0.323 s and later 0.099 s are startup observations with different driver-cache warmth,
not a controlled FPS comparison or a claim that all startup time improved by that fraction.
The renderer neither evicts entries nor changes keys, shader profiles, source caches or capacities.
No serialization is added to the frame loop; existing failure handling logs and continues.

The first reference startup had 672 MSL text-cache hits but about 10.39 s summed shader API work,
including about 10.21 s nested library work; 690 PSO creations totaled about 0.323 s. A later
startup's library time was much shorter even before it could benefit from the newly saved
archive. That difference cannot be attributed to this fix: Metal/OS cache warmth differs.
Library time is nested inside shader time and must not be added to it. Source text hits are
not driver cache-hit evidence. Startup timing is not sustained gameplay FPS.

Metal intentionally reports no dynamic vertex-input capability, so the common asynchronous
pipeline path does not run here. That path can omit geometry until compilation completes and
is not enabled as a workaround. Unknown new-area shaders still require first-use preparation;
the persistence fix does not promise to remove all scene-entry hitches.

## Fragment-stage findings

A read-only scan covers 16 cached fragment variants selected from frame pipeline labels and
the main-pass/earlier-hotspot shader set. No literally repeated sample/read/gather expression
was found. This is a static shortlist, not a GPU duration ranking or proof that the Metal
compiler cannot improve anything. Distinct textures/coordinates, LOD, derivatives, branches
and intervening writes must retain their meaning. No sample, precision or filter is changed.
RenderList already suppresses unchanged texture/sampler/buffer binding commands; those calls
are not repeatedly removed and counted as a new optimization.

## Measurement checkpoint

The private fixed save enters Central Yharnam directly. The current guards, original-state
hash checks, quiet warmup, muted real CoreAudio and lightweight telemetry are preserved.
The only ABBA score toggle is `BB_MTL_INLINE_UPDATES`; compile audit, native capture and GPU
stage timers are OFF in every scored run. The warm-archive persistence fix is present on both
sides. Its archive is retained during gameplay in both sides of this first suite.

| Order | Inline | Mean FPS | 1% low FPS | Process median MiB | Owned Metal median MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| A1 | OFF | 47.5014 | 31.8471 | 6253.42 | 1346.84 |
| B1 | ON | 48.9607 | 38.1863 | 6154.47 | 1215.18 |
| B2 | ON | 47.6368 | 37.3806 | 6288.28 | 1357.02 |
| A2 | OFF | 48.6799 | 34.2370 | 6115.03 | 1200.80 |

OFF/ON means are 48.0906/48.2987 FPS (+0.43%), 33.0421/37.7835 low FPS, and
6184.23/6221.37 MiB process footprint (+37.15 MiB). Owned Metal differs by +12.28 MiB,
a separate accounting ledger. No scored run had positive swap growth. Draw means agree
closely and no score-window shader compilation or GPU fault occurred. The small average-FPS
difference lies inside the between-run variation; the low-frame change is not treated as a
repeatable benefit. Inline writes therefore remain OFF by default despite byte correctness.

## Retained archive memory follow-up

Persisting an archive alone is not accepted as the final fix. All four runs above load it and
have a higher process footprint than the earlier absent-archive captures. A same-renderer
private-cache check holds inline writes OFF and changes only archive existence before boot:

| Archive at boot | FPS | Process median MiB | Owned Metal median MiB | Warning fraction |
| --- | ---: | ---: | ---: | ---: |
| Absent, freshly populated | 49.3814 | 5507.95 | 1217.25 | 0% |
| Existing file loaded | 48.7377 | 6121.09 | 1192.37 | 100% |

The loaded case has about 613 MiB more process footprint while owning about 25 MiB less Metal
resource capacity. These two runs are memory attribution, not a statistically established FPS
effect. The private archive is backed up and restored byte-for-byte; normal user caches and
saves are untouched. This intermediate persistence-only renderer was never installed.

Patch 0031 drops the compilation-only object after serial prewarm (after an optional save),
before worker/selection threads start. All live render, compute, tessellation, geometry and
depth/stencil pipeline maps, functions and libraries remain retained. No active pipeline is
evicted or rebuilt on a repeated lookup. `BB_MTL_RELEASE_WARM_ARCHIVE=0` keeps the object for
comparison. Default release must still pass actual gameplay/memory testing before installation.

The loaded-archive release ABBA (intermediate binary, before persistence became opt-in) gave
47.7863 -> 49.1251 FPS, and 6253.69 -> 5786.52 MiB process medians. This removes about 467 MiB
relative to retaining the loaded archive, but still exceeds the absent-archive reference.
No zero-cost-persistence claim follows. That intermediate variant is not installed.

The normal user's cache was read-only checked: neither binary archive file exists there.
The final configuration must preserve that starting condition. Only the experimental binary
file created by this task in the PRIVATE benchmark cache is retired to a separate evidence
directory. All SPIR-V, MSL and pipeline-key files remain. Final ABBA fixes persistence and inline
writes at zero, toggles only release of the fresh warmup archive object, and verifies that no
binary file is created. Thus the final A condition matches the installed app's real cache use.
No normal cache is deleted, moved or resized.

Tradeoff: a newly encountered pipeline's key, SPIR-V and MSL retain normal persistence. If binary
persistence is explicitly enabled, its PSO joins the saved archive during the next startup
prewarm rather than at the current session's shutdown. Existing prewarmed PSOs hit an already
present disk archive before release. Unknown/new
pipelines still build normally; no draw is skipped. This changes compilation-archive lifetime,
not texture, shader-object or active-PSO cache capacity.

## Final original-budget result and defaults

Same final renderer, 45 s quiet warmup plus 45 s score each, ABBA; ten accepted flags ON,
`BB_MTL_PERSIST_WARM_ARCHIVE=0`, `BB_MTL_INLINE_UPDATES=0`, fragment-uniform experiment OFF.
Only `BB_MTL_RELEASE_WARM_ARCHIVE` changes. Initial/final private cache checks confirm no binary
archive; all SPIR-V/MSL/key caches remain. Original normal-user caches and saves are untouched.

| Order | Release | FPS | 1% low | Process median MiB | Owned Metal median MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| A1 | OFF | 48.3202 | 34.2466 | 5605.27 | 1348.37 |
| B1 | ON | 49.2922 | 40.0063 | 5478.88 | 1233.36 |
| B2 | ON | 49.2060 | 30.4870 | 5460.39 | 1217.13 |
| A2 | OFF | 49.3972 | 41.9425 | 5487.23 | 1240.24 |

Means: 48.8587 -> 49.2491 FPS (+0.80%),
38.0945 -> 35.2466 low FPS, 21.710 -> 21.595 ms p95,
and 5546.25 -> 5469.64 MiB
process footprint (observed -76.61 MiB).
Owned Metal means differ by -69.06 MiB;
this is a separate capacity ledger, not additional memory to sum with footprint or subtract to
infer CPU use. Retirement phases vary by run. No isolated stable FPS or low-frame benefit is
established, nor an exact causal memory saving of the observed magnitude. Every score has
normal pressure, zero positive score-window swap growth, no shader compilation or GPU fault,
matching camera and narrowly matched draw counts. All original-state and cleanup checks pass.

Default release is accepted on resource-lifetime correctness, no additional GPU work/resource,
retained active-PSO identity, and this bounded no-pressure-regression comparison. It runs once
after prewarm, never per frame. The 32-patch series reconstructs idempotently. Native tests prove
GPU output, repeated PSO identity, late creation after archive release, explicit-flush next-start
hits, and no-flush release without producing a binary file. Twelve compatibility groups retain
velocity/depth, occlusion, two-level BDA and existing memory/clear behavior.

Normal Play clears compile/native audit controls. Binary persistence stays OFF because its
loaded form has material CPU-memory overhead even after object release. Inline writes remain
OFF because the extra compute path does not establish a useful average-FPS gain. Audio runtime,
channel mapping, queue capacity, settings and saves are unchanged. >=60 FPS and removal of
new-scene hitches or audible crackle have not been achieved or verified by this work.

Local final report: `outputs/benchmark-m4-attachment-audit/archive-budget-final-abba.json`.
Reproduce in an isolated frozen no-binary-archive cohort with:

```
python3 tools/macos/benchmark_candidates.py --workspace /path/to/private-cohort \
  --app '/path/to/private/Bloodborne Optimized.app' --name archive-budget-abba \
  --candidate BB_MTL_RELEASE_WARM_ARCHIVE \
  --fixed-env BB_MTL_PERSIST_WARM_ARCHIVE=0 --fixed-env BB_MTL_INLINE_UPDATES=0
```
