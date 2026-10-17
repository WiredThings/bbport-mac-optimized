# Two-level GPU address translation (0.2.9-metal-pt2)

The Metal backend now defaults to a two-level BDA page table. It preserves the existing 40-bit
guest address space, 16-KiB page size, current/mirror read view and guest write-through view.
This is address metadata compression; texture cache capacities, retention and effects stay as
before. The Vulkan fallback binaries and CPU runtime are preserved by the app overlay.

## Layout and lifetime

A leaf holds 4096 entries per view. At 16-KiB guest pages a leaf covers 64 MiB of virtual memory;
the two views share one 64-KiB buffer. The two root planes together occupy 256 KiB. Each missing
root points into an initialized shared zero leaf (64 KiB), so a missing mapping never causes a
physical null-pointer load. Only touched virtual regions allocate leaves. There is no fixed
window assumption: high guest addresses and the final page below 1 TiB remain representable.

Leaf buffers retain stable GPU addresses until the cache shuts down. Unmap/republish clears
entries rather than freeing/recreating the leaf. UniqueBuffer still uses completed-tick deferred
destruction. Zero/data initialization is submitted before a new root pointer is published.
Every render/compute encoder explicitly declares the indirect leaf buffers as reads; the Metal
runtime's tracked blit hazards and the existing root binding as logical hazard anchor order
pending shader reads against table updates. No per-frame GPU wait is introduced. This also
keeps held rendering, replay, velocity render tails and occlusion isolation on their existing
paths. The resource registration list retains no Metal objects and removes leaves on shutdown.

## Shader ABI and caches

Both paged buffer loads/stores and DMA/ReadConst use the same new lookup helper. It bounds the
40-bit guest address before indexing, reads the selected root plane and then the leaf entry.
Descriptor range guards, zero loads, trash stores and write-through ownership are retained.
The paged record's guest-table offset now selects the guest root plane. CPU table writes and
shader translation use the same leaf/address constants.

The compiler profile records the ABI. Pipeline namespaces are metal[-relaxed]-pt2; the same
build's flat control uses metal[-relaxed]-flat2. Existing caches remain intact. SPIR-V-derived
MSL keys also differ when generated addressing differs. First-use compilation may affect the
first visit, so gameplay comparisons should use a warmed scene and matching camera/settings.
Set BB_MTL_TWO_LEVEL_BDA=0 in the launcher's environment for the flat A/B control; the default
and normal subsequent launch are two-level.

## Verification and measurements

The production C++ table updater and SPIR-V emitter are exercised on a real Metal GPU with
16- and 64-KiB pages: high addresses, leaf/page boundaries, both views, pending remaps, unmaps,
absent roots, 40-bit overflow, last-page bounds, write-through exclusion and DMA/ReadConst.
The independent regression groups cover velocity batching, both motion-blur blits, occlusion
history/isolation, staging reuse and guest allocation lifetime tracking.

The benchmark uses production paged-buffer shaders, 4,194,304 lookups per dispatch, local and
random page patterns, batches of 16 dispatches, warm-up and 20 samples, in both A/B orders.
It is an addressing microbenchmark, not a game FPS estimate. Game FPS, actual process footprint
and table allocation counts are recorded separately. Logical table capacity savings do not
prove an identical physical footprint reduction. Full results live alongside the release's
verification artifacts; no gameplay is launched by the automated tests.
