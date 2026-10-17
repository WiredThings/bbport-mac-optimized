# Internal GPU allocation lifetimes (0.2.8-metal-mem3a)

This release instruments the game's two known GPU suballocators inside its persistent direct
memory pools. It does not instrument general malloc, animation heaps, or all engine pools.
OS direct-memory liveness and internal suballocator liveness are separate observations.

## Verified hooks and bounded coverage

The supported 1.09 executable has a range allocation return at 0x26aa255, three TLSF-like heap
legacy allocation returns at 0x11e6629/0x11e6736/0x11e6846 (not reached by the measured run),
the actual TLSF malloc/memalign entries at 0x2085730/0x2085a60, a collector's accepted-free branch at
0x26aa986, and heap free at 0x20858a0. Expected instruction bytes must match. The collector
captures the node boundaries before coalescing; the heap wrapper records a matching free only
after the original function returns. Each event carries an allocation generation, so an old
free cannot release a replacement at the same address. Native inline collector code preserves
GPRs, flags, x87/MXCSR and all 16 AVX vector registers, with the red zone untouched.

`BB_GPU_LIFETIME=1` is the Metal launch default; `=0` disables the new instrumentation and
reclamation. Diagnostic int3 mode disables this tracker. Conflicting BB_FREE_CHECK/BB_LABEL_TRAP
collector diagnostics skip the range free detour. Automatic reclamation requires complete
paired native hook coverage; unsupported versions and partial coverage only produce diagnostics.
The actual allocation entry wrappers pair plain/aligned allocations with frees; nested
aligned-to-plain calls count once. Calls outside GPU-visible direct mappings are excluded
before the tracking lock. Per-thread mapping classification caches invalidate on the runtime's
atomic mapping generation, including after remaps/protection changes and negative cache hits.
The measured first run reached all hook byte checks, but old allocation returns observed no
heap allocations while the shared CPU/GPU free function received millions of CPU calls. This
is why byte-check coverage alone is not reported as effective runtime ownership coverage.

Histories/live intervals have 32768-entry per-domain limits and bounded retired generations.
A capacity overflow disables automatic reclamation for that process, preventing an unrecorded
replacement allocation from authorizing stale-resource removal. Unknown frees stay unknown.

Every five seconds `Guest resource lifetime range/heap` reports observed allocation/free,
current intervals/bytes, same-address reuse, overlap replacements, unknown frees and drops.
`Metal guest association` reports owned allocation capacity by current guest lifetime status.
Associations cover image backings, promoted/MSAA images, scene proxies and owned guest-addressed
buffers. Engine-owned rings/tables, imported multi-allocation chunks and framework histories
remain unknown when no single covering allocation exists.
Associations retain no Metal resource; views and delayed GPU destruction still use the 0.2.6
actual-object lifetime ledger. Unknown includes uninstrumented sources and opaque resources.
These capacities overlap footprint and cannot be added as separate RAM usage.

## Explicit release reclamation

Once per second the recording thread joins pending texture binding work and examines registered
images. A clean sampled image owned by the fully wrapped TLSF heap can be unregistered only when its fully covering allocation was
explicitly freed at least two seconds earlier, no live interval overlaps it in either allocator,
and every relevant GPU tick is known completed. At most 16 images are retired per scan.
The range-family tracker observed unknown frees from additional allocation paths and remains
diagnostic only; it cannot authorize removal. Replacement allocations re-associate existing cached images; reuse alone never evicts them.
A generation lock spans cache unregistration so a concurrent allocation cannot publish a
replacement halfway through removal. There is no GPU wait or guest page discard/decommit.

Render/depth targets, storage images, video-out surfaces, GPU-modified or GPU-dirty images,
bound/target/rebinding images, and scene proxies are protected. Velocity/MetalFX history and
occlusion counters are not candidates in this sampled-image cache. Deferred image/view/native
Metal destruction follows the existing scheduler path. A stale FindImage memo now checks slot
liveness before dereferencing a retired entry. Existing cache capacities, pressure thresholds,
age policies, staging retention, resolution, effects and user presets are unchanged.

`Guest resource reclaim allocation-bytes` counts cumulative cache allocation capacity retired,
not instantaneous physical memory savings. Gameplay footprint, graphics ledger, allocation
release and system compression/swap still need to be observed across a scene round trip.
`tools/macos/analyze_metal_memory.py` keeps the matching internal and Metal association snapshots.

## Validation

Tests exercise generations, overlapping allocator domains, unknown/double/old frees, bounded
history overflow and concurrent reuse exclusion; real native free detours and an AVX/GPR/flags/
MXCSR preservation harness; real Metal allocations and views retaining retired textures; and
completed-tick versus bound/target/storage/proxy/GPU-write reclamation eligibility. Existing
velocity batching, two blur blits, occlusion query isolation and staging reuse tests are rerun.
No automated test launches gameplay. Actual hook coverage and memory benefit require a new
user-started run; do not interpret unit tests as a measured reduction in game RAM.
