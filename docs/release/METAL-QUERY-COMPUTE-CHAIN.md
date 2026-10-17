# Ordered query compute chain

Status: `BB_MTL_QUERY_COMPUTE_CHAIN=1` is experimental and **OFF by default**. The normal
installed 0.2.18 app is unchanged. There is no accepted new gameplay FPS, slow-frame or memory
non-regression result, and no 60 FPS claim. A strict scored baseline was stopped during warmup
when system warning pressure persisted; its original saves/preferences were preserved.

## Work removed, dependencies retained

The original occlusion path ends its encoder before and after every snapshot. A small zero
fill between the end of one interval and the beginning of the next opens a blit encoder.
The candidate joins held/replayed render producers and flushes the velocity tail before
reading query counts/scales. It orders snapshots on an existing serial compute encoder with
buffer/texture barriers. After each snapshot it publishes writes and retains a flag requiring
the next **new physical encoder** to wait for this compute encoder's fence. Reusing the current
compute encoder does not consume that future wait.

Uniform-byte fills may use the existing FillU32 kernel only when an unheld compute encoder is
already open, the range is aligned to four bytes and no larger than 256 bytes. Other fills
retain the original path. Every original byte is still written, including both query validity
fields. Counter resets, scale conversion, total/history updates, max-of-three results,
collision/overflow fallbacks, GPU submission/tick retirement and CPU completion waits remain.
No query shader arithmetic, resolution, effect, cache budget, texture or staging-buffer
capacity changes. No extra GPU texture/buffer is introduced; the existing FillU32 PSO can be
compiled on its first new use. No guest memory is cleared by the CPU ahead of GPU work.

[Apple documents compute barriers](https://developer.apple.com/documentation/metal/mtlcomputecommandencoder/)
as ordering writes and reads within a compute pass; cross-encoder dependencies still use the
existing fences. This experiment does not assume automatic ordering of aliased guest buffers.

## Isolated timing and actual path coverage

An interleaved ABBA test uses 23 synthetic query intervals, 16x16 depth-tested triangles,
46 snapshots and 23 zero fills per batch. Each process excludes eight fixed warmup batches
and retains all 20 measured batches; raw CPU/GPU samples and exact result assertions are local.
All query pairs have valid fields and the expected numerical result, not just monotonic values.

| Order | Query path | Median GPU ms per synthetic batch |
| --- | --- | ---: |
| A1 | Original | 3.083958 |
| B1 | Chained | 2.022833 |
| B2 | Chained | 1.903875 |
| A2 | Original | 3.364959 |

The mean of the two per-run medians is 3.224459 / 1.963354 ms, about 39% lower. This is an
isolated encoder/query workload, **not gameplay frame time or FPS**. CPU-recording medians
show substantial drift, so no CPU percentage gain is claimed.

Three original and three candidate Central Yharnam metadata frames used one renderer binary
(SHA-256 `830e6a4fc75011c6eeb64d67598957ef0bd5fbab169364a28c4e88209994e3e1`),
the same frozen direct-entry save/settings and camera. Query work varies between frames:

| Path / frame | Snapshot dispatches | Compute encoders containing snapshots | FillBuffer encoders |
| --- | ---: | ---: | ---: |
| Original 3075 | 44 | 44 | 22 |
| Original 3080 | 8 | 8 | 4 |
| Original 3085 | 48 | 48 | 24 |
| Candidate 3192 | 0 | 0 | 0 |
| Candidate 3197 | 50 | 26 | 1 |
| Candidate 3203 | 60 | 31 | 1 |

Thus the candidate actually puts two adjacent snapshots and their intervening fill into one
compute encoder. Different frames have different query/visibility work; total encoder counts
cannot be compared as an FPS gain. The main G-buffer workload remained 551 draws / 8,460,948
indices in the compared semantic frames. A bounded final display showed scene/character/HUD
intact; animation differed and it is not a synchronized image-equivalence proof.

Both short series reported 352 MiB / 19 staging objects, but reached system warning pressure.
That is a limited capacity observation, not an accepted footprint, paging or slow-frame gate.
The formal original-path run hit the unchanged warning guard before scoring. All original
save hashes and launcher preferences were preserved, and no logged GPU fault was reported.

## Capture boundary correction

Query workloads can be absent in one frame. Native diagnostics can optionally collect at most
three metadata frames with `--capture-native --capture-count 3`; display readback remains
single-frame only. Each member keeps the existing 8192-record / 2 MiB bound. Unique names
include the guest frame number, so members in the same second do not overwrite one another.

A first series exposed a diagnostic bug: EndRendering drained render/replay work but left a
compute encoder open across the frame boundary. Those records began with a close lacking an
open; that series was explicitly invalidated and retained locally. A single-renderer boundary
drain hook now closes all encoder types and tail work before changing armed/recording state.
It is registered before recording and cleared after recording threads stop. It allocates no
callback storage and runs only during native capture. Normal Play does not call it.
The harness uses the strict attachment analyzer before marking **every** capture member valid;
truncated, unbalanced or attachment-free records fail. Capture results are never FPS results.

## Correctness coverage

Focused flag-off/on checks cover exact fill bytes and surrounding canaries, existing-encoder
reuse, the size/alignment fallback, subsequent GPU blit readers, and a real fragment shader
that consumes all four query pairs before any CPU/GPU wait. Its exact green pixels verify
compute-to-render visibility. Held/direct/no-replay modes, query history, collisions, overflow
and retirement are exercised. Combination tests retain velocity/depth seeds, blur bypass,
two-level BDA, occlusion, idle backing and prior transfer candidates.

Private raw evidence stays under `outputs/metal-query-chain-tests`,
`outputs/metal-query-chain-combination-tests` and `outputs/benchmark-m4-attachment-audit`.
Game data, shader text, caches, traces and saves are not published with this report.

Validation: all 71 existing native groups, 8 focused query groups and 15 combined native
groups passed, including Metal API validation. The metadata unit and 35 benchmark plus
19 Metal report/packaging Python tests passed; all 24 serialized patches apply idempotently.

## Follow-up combined run and memory audit

A 45-second-warmup / 45-second-score attempt enabled query chaining, precise residency,
direct copy, direct texture tiling and adaptive staging together. It stopped at 58.9 seconds
under the unchanged sustained-warning guard, before scoring. The last recorded process
footprint was 5,785,631,200 bytes and owned Metal allocation capacity 1,334,151,824 bytes.
The preceding reference ended near 5,975,611,824 / 1,490,873,488 bytes, but startup trajectories
were unmatched: this is **not a controlled memory-saving result**. Original save hashes and
launcher preferences remained unchanged. No candidate is enabled in the normal installed app.

A focused ARC/O3 x86_64 probe created 2000 Metal render-pass descriptors and observed weak
references to both descriptor and attachment after each local scope. Both were already nil
without a per-call autorelease pool. Main draw recording, replay jobs, texture creation and
presentation also have existing scoped pools. Therefore the presence of a convenience factory
outside a local pool does not establish a leak; no speculative pool change was made.
Shader translation already releases previous IR objects while preserving arena capacity.
These checks do not exclude all transient driver allocations or all possible lifetime bugs.

After the owned process exited, system pressure returned to normal. Remaining swap usage was
about 8.37 GiB; historical swap alone does not establish ongoing pressure. The same gameplay
workload repeatedly reaches warning pressure, preventing a qualifying FPS/slow-frame result
under this benchmark's strict gate. No other apps were terminated and no memory guard, cache
capacity, resolution or effects were relaxed to obtain a score.
