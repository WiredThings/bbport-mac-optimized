# Metal memory accounting port, 0.2.1-metal-mem1

## Imported change

Adapted the process-memory accounting from bmy/bbport-mac commits
`a923b61ecce51d9d5bfe1deb855d82ed2683eb0c` and
`4de5fd1340c8184dd88687fc07631ee2425cb587` to the existing Metal Runtime.
The root Vulkan fallback already had it; the direct Metal backend did not.

The budget is Metal's recommended working set minus the process footprint
outside macOS's graphics ledger. Both resident and compressed graphics ledger
bytes are excluded from that subtraction. Metal/driver graphics growth therefore
does not recursively shrink the budget. Sampling is cached for 250 ms. Failed
sampling falls back to Metal's recommendation. The upstream 2 GiB budget floor
is retained (bounded by the device limit); it is a lower bound, not a 2 GiB cache
cap. A diagnostic reports budget, non-graphics bytes and the graphics ledger.

A required native-Metal compatibility adjustment removes `GuestMemory::ImportedBytes()`
from `MTLDevice.currentAllocatedSize` before comparing usage with the budget.
A 64 MiB no-copy wrapper raised currentAllocatedSize by exactly 64 MiB while
process footprint and graphics ledger did not grow: those pages are already
charged to non-graphics CPU memory. Keeping the raw allocation count with the
new budget would charge the same pages twice and could force aggressive eviction.
The adjusted renderer usage is compared with the graphics ledger (take the larger),
as in the existing bmy-derived Vulkan collector, to include driver-owned graphics.
On OS sampling failure both usage and budget fall back to the original counters.

The existing 70/85/95 percent GC watermarks, 20-second normal idle lifetime,
5-second pressure idle lifetime, GPU-written/tiled-image protection, synchronous
writeback and GPU completion checks remain unchanged. No fixed smaller budget,
new cache directory, shader-cache invalidation or staging-keep reduction is used.

This is accounting, not a hard process-RAM cap or a new system-pressure watchdog.
A scene whose working set is below the old and new collection marks may consume
roughly the same memory. It chiefly makes collection reachable earlier when
non-graphics memory grows. Full-game memory savings and FPS are unmeasured.

## Existing upstream fixes verified, not duplicated

- Huax's pinned Metal staging pool already trims idle ring/dedicated allocations
  after 3000 frame ticks and GPU completion. Held deferred spans are protected;
  the 512 MiB ring keep setting is unchanged. bmy's staging retention commit
  `85bb14f2905934961f6af6a7b456287de6a1ec0c` is already reflected here. Prewarming
  additional Vulkan blocks would increase memory, so it was not added to Metal.
- Metalborne `6069d1617369b38f19b02262ee9a6a665c253b7a` prevents blocked tiled
  images from starving eligible LRU entries. The same bounded scan and rotation
  already exist in Huax's texture cache. Its regression test is included to keep
  this behavior intact.
- Huax lean texture usages, guest no-copy buffers, resource residency and existing
  ABI/menu adapters remain in place. No Vulkan-only driver/pool code is applied
  to Metal.
- shadPS4 issue 4877's small UMA budget-floor patch is not imported: its collector
  and driver differ, and its thresholds are not a validated fit for this backend.

## Verification

The `0003-ledger-budget-and-retirement-tests.patch` series includes pure budget
regressions (driver graphics growth, invalid/negative counters, saturation,
small limits and sampling failure), Metalborne's blocked-prefix LRU regression,
and an actual Metal object-lifetime test. The latter ages a dedicated allocation
past idle expiry while held, then while its tick is still open, and verifies it
is released only after freeing, GPU completion and deferred destruction.

Build with the existing build_metal.sh, then explicitly build and run
`mtl-memory-budget-test`, `texture-cache-gc-test`, and `mtl-runtime-test`.
Use the existing x86 dependency library path for unbundled test executables.
The 64 MiB compatibility probe is `tools/macos/metal-budget-alias-check.mm`;
compile it for x86_64 with the pinned engine's gpu/shadps4 include directory,
Objective-C ARC, and Metal/Foundation frameworks. Its assertion proves the
wrapper does not inflate the adjusted renderer usage.

The release packaging verifies runtime instruction bytes, relative library
resolution and deep strict signatures. No game is started during validation.

## Sources

- https://github.com/bmy/bbport-mac/commit/4de5fd1340c8184dd88687fc07631ee2425cb587
- https://github.com/bmy/bbport-mac/commit/a923b61ecce51d9d5bfe1deb855d82ed2683eb0c
- https://github.com/bmy/bbport-mac/commit/85bb14f2905934961f6af6a7b456287de6a1ec0c
- https://github.com/Livio21/metalborne/commit/6069d1617369b38f19b02262ee9a6a665c253b7a
- https://github.com/Huax-1839/bloodborne-macos/tree/11992198df6306dacf6ec37de57ee920db1acc11
- https://github.com/shadps4-emu/shadPS4/issues/4877
