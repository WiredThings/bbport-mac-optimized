# Cross-chunk read crash fix — 0.2.25

## Observed failure

The owner's 2026-10-18 normal traversal/combat run stopped after approximately 25 minutes:

```
Layer memory: 0x70d1a34400+0xf9ad8 not whole in one guest chunk
CreateBuffer: Assertion Failed! Metal has no sparse buffers (GetSparseArena is null)
STOP: GPU library assertion failed
```

A 1,022,680-byte **read-only** resource was not contained in one imported guest chunk.
`LayerBind` returned no single buffer, and `ObtainBuffer` fell through to the shared Vulkan
sparse-arena path. The Metal assertion terminated deliberately; the log does not establish
an OS memory-pressure kill. Last reported process footprint was about 7.07 GiB. The four
optional launcher experiments were enabled, but this evidence does not tie the assertion to
any one experiment.

## Fix and scope

Patch 0034 intercepts read-only layer misses when the backend has no sparse arena. It resolves
and validates the entire range into complete source pieces, reserves an exact range in the
**existing 128 MiB stream ring**, and records ordered GPU buffer copies. Earlier GPU writes
remain visible, subsequent writes do not mutate the captured snapshot, and normal tick watches
prevent recycling before readers finish. Texture-buffer pieces retain image synchronization.
The old layer path has already copied current mirror data back before reporting its miss.

No new permanent GPU buffer, cache, BDA entry, shader variant, or global budget is added. The
rare crossing consumes ring space and transfer commands; it can trigger the ring's ordinary
wait if full. Thus this is a correctness fix, not a claim of free transfers or higher FPS.
Host metadata is temporary and limited to 4096 pieces. Holes, overflow, incomplete sources and
requests larger than ring capacity reject before recording any gather; unsupported resources
are not truncated or zero-filled. Writable cross-chunk aliases remain outside this fix.

Single-chunk bindings are unchanged. Two-level BDA, motion/velocity, occlusion, lifecycle
reclamation, attachment policies and all launcher choices retain their existing behavior.
Game data, quality, saves and original user-selected optional experiments are unchanged.

## Validation

`mtl-runtime-test` passed with `MTL_DEBUG_LAYER=1`, including the exact observed resource size
across discontinuous sources, complete-byte comparison, GPU updates before and after snapshot,
five captures wrapping one fixed ring, and rejection of missing/invalid/oversized ranges.
The existing runtime suite also passed buffer, guest import, texture/tiling, format/view,
occlusion, ordering, idle-staging and memory-lifetime checks. The 34-patch series reconstructed
and matched idempotently. This does not reproduce the owner's actual walking route or prove
that every unrelated gameplay crash is eliminated.

Local evidence: `outputs/crash-20261018-cross-chunk/last-run.log`, `context.json`,
`native-runtime-test.log` and the installation record.
