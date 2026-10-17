# Metal memory categories and idle staging backing (0.2.6-metal-mem2)

This update measures allocation ownership and release rather than shrinking texture caches.
The Metal-only probe adds `runtime_memory_snapshot_v1`; the original probe and both Vulkan
renderers are preserved. The renderer uses an optional symbol lookup and remains compatible
with probes without this diagnostic ABI (it reports liveness as unknown).

## Categories and accounting

Every five seconds `Metal memory category` reports unique owned buffer/texture capacity for:
ordinary buffers, staging rings/dedicated transfers, sampled textures, render/depth targets,
reduced scene proxies, velocity mirror/snapshot/packing, explicitly owned upscaler textures,
and occlusion counters/history. Texture views and imported guest-buffer wrappers are aliases,
not new allocations. A lifetime token on each owned Metal resource records its actual object
teardown; a retained texture view can therefore keep an allocation in the retired category
after its original renderer reference is released. The ledger does not retain Metal resources.

`allocated` includes the retired and discarded subsets; do not add these columns together.
`released` is cumulative capacity of destroyed Metal objects, not proof of immediate physical
page return from the driver. `discarded-capacity` is retained reusable staging capacity whose
contents Metal accepted as empty; it is not measured resident memory or measured savings.

`Metal memory totals` reconciles these capacities with `currentAllocatedSize`, no-copy guest
alias capacity, process footprint and the graphics ledger. Unclassified Metal capacity is kept
explicit: it includes opaque MetalFX internals, pipelines, drawables, alignment and other
uninstrumented Metal allocations. The classification does not purport to measure those
components individually. CPU heaps, Rosetta and runtime data remain in the non-graphics part
of process footprint; the PS4 engine's own suballocations inside a still-live direct allocation
cannot be inferred from the operating-system allocation boundary.

`Guest memory categories` is a locked snapshot of the production runtime's direct allocations,
types 0/3/other, flexible live bytes, imported capacity and its intersection with live direct
allocations, free-but-retained imported ranges, wholly free imported chunks, release call/byte
counts and bytes zeroed in GPU chunks. Mapped guest addresses can alias the same physical pool:
the mapped byte count is diagnostic only and must not be added to live allocation bytes.
None of these capacity counts should be added to process footprint.

## Release investigation

The imported Metal guest chunks currently live for the process. `sceKernelReleaseDirectMemory`
removes game mappings/allocations and zeros released GPU-visible ranges. It does not destroy
the no-copy Metal buffer or return its backing pages. Buffers are registered in the device's
encoder list, the layer's source table, caches and GPU page tables with stable handles/addresses;
destroying or replacing one on a release notification is unsafe without retiring all users.

An isolated 64-MiB macOS shared-memory test rejected `madvise(MADV_ZERO)` with ENOTSUP (45);
its second mapping retained the original bytes. Do not substitute this call, MADV_FREE, or
unmap/remap for the existing shared-memory semantics. No imported chunk is made purgeable.
The new counters distinguish runtime-level releases from the game's own internal heap frees.
An internal heap free may leave the enclosing direct allocation live; such bytes are not safe
to reclaim merely because a scene changed. Full-game liveness distribution needs a new run.

## Safe optimization

An existing staging ring block that has been unused for 3000 frames and whose last GPU tick
has completed is removed from the residency set and marked `MTLPurgeableStateEmpty`. Its Metal
object, size, GPU address and pool slot stay available. A subsequent successful ring reservation
restores NonVolatile state and residency before any CPU write or GPU command consumes it.
Only staging content that no caller still holds can be discarded; deferred held transfers,
unfinished GPU work, texture caches and guest imported memory are excluded. Expiry, 512-MiB
retention setting, cache budgets and eviction rules are unchanged. No GPU wait is added.
Set `BB_MTL_IDLE_STAGING_DISCARD=0` before starting the process to disable this optimization.

A debug-layer GPU round-trip validates contents after reuse and verifies that Metal accepted
Empty/NonVolatile states. The immediate process footprint did not fall in the first controlled
16-MiB test: making backing discardable is not an observed 16-MiB physical-memory saving.
Real memory-pressure/scene-transition benefit remains to be measured in gameplay.

## Compatibility and validation

The existing velocity render-tail drains, two blur blits, query isolation, max-of-three
occlusion history, sampler memo, texture GC thresholds and completed-handler retirement are
unchanged. The allocation ledger does not alter ordering or resource bindings. Tests cover
real delayed object destruction, views retaining parent textures, held/unfinished staging
exclusion, idle ring reuse without a new buffer/GPU address, and real GPU copy correctness.
The C runtime test uses production allocation functions for partial release, neighboring live
ranges, zero-on-reuse, and retained imported capacity.
