# Direct compute resources and RGBA16F export

**Later update (0.2.19):** the owner authorized a warning-tolerant guard and combined A/B,
then accepted the observed ~105 MiB footprint increase. The combined launch profile is now
enabled; individual-stage reports below retain their historical status and evidence limits.
See [current guard, results and activation decision](METAL-BENCHMARK-GUARD-V3.md).

Status: experimental, **OFF by default**. The normal installed 0.2.18 app is unchanged.
There is no new accepted gameplay FPS, slow-frame or memory non-regression result yet.
Resolution, effects, filtering, shader precision, cache budgets and memory guards are retained.

## Resource-access scope

`BB_MTL_PRECISE_COMPUTE=1`, together with `BB_MTL_PRECISE_RESIDENCY=1`, allows plain game
compute shaders without the physical-storage-buffer-address capability
to use only their explicit Metal bindings. Both direct and indirect dispatch recording use
the same gate. Unknown stages, tessellation/geometry transforms and physical-address accesses retain
broad guest declarations. A page-table `OpName` alone can describe an unused declaration;
real cold/cache-hit shaders confirmed that this name is also present in direct-only modules. The SPIR-V-derived metadata is retained on
cache hits; changing cache contents or recompiling the game shaders is unnecessary.

This reuses the scheduler's existing union rule: a later indirect dispatch on the same
physical or hoisted encoder adds guest/leaf declarations before running. A mixed encoder is
never downgraded. Barriers, fences, buffer write tracking, page-table mappings and GPU tick
retirement are unchanged. This is **not** byte-range slicing of a bound large imported buffer,
and it does not prove that all imported guest memory can become pageable.

## Copy work removed

`BB_MTL_TEXTURE_TILING_HALF=1`, together with `BB_MTL_TEXTURE_TILING=1`, extends the existing
texture-to-guest tiler to a tightly pitched, single-layer/single-mip, single-sample RGBA16F
macro-tiled image. It reads a `texture2d<half>` and bitcasts the four half values into the
original 64-bit tiler element. It keeps the original PS4 macro-tile address calculation,
bank swizzle and destination bounds. Unsupported formats, dimensions, arrays, mip chains,
row padding, sample counts and generated-shader mismatches retain the old path.

An existing authoritative proxy at an exact 2x ratio can supply the native pixel logically,
avoiding both its full-size resolve and the linear texture-to-buffer copy. No proxy is created
just for this optimization. Its native image remains dirty until a real native reader needs
it; native writes invalidate/refill the proxy under the existing rules. Native exports avoid
the linear staging copy but retain their original data, including NaN payloads.

The existing 32-entry host tiling PSO cap remains; 32-/64-bit keys are distinguished and cap
exhaustion falls back. No GPU texture/buffer or larger staging/cache budget is introduced.
First use can compile an additional host PSO, so capacity checks alone do not prove a process
footprint or system-pressure benefit.

## Exact half-value behavior

On the tested M4, a native half texture read preserved every one of the 65,536 half bit
patterns, including signed zero, subnormals, infinities and NaN payloads. Initially the proxy
shortcut failed the old-path comparison: the original RGBA16F render-target resolve writes
all NaNs as positive quiet NaN (`0x7e00`). A bounded mismatch check confirmed that difference.
The proxy-only specialization now reproduces that normalization using integer masks; native
exports preserve the source bits. No finite arithmetic or lower precision is substituted.

Tests compare native tiling with the original blit-plus-tiler for 128x64, 128x73 and 1920x1080,
three bank swizzles and both RGBA8/RGBA16F. The half cases cycle all half bit patterns. Separate
proxy tests compare against a real native render resolve, then verify stale-native/later-read
coherence, non-2x fallback and unchanged tracked resource capacities. The initially rejected
NaN result is retained locally, rather than treated as a passing optimization.

## Bounded actual-path diagnostics

Native metadata includes per-dispatch classification and the broad-residency state at that
dispatch, plus successful 32-/64-bit texture tiling events and avoided linear capacity.
The analyzer rejects inconsistent classification flags, missing compute encoders and invalid
copy extents. These records contain no resource payload and retain existing capture bounds.
Requested narrow access alone is not evidence that a mixed encoder omitted broad residency.
Copy counts and isolated timings are not gameplay FPS; installation needs a qualifying
same-scene comparison with slow-frame and memory checks.

Private evidence is kept under `outputs/metal-resource-copy-tests` and
`outputs/metal-resource-copy-combination`. No game data, caches or shader sources are published.

## Validation and measured limits

All 71 existing native regression groups passed. Five focused groups, 12 combined
held/direct/no-replay groups, four real compute variants compiled cold and from cache,
37 benchmark Python tests and 20 Metal report/packaging tests passed. The physical-pointer
compute variant remains broad; the direct variants are recognized despite an unused page-table
name. All 25 serialized patches reconstruct and reapply idempotently.

The final isolated 1920x1080 native-export run retains 20 samples per path after fixed warmup,
interleaving old/new in ABBA order within one process. RGBA16F GPU medians were 0.975125 ms
for blit+tiler and 0.508146 ms for direct texture tiling. This synthetic preallocated workload
is not gameplay FPS or a process-memory result; raw samples are retained locally.

Two three-frame Central Yharnam diagnostic series used the same renderer SHA-256
`7ee150329b7337ff2e1076a5247d18c131d18732bdb02818137d4736e91df2df`, cohort/settings and
camera. Existing precise residency and RGBA8 tiling were ON in both; the two new flags alone
were OFF / ON. Every member passed strict bounded-metadata validation and original saves and
launcher preferences remained unchanged. Main G-buffer work was 551 draws / 8,460,948 indices
in five members; one reference member had 552 draws / 8,584,020 indices. No synchronized
full-scene image-equivalence claim is made from these metadata captures.

| Path | Game compute dispatches per captured frame | Direct-only requests | Dispatches without broad guest declarations | Successful tiling exports | Half exports |
| --- | --- | --- | --- | --- | --- |
| Reference | 68 / 65 / 67 | 0 / 0 / 0 | 0 / 0 / 0 | 1 / 1 / 1 | 0 / 0 / 0 |
| Candidate | 66 / 63 / 65 | 27 / 27 / 29 | 8 / 8 / 8 | 2 / 2 / 2 | 1 / 1 / 1 |

The remaining requested-direct dispatches reused an encoder that already had broad residency;
they intentionally did not downgrade it. Per-dispatch states are not counts of API calls or
proof of resident-byte savings. The new half export bypassed one proxy-to-native resolve and
one 16,711,680-byte linear staging request/copy per frame. Avoided allocation requests/traffic
are not equivalent to a permanent reduction in resident memory or cache capacity.

Both entry series had 285,212,672 bytes / 14 staging objects. However, candidate render-target
capacity was 566,392,704 bytes / 266 objects against reference 503,706,496 / 249 at these early
samples; process footprint was about 5.49 / 5.48 GB decimal. Loading/retirement trajectories
were unmatched, so neither memory non-regression nor a saving is established by those numbers.

A warmed reference capture stopped at 40.9 seconds under the unchanged warning guard. To
inspect paths within that safety limit, `--capture-native --capture-on-entry` now captures
after the existing stable-world check, explicitly marks `warmup_completed=false`, never emits
FPS metrics, and is rejected for scored/resource-capture runs both at CLI and function entry.
All default scoring warmup and memory guards remain unchanged. Entry metadata is diagnostic,
not steady-state performance evidence.

The full 45-second-warmup / 45-second-score candidate run subsequently hit the same warning
guard at 59.1 seconds, before scoring; cleanup verified originals unchanged. No new accepted
FPS/slow-frame/memory comparison exists, the >=60 FPS objective remains unverified, and the
normal installed app is unchanged. No unrelated app was closed and no guard/cache/quality
setting was relaxed to obtain results.
