# Metal layout and compilation follow-up, 2026-10-23

## Patch interface correctness

The actual failing triangle pipelines wrote fourteen `float4` patch outputs (224 bytes).
Their evaluation shaders read locations 4–13, which SPIRV-Cross compacted to 160 bytes:
location 4 moved from byte 64 to byte 0. Padding the tail or removing the stride check
would leave wrong addressing. Merely declaring unused SPIR-V inputs also fails because
inactive inputs are removed.

Patch `0086` supplies every producer patch output through SPIRV-Cross's tessellation-input
interface API. It validates complete producer reflection, active input types, generated
field offsets and the stride. A consumer that reads no patch constants can omit that
buffer. Scratch allocation remains based on the existing producer stride. Per-variant
keys include the linked layout; existing SPIR-V, MSL and pipeline archive files are retained.
There is no global cache clear or smaller cache budget.

The synthetic GPU test reads pixels from two distinct patches. Leading, middle and trailing
unused fields with 224/256-byte producer layouts each pass cold and after cache reload:
12 cases, 1,272 interior pixels per case, no wrong or stray pixels. Invalid offsets, types
and missing fields are rejected; the old compacted 160-byte pipeline is still rejected.
All 21 real tessellation keys (58 compiled stage variants) in the privately copied current
corpus build in both passes; the second pass stores no new MSL. Game shader payloads are
not part of the repository or source archive.

## Native critical-path evidence

Separate instrumented runs at the fixed Central Yharnam camera observed about 19.9 ms
between frames, 6.44 ms CPU recording and 0.65 ms replay waiting. The Rosetta frontend's
own report shows about 18.2 ms waiting for the previous frame. Its wait counters and
`guest_age_ms` are not automatically shared into the ARM process's old CSV columns;
zero in those columns is not evidence of no guest waiting.

A bounded 2,048-command-buffer trace gives a draw-frame interval union of about 19.91 ms,
with about 16.36 ms enqueue-to-GPU delay per draw command buffer. This includes queue
ordering/dependencies and is not hardware utilization or pure shader execution time.
The sampled stage estimate ranks guest rendering highest, about 16.3 ms/frame, followed
by compute about 2.35, host rendering about 0.80, blit about 0.76 and occlusion about 0.11.
Those spans can overlap and have incomplete render-stage pairs; do not add them or infer
a precise ALU-versus-bandwidth split. Main drawing remains the useful GPU investigation
priority; faster CPU translation alone does not imply another proportional FPS gain.

## Library experiment and binding ownership

The private current shader corpus contains 607 SPIR-V files but 500 distinct contents.
The observed slow-compile log has 253 distinct shader stage/hash/permutation events and
207 distinct pipeline events; nested warnings are not 460 independent repeated compiles.
A source-cache hit still calls the Metal source-library API.

The first broader prototype shared library and function handles. Eight interleaved runs
had no established average-FPS gain and higher average accounted process memory. Extra
capacity was in staging/render targets; that does not prove the weak index caused it.
This prototype was not adopted. Its warm diagnostic reduced 239 library builds to 204
and the shader span from about 650 to 604 ms, too small to claim meaningful scene-entry
improvement. Those measurements are not results for the final scoped implementation.

The final experiment shares only weak library references. Each module still requests its
function and owns its binding/variant metadata. Metal itself may return the same function
for repeated requests, so patch `0087` also keys render, compute, tessellation and geometry
PSOs by a monotonic per-module identity. IDs are in-memory only; persistent archive keys
are unchanged. A synthetic pair with binding 0 versus binding 1 generates identical MSL;
the former key returns the wrong module's metadata, while the protected key passes.

The index hashes complete post-rewrite MSL, the entry and actual math/language options.
Its 1,024 metadata slots hold no strong GPU references; full or expired entries retain
ordinary compilation behavior, and failures/exceptions can retry. This is separate from
texture/PSO cache capacities and does not ship offline `.metallib` files. The launcher
exposes `BB_MTL_LIBRARY_REUSE` as a default-OFF experiment; saved choices are preserved.

## Runtime validation

The initial layout-only ABBA averaged 49.55 FPS before / 49.25 after, with comparable low
frames and about 23 MiB lower mean accounted process medians. No sustained FPS gain is
claimed. Source, scene, settings, quiet warmup, capture mode and memory guards are recorded
for the final candidate comparisons. Accounted process sums are not unique physical RAM.

The final-source ABBA (new library experiment OFF, existing selected flags retained) measured:

| Order | Version | FPS | 1% low | Accounted process median MiB |
| --- | --- | ---: | ---: | ---: |
| A1 | Installed 0.2.50 | 47.867 | 38.703 | 5604.95 |
| B1 | Final candidate | 49.223 | 42.573 | 5626.57 |
| B2 | Final candidate | 49.905 | 43.842 | 5478.01 |
| A2 | Installed 0.2.50 | 49.497 | 38.743 | 5784.77 |

Means are 48.682 / 49.564 FPS and 5694.86 / 5552.29 MiB accounted usage. All scoring
windows had no shader compilation, no positive swap growth and the same warning pressure
level. The short comparison does not prove a repeatable 1.8% gain or establish 60 FPS.

With the scoped library experiment ON, ARM F3 request/auto-continue, six Chinese replacement
files, owned-child reaping and two consecutive SetHp respawns pass. The x86 in-process
fallback also boots and completes a bounded 2,048-command-buffer trace with it ON. The
experimental checkbox starts OFF, persists an ON choice across a QA relaunch, and was
restored OFF before quitting. 91 launcher tests (one skipped), 85 benchmark-tool tests,
13 native/probe packaging tests and six online-routing tests pass. Fresh live online,
physical F3 and enemy-combat execution are not claimed.

[Machine-readable aggregate](METAL-051-RESULTS.json). All gameplay uses private save/cache
copies; normal progress and preferences are not a seed-selection choice or a test target.
All 11,671 protected formal files match the pre-work hashes. Severe pressure, six-second
rapid-swap, capacity and lock/sleep guards remain active. No visual capture runs during FPS
scoring. No game shaders, screenshots, credentials, saves or development drivers are shipped.
