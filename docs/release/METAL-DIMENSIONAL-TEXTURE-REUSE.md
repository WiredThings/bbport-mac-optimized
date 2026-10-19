# Dimensional texture reuse

## Crash evidence

The 2026-10-19 18:22 gameplay crash was `SIGABRT` (shell exit 134) on `bb:DrawRec`, in `Runtime::CreateImageView`. The preceding log identifies a `Color1D` view of a `Color2D` image. Metal rejected the `Texture2D` → `Texture1D` view. This is distinct from the earlier speculative numeric-format assertion and the separately logged tessellation pipeline failure. The player reported being killed by an enemy in `cummmfpk` immediately beforehand.

The old log does not contain the resource address, dimensions, or cache-selection path. It therefore cannot establish which of the two invalid selection paths below caused that particular live crash.

## Reproduced paths and correction

The native regression uses the actual `TextureCache::FindImage` and Metal backend, with synthetic memory and no game files or disk cache:

- The exact-match loop previously allowed any image dimension when the requested extent was `1×1×1`. A cached 2D image was returned for a 1D request.
- After strict exact matching failed, same-address overlap resolution compared layout/format/size without requiring compatible dimensions. A `64×1` 1D request also selected a cached 2D image.

The old implementation produces the same Metal assertion and exit 134 in the regression. Exact matching now requires the correct image type; memoized returns also validate view compatibility. A 1D/non-1D overlap is not returned as a texture view.

When the pixel layout is identical, the cache uses its existing image replacement protocol: refresh the source according to CPU/GPU dirty tracking, copy all pixels to a correctly typed backing, mark bound resources for rebinding, unregister the old entry, and defer destruction until GPU completion. The complete overwrite avoids an unnecessary destination upload. This retains GPU-only values and avoids retaining independent aliases that can become inconsistent after later GPU writes. Different layouts retain the existing independent-resource allocation behavior. Cache budgets and collection thresholds are unchanged.

View construction now rejects any remaining incompatible type before entering the backend, with the address, dimensions and existing failure-only thread/stack diagnostics. It does not silently substitute a view type or discard a draw. At most eight dimensional-reuse messages per process identify `exact-1x1-reject`, `overlap-replace`, or `overlap-reject`.

## Validation and limits

The regression checks both previous lookup failures, valid repeated cache hits, 32 alternating dimensional replacements with a GPU-only byte pattern, and a constant registered-image count. Metal API validation is enabled for the positive regression. A deliberately invalid direct view exits 23 before a Metal API call. The original speculative-format test and Metal runtime, scene targets, BDA page table, scheduler, and motion shader tests also pass. Ten online Python regressions pass; the networking module and host executable are not modified by this adapter.

These tests establish pixel preservation and lifetime behavior for the exercised layouts. They do not establish a gameplay FPS or resident-memory improvement. Live enemy-death regression is recorded separately; interrupted startups are not accepted as death/respawn validation. Existing formal saves, preferences and shader caches are read only; test runs use their own copies.

## Isolated live regression (2026-10-19)

The player performed two enemy deaths in `cummmfpk` using a private save/config/cache copy. Both returned to an operable lamp scene in the same process. Frame telemetry recorded world unload/reload intervals at 107.91–120.05 s and 326.82–338.97 s. During the second reload, the new guard logged `overlap-reject`: address `0x705eb60000`, requested `Color1D` 1×1×1 (32 KiB), cached `Color2D` 38×14×1 (64 KiB). The incompatible candidate was rejected and rendering continued without the invalid-view assertion or Metal abort. This verifies a real gameplay guard hit; it does not identify the resource from the original crash, whose log omitted its address.

The isolated session was offline; existing networking regression tests pass, but this change has not received a new live online death test. The separately logged tessellation pipeline failure (160/224 byte patch constants) remains outside this fix. No stage-level GPU capture was enabled and no comparative FPS or memory benefit is claimed.

```sh
cmake --build out/gpu-metal --target texture-cache-views-test --parallel 2
MTL_DEBUG_LAYER=1 out/gpu-metal/texture-cache-views-test
# Expected exit 23, with the incompatible-view diagnostic:
out/gpu-metal/texture-cache-views-test invalid-view
```

The renderer remains pinned to `11992198df6306dacf6ec37de57ee920db1acc11`, with this change carried as adapter 0039. No shader format, archive version, game patch, motion-vector shader, occlusion policy, two-level address-table encoding, or quality setting changes.
