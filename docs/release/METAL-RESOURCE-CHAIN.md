# Requested buffer accesses and Metal resource lifetimes — 2026-10-21

Status: bounded diagnostics, default-OFF experimental candidates and validated consumed-clear
continuation enabled in development source. The installed application,
normal saves/settings/cache and rendering quality remain untouched. Adapter 0047 builds on
0045/0046; it does not restore the withdrawn fragment-fence experiment. The latest controlled
travel comparison and real-cache tests are recorded at the end of this document.

[当前结论摘要](METAL-RESOURCE-CHAIN-STATUS.md)

## Recording boundaries

The native metadata recorder remains opt-in (`BB_CAPTURE_NATIVE=1`), at most 8192 records /
2 MiB per frame. It compares only the CPU command payload of observed 192-byte writes:
eight watched guest addresses, no GPU readback, no exported payload. Versions reset at each
capture boundary. Equality and allocation-generation identity are independent fields.

Bound buffer records preserve logical `Shader::SwStage`, program hash and original guest
address until `ResetBindings`. The original AllGraphics/Compute barrier decisions are unchanged.
Stages are logical provenance, not physical Metal stage timing; geometry/tessellation lowering
can differ. Index fetch has stage 6, runtime/unknown stage 7.

Known overlapping guest ranges and native-buffer aliases produce per-version observations;
unknown global/paged accesses remain unknown. Runtime observations overlap shader observations
and must not be added. General access records stop at 1024 with an explicit overflow marker;
watched-range observations continue within the global capture bound. The command sequence is
requested access order, not a proof of actual GPU execution order after held-pass replay.

CPU read/write hooks cover explicit `ReadMemory` / `OnCpuWrite`, CPU command writes and
VRAM read/write-fault pages, not every possible raw access. Fault records conservatively cover
4 KiB pages; a later write to an already-unprotected page can remain unobserved. The command mirror update is additionally visible through Runtime
access tracking, but only its logical target increments the command-write version. Missing
records, aliases, generation zero, truncation or overflow cannot justify deleting work.

## Lifetime metadata

Each captured frame takes one metadata snapshot, even if multiple submits/GC calls occur.
It contains at most 2048 live owned Metal allocations, a fixed ring of 256 actual destructions,
256 staging entries and 2048 texture backings. Every limit/overwritten history is explicit.
Integer identities and monotonic IDs do not retain resources. No heap/texture allocation,
resource-content access, encoder close, forced GPU wait or collection change is introduced.

The existing allocation ledger records retirement separately from the associated-object
callback on actual Metal object destruction. A diagnostic UID separates reuse of native pointer
values. Birth/retirement/destruction use steady-clock microseconds. The ring's cumulative count
is not a complete historical log once it exceeds 256.

Staging records include ring/dedicated kind, capacity, idle submission/collection tick age, held status, known GPU
completion and discarded status. Texture backings include image UID/native identity, original
guest generation, target/proxy flags, access/write ticks, LRU age, known GPU completion, guest-tag status/current owner and the unchanged safe-release
predicates.
Older captures call this staging field `age_frames`; these are internal OnSubmit counters,
not presented game frames. New records use `age_ticks`.
A cached retired texture backing and a ledger object pending destruction are different states.
Capacity is not physical footprint; valid cold resources remain cached under existing policies.

## Validation and measurements

Directed tests exercise equal/changed payloads, known/unknown/changed generations, native aliases,
vertex vs fragment provenance, nonoverlapping endpoints, CPU hooks, reset and capacity overflow.
The real Runtime test confirms a retired allocation remains visible before actual destruction,
then appears in destruction history. All existing barrier bookkeeping remains conservative.

`tools/macos/analyze_resource_chain.py TRACE` summarizes observations and retained bounds. It
never emits a safe-to-delete verdict. Captured diagnostics are excluded from scored FPS results.

## First warmed captures (not FPS scores)

All diagnostic launches use the existing old frozen cohort, not a fresh Central Yharnam save:
native 1080p, MetalFX OFF, effects unchanged, fragment uniforms ON, native clear batching OFF,
45 seconds warmup plus the benchmark quiet-window checks. No test/compile runs during a game
capture. Formal save hashes and launcher preference non-write are checked by every cleanup.

The first same-binary pair (`b9364f798c087933c303e8296b837328d6017dfeb83d8ec6aa0433923766c1c2`)
records three warmed frames for each existing update path. Every frame has 35 writes to the
same 192-byte guest range. All 34 subsequent writes have identical CPU payload and the same
known guest allocation generation. The recorder captures each resulting Runtime write, but
no watched shader/CPU read. This is **not evidence of dead writes**: direct CPU loads, an
already-unprotected write page, descriptor flattening and cross-frame consumers are not fully
observed. The 1024 general-use record limit is reached; watched records continue and the overall
capture is not truncated. Neither synchronization nor writes are removed.

| Existing path | Staging capacity | Staging objects | Target capacity | Target objects |
| --- | ---: | ---: | ---: | ---: |
| Inline compute | 293.875 MiB | 21 | 603.581 MiB | 343 |
| Staged/blit | 202.875 MiB | 14 | 534.185 MiB | 260 |

Across those frames, the oldest staging block was used 8–11 internal collection ticks ago for
inline and 4–5 for staged; no completed staging block has been idle for 600 ticks, none is held.
These are frequently reused blocks, not evidence of abandoned resources. Both paths have zero
ledger-retired bytes in the snapshots. Recent actual target destruction follows retirement in
at most 75.1 ms for inline and 443.1 ms for staged **within the retained 256-record ring**; this
does not rule out a different lifetime outside that ring or in a different scene.

After adding conservative fault-page observations and ownership predicates, a repeated inline
run (`f2d47581201c67efe3fba632cd922f8e4094239f4dd08d0243f38f85e693276d`) instead has only
202.875 MiB staging / 14 objects and 534.185 MiB targets / 260 objects. Instrumentation and
startup history differ, so this is not a causal A/B with the first binary. It does demonstrate
that a larger population cannot be attributed to the inline flag from the original pair alone.
The three captured frames again see 35/34 writes/equalities and no observed watched CPU access.

This later run has 617 texture backings: 455 unknown allocation status, 138 live, 24 reused,
none released. Of 190 cold backings (>=600 collection ticks), 174 retain the GpuModified flag.
No backing satisfies the existing safe-invalid-release predicate. Among ledger target objects,
85,315,840 bytes are associated with reused guest tags; this needs per-generation overwrite /
writeback analysis, **not deletion by age**. Live/unknown/reused does not establish that cached
GPU data can be discarded. Cache budgets, all target protection checks and retirement delays
are unchanged. No performance preference is changed or new FPS gain claimed.

The final same-binary staged confirmation has 218.875 MiB staging / 15 objects and
534.185 MiB targets / 260 objects, versus 202.875 MiB / 14 and the same 260 targets for inline.
Thus the extra 83 targets from the first pair do not consistently follow the update flag.
No cache-size or forced reclamation change is justified. All four launches (12 captured frames)
finish through their own cleanup, with normal saves unchanged and no preference writes.

Final source also rechecks the recording state inside the native-output mutex, so a CPU fault
crossing the closing frame boundary cannot append a late record after recording has stopped.
This changes diagnostic closure only, not game synchronization. The staging-age field is renamed
to age_ticks; the offline analyzer accepts the legacy age_frames spelling as internal ticks.

## Retained verification

The retained source passes 11 Metal invocations (nine existing regression executables, the real
Runtime source/lifetime case and the staged-update fallback), the strengthened CPU metadata test
and 84 Python checks. All 47 adapters reconstruct and reapply to the unchanged upstream pin;
outer/engine whitespace checks pass. The last game captures precede the diagnostic output-lock
recheck and age-field rename; those final output-only changes are covered by the rebuilt tests,
not represented as another in-game measurement. No new optimization flag or production default
is enabled. The installed renderer remains
`f408e615d804194fdd97124a2a2d02ea4a3dc1812cacc6ec3f01312d68ef4ce7`.
All isolated game processes have stopped; final memory pressure is normal. chronod remained
small and was not restarted.

Evidence: `outputs/resource-chain-20261021/` in the enclosing task workspace contains compact
summaries and validation logs; raw captures and frozen save data remain private outside Git.

## Follow-up: watched range and content provenance (0048–0051)

0048 pre-seeds `BB_WATCH=guest-address,size` before the first observed write. Version zero
means no command payload has been observed yet. The observer now also covers ring snapshots,
vertex ranges, pending copy controls, hoisted shader bindings, DMA and texture ranges. An
unbounded device-address access remains explicitly unknown. The private benchmark accepts
`--watch-buffer` only for native metadata captures, never scored FPS.

The captured 35 × 192-byte payloads are entirely zero. No narrowly identified consumer is
observed after the first write; broad copy-shader ranges occur before it. This does not cover
every raw CPU access or paged GPU access. Equal zero payloads do not justify deleting writes.

0049 tests an exact byte-pattern native fill, preserving every command and its ordering. GPU
byte tests cover intervening writes, earlier readers, caller payload mutation, canaries,
nonuniform/unaligned fallback and unchanged staging capacity. The actual game capture hits
all 35 writes. The flag `BB_MTL_FILL_UPDATES` remains **OFF**: same-binary ABBA gives −1.25%
and reverse BAAB +0.35%, with combined means 62.408 FPS OFF and 62.123 FPS ON (−0.46%).
Each order's last run retains a smaller resource population, with either flag value. This is
not a proven optimization or a causal memory saving. These are the old frozen cohort's
native-1080p measurements, not a fresh Central Yharnam 60-FPS claim.

0050/0051 add opt-in native backing counters for uploads, clears, copies, encoded draws and
potential storage writes. Real texture views resolve to their base allocation. Counters run
only with native capture or the private `BB_TEXTURE_AUDIT_DIR` inventory; normal launches
perform no content counter locks or resource inspections. Inventory snapshots hold no Metal
objects and perform no readback, GPU wait, collection or cache-budget change. The fixed six
counters and timestamp add 56 bytes per existing allocation-ledger entry. They do not keep a
resource history alive. `GpuModified` can be set at binding, so it is deliberately not equated
with a recorded draw. Draws can still mask/discard every fragment, and storage bindings are
potential writes; these counters do not prove per-pixel or full-overwrite coverage. Copy counts
conservatively include entry paths that may fall back or reject a transfer. External native
write paths are not exhaustively covered, so zero observations are not proof of CPU ownership.

The warmed 0050 capture has 16 reused-tag backings / 46.21 MiB. Their two largest backings
have encoded draws; other entries have potential storage writes. Only two tiny backings have
upload-only provenance. This is different from the earlier 81.36-MiB startup population and
does not establish monotonic growth. No GpuModified backing was discarded or written over a
new allocation. The existing mip-hash refresh can intentionally preserve GPU-only mips after
a partial CPU write; treating CpuDirty, a freed heap tag or a partial replacement as a full
texture overwrite would be unsafe.

Validation: 14 real Metal/CPU cases pass with diagnostic counters, including existing scheduler,
scene targets, velocity, two-level BDA, dimensional compatibility and guest lifetime tests.
A separate content test passes with native capture disabled and private inventory enabled.
The private World Warper builder now matches the production linker's header-padding option;
its release-reference instruction hash check remains mandatory and passes.

`tools/macos/analyze_texture_lifecycle.py SNAPSHOT...` compares 2–32 inventories from the same
private process. It separates added/removed/surviving image UIDs, allocation-tag state and
positive write provenance. It rejects cross-directory UID comparisons and duplicate rows,
and treats legacy missing counters as missing rather than zero. Image UID survival does not
imply survival of the same native backing. The tool does not emit a reclamation verdict.

## First native travel inventory and remaining boundary

The repaired private developer app completed one controlled sequence from its frozen chalice
save to Hunter's Dream and then Central Yharnam, with real world teardown/load observed.
Texture backing capacity was 638.88 → 835.65 → 908.87 MiB, and remained 908.87 MiB during the
final stationary interval. This is an initial scene-cache population increase, not sufficient
evidence of a leak or of bounded behavior over repeated cycles. Normal save hashes/settings
were unchanged; the owned game stopped through normal benchmark cleanup. No default rendering
or reclamation preference was changed.

At the final point, 41.58 MiB of texture backings have released heap tags, no current covering
owner and known GPU completion. Major entries were previously depth targets or storage images.
The collector intentionally protects these GPU-written roles; existing sampled-texture
reclamation did run (86 images / 1.29 MiB over the diagnostic). Thus this is a specific retained
resource class, not proof that all frees are missing. Reused/unknown tags remain a separate
problem. Full released-generation exclusion of overlapping live allocations, ordering and
potential cached/native aliases still need to be proved before extending collection. No
writeback to a freed heap payload or discard based only on age/flags is introduced.

Registered zero-backing descriptors can have UID zero. The CSV analyzer counts them explicitly
and excludes them from UID survival comparisons; it rejects unidentified nonzero-byte backings.
It never fabricates an identity from a reused address.

An earlier Old Yharnam boot was stopped by the rapid-swap guard; another attempt reached the
scene but exposed the old embedded runtime in the developer package. Neither yields travel
performance data. After explicit permission to retry once with all protection retained, the
sequence above completed. The native-pattern FPS experiment remains off. Repeated travel,
death/respawn and a performance comparison of any future reclamation change are still pending.

The system's currently allocated swap capacity is not a fixed swap limit. Evaluate pressure,
actual swap growth and free disk space separately. No swap purge, cache clear or threshold
relaxation was performed. Chronod was restarted under the owner's prior authorization after a
955-MiB footprint was measured; normal pressure samples preceded the controlled retry.

## 0052 candidate: released storage-only backing retirement (default OFF)

The first travel inventory's storage-only upper bound is 47 entries / 30.11 MiB. This is
**not** a measured saving: allocation overlap exclusion and exact wall-clock coldness still
require runtime proof. `BB_TEXTURE_RELEASED_STORAGE=1` enables only a new eligibility path;
normal launches retain the original protection by default.

Required gates are a registered GPU-written, single-sample storage image, no GPU-dirty state,
no historical color/depth/video-output role, no depth association, no scene proxy, no bound,
target, pending-rebind or feedback state, and known completion of both last access and write.
The image must have been untouched for at least two seconds. A fully observed heap tag must
have been released for at least two seconds; neither allocator domain may contain any live
allocation overlapping any part of the guest interval. Missing/overflowed history, live/reused
ownership and partial live replacements fail closed. Native color/depth targets and the
previously observed reused-tag population remain protected.

The read-only `ReleasedRangeUnused` preflight shares the same predicate as `WithReleased`.
It avoids breaking a render pass for already-rejected extension candidates. A successful
preflight is never sufficient: `WithReleased` repeats the proof under its allocation lock
through normal untracking, unregistering and deferred image deletion. A concurrent allocation
between preflight and commit is explicitly tested and prevents deletion. Actual extension
hits have a separate bounded-frequency log, not inferred from the generic reclaim counter.

There is no guest writeback, page discard, cache-size change, mip-hash shortcut, new staging
buffer, texture snapshot or GPU wait. In particular this avoids writing old GPU pixels over
allocator metadata stored in a freed payload. Existing view/memo invalidation and deferred
native-object destruction remain in use.

Directed GPU validation uses a real 64×64 storage texture: preserves and reads back its red
GPU contents before retirement; rejects incomplete use, role/alias/rebind hazards and live
partial/address replacements; then proves a real retained Metal view keeps the native
allocation alive until its final reference goes away. CPU tracker tests also prove the
preflight does not report a fake reclaim and cannot authorize a raced allocation. All 15
existing/new cases pass. This does not yet prove repeated gameplay or death/respawn correctness.

`tools/macos/dev/compare_released_storage.py` prepares same-binary ABBA/BAAB or exploratory
AB/BA native-travel comparisons from identical private cache/save seeds. It has fixed Dream /
Central Yharnam cycles, proves real teardown and arrival, waits for settled shader/camera
windows, retains every frame in each score window, and records actual executed-probe hashes.
No GPU capture, content counter inventory, readback or compilation runs during FPS scoring.
The ordinary benchmark's severe-pressure, rapid/total-swap and lock guards remain unchanged.
A suite with no real extension hit explicitly fails rather than claiming a gain. Main-program
scores, snapshots and map-loading periods are not substituted for settled gameplay scores.
The owner explicitly authorized the long experiment. Its first boot was stopped by the
unchanged rapid-swap guard after 43.75 seconds, before any gameplay score (about 650 MiB
additional swap across the final two intervals). That attempt is retained and excluded.
After pressure returned to normal, a fresh isolated ABBA suite completed with the same guards.
No thresholds, resolution, effects or cache budgets were changed. Formal application and data
remain untouched.

## Completed 0052 long comparison and 0053 real-cache integration

The successful ABBA suite ran four serial launches, approximately 350 seconds each (23.4
minutes total). Every launch used the same renderer and actual executed developer probe,
the same frozen private save/settings/cache seed, and two Dream → Central Yharnam cycles.
That is eight complete round trips and sixteen 30-second settled score windows. All normal
save/preference hashes remained unchanged, every owned game process stopped, and no GPU fault
was reported. Pressure reached warning (2), never critical (4); all original rapid/total-swap
and lock protections stayed active. This is bounded travel coverage, not an hours-long leak
test or a death/respawn regression.

Renderer: `781a864be088174a7060e83874ac4a15013415c3d5ea82d4338214e434ecb182`.
Executed probe: `33ad639758eccb26975f28b565622202ec2b03f21a621ac1f4dc7c6e4e4a615f`.
Configuration: frozen native 1080p, MetalFX OFF, all other cohort effects unchanged;
inline updates/fragment uniforms ON, native-clear batching/native-pattern fill OFF.
No GPU profiling, inventory counters, snapshots, readback or builds ran during scored windows.

The table averages the two independent launches of each mode for each matched visit. Memory
is the mean of each window's sampled median process footprint, not allocated texture capacity.

| Settled visit | OFF FPS | ON FPS | FPS difference | Footprint difference (ON − OFF) |
| --- | ---: | ---: | ---: | ---: |
| Dream 1 | 48.44 | 49.59 | +2.36% | +24.1 MiB |
| Central 1 | 47.26 | 46.69 | −1.21% | −32.0 MiB |
| Dream 2 | 49.62 | 50.05 | +0.87% | −30.8 MiB |
| Central 2 | 47.43 | 47.33 | −0.23% | −21.5 MiB |

Central's combined means are **47.35 FPS OFF / 47.01 FPS ON (−0.72%)**, sampled footprint
5958.3 / 5931.5 MiB (−26.8 MiB), owned Metal allocation capacity 1415.1 / 1391.7 MiB
(−23.4 MiB). Average per-window p99 frame time was 23.83 / 26.07 ms, and 1%-low FPS
38.89 / 31.03. These tail results do not establish a causal regression from only two launches
per mode, but clearly do not support a smoothness benefit. Cameras match; Central's mean draw
count span is below 0.05%. Dream 1 varies by 2.71%, adding uncertainty there. Frames within
one launch are not independent repetitions.

The ON launches actually retire 113.71 and 189.28 MiB of backing capacity across their events.
This is summed capacity of destroyed resources, including earlier backing versions, not unique
resident memory saved or a footprint reduction of that size. Other cache population and driver
allocation vary between launches. Repeated travel itself also grows normal working sets in
both modes. These data support actual retirement of eligible dead storage copies, **not a
stable FPS gain or a large guaranteed physical-memory saving**. Keep
`BB_TEXTURE_RELEASED_STORAGE` default OFF; no launcher default or formal binary is promoted.

0053 adds three real `TextureCache` integration invocations (OFF, ON, existing dimensional
view regression), all passing under Metal API validation. Synthetic mapped guest memory and
real GPU pixels exercise the production cache, tracker, view memo and deferred deletion:

- An old released heap tag and cold LRU cannot release an incomplete GPU write.
- A partial live allocation in the other domain prevents deletion even after GPU completion.
- When eligible, removal untracks pages, unregisters the image and advances registry generation;
  the caller drops its old memo as in the rasterizer, and deferred image/view slots disappear.
- A retained native view keeps its base texture alive; after its final reference is released,
  the actual base allocation is destroyed. The OFF case keeps it cached.
- The linear-storage readback queued before deletion is removed, so draining downloads cannot
  write old GPU pixels into the freed heap payload. CPU canaries stay unchanged.
- Reallocation of the same address yields a new image UID and the new uploaded pixel values;
  repeated unchanged descriptors then hit the new cache entry normally.

The additional waits/readbacks above exist only in the standalone synthetic test, after the
game suite exited. There is no new production GPU wait, upload buffer or cache budget. All
53 adapters reconstruct the retained engine source exactly. The previously retained fifteen
compatibility cases cover velocity, two-level addressing, scene targets, cache/view checks and
the scheduler. The new integration tests do not replace continuous enemy-death/respawn or
in-game visual validation; those remain pending before considering any default change.

Compact results, the excluded first-attempt guard record and integration logs are retained in
`outputs/resource-chain-20261021/` of the enclosing workspace. Frozen saves, cache copies,
developer applications and raw run directories remain private outside Git.

## 0054–0057: packet identity, continuous frames and exact allocation overlaps

0054 records the CPU decoder's packet source for the observed 192-byte class. The three
captured frames each contain 35 graphics `WRITE_DATA` origins and 35 matching command writes
to the watched address; none is a constant-RAM dump. Decode provenance is explicitly labeled
`cpu_decode`, because it can run ahead of recording and is not GPU execution order.

The old host-triggered three-member captures have gaps: 5852, 5860, 5867. They cannot exclude
intervening access. 0055 adds an opt-in consecutive encoding-frame chain, bounded to three
frames and the existing independent 8192-record / 2-MiB limit for each member. It starts the
next member on the same boundary that closes the previous one, without a host polling gap.
Each member resets its watcher versions; membership and actual frame numbers are validated.
The live test completes frames 5883, 5884, 5885. Later 0056 and 0057 runs also validate their
three-member chains. The benchmark exposes `--capture-consecutive` with `--capture-native
--capture-count 3`; unsupported binaries, wrong member identities, gaps, display readback and
scored/single-frame modes are rejected. Use `--watch-buffer ADDRESS,192` with the address
derived from the same private cohort; the address below is not a universal game constant.

0056 adds a read-only, allocation-lock-consistent interval inspection. At most 256 live entries
per domain are visited, with explicit truncation and dropped-history flags. It reports the
original tombstone's state separately from the current derived `Reused` state, exact image
overlap counts/bytes per domain, and a covering owner. It changes no retirement predicate,
LRU age, cache budget, synchronization or GPU work. Normal launches do not call this observer.

This resolves an important interpretation error: **`current_owner=0` means no single live
allocation covers the whole image, not that all bytes are unowned.** In consecutive frames
5899–5901, all sixteen reused-tag backings (46.207 MiB capacity) have an observed original
release **and live heap overlap inside their actual image intervals**. Every one lacks a
whole-image covering owner; none lacks partial live ownership. No history drop or diagnostic
truncation is recorded. This is not an unpaired original heap free or reuse only in padding.

For example, image UID 607 has a 15.94-MiB guest interval / 16.06-MiB native backing. It overlaps
34 live heap allocations totaling about 14.91 MiB within that interval. UID 251 overlaps two
live allocations totaling about 15.91 MiB of its 15.94-MiB guest interval. These per-domain
logical overlaps are not a physical-memory union across allocator domains. Combined with the
earlier positive draw/storage-write provenance and partial-mip preservation path, they do not
authorize dropping old GPU-written aliases merely because the original tag was released or
no whole-image owner exists. The existing protection remains unchanged.

The 192-byte write range itself is the prefix of a **live 256-byte heap allocation**, at
`0x7056cb36e0`, generation 1026 in this cohort. Its recorded allocation begins at that address
and stays the same through all three frames; each has 35 all-zero writes. This does not make
its much larger shared native-buffer capacity a 256-byte allocation leak. Requested broad
device-address reads before the first clear are attributable to the game's known buffer-copy
shader `fefebf9f`; no exact dynamic load from this interval has been proved. Version zero is a
frame-local reset, not proof that earlier-frame writes had no consumers. Raw game CPU loads
and actual paged addresses remain a limit on safely eliminating writes.

0057 fills a separate observer gap: Metal occlusion snapshots now report their exact guest
destination and strided geometry (one 8-byte write every 16 bytes), plus a conservative enclosing
interval to the watcher. This changes only metadata. The live 5871–5873 capture contains
10 / 18 / 0 snapshot events with complete destination fields, and **none intersects the watched
192-byte interval**. Thus this particular resolver is not an observed intervening writer in
these frames; do not generalize that negative result to other scenes or all native compute
paths. No query, motion-vector or clear command was skipped.

Validation: the strengthened CPU metadata test verifies packet origins, excluded sizes/inactive
captures, continuous member numbering and per-frame version reset. Eight resource-analysis
tests distinguish absent legacy metadata, partial live owners, edge-only reuse and unobserved
frees; 49 benchmark tests pass, including gap/identity and CLI-mode rejection. The tracker test
also checks partial and cross-domain overlaps, original-free vs replacement evidence, bounded
inspection, overflow and unchanged reclaim counters/policy. All fifteen retained Metal/CPU
compatibility invocations pass with the final diagnostic library, including velocity, two-level
addressing, scene targets, dimensional reuse, GC and scheduler tests. All 57 adapters reconstruct
the retained source exactly. Four short diagnostic launches cleaned up their owned processes,
preserved normal saves and wrote no launcher preferences. They are not FPS comparisons.

No new optimization is enabled: native-pattern fill and released-storage collection remain
default OFF, and all 35 command writes and overlapping GPU-written textures remain protected.
Continuous enemy-death/respawn and in-game visual regression for the storage candidate are
still pending; diagnostics and synthetic tests do not substitute for those gates. The installed
application remains unchanged. Compact results are `packet-origin54.json`,
`consecutive-origin55.json`, `exact-overlap56.json`, `query-overlap57.json` and the matching
test logs under the enclosing workspace's `outputs/resource-chain-20261021/`.

## 0058: indirect arguments and command-processor reads

The shared core's indirect-argument bookkeeping was not itself a native watcher record.
0058 adds records at actual Metal argument-consumption encodes, after unsupported draws have
been rejected: 16/20-byte direct indirect-draw arguments, the same guest argument layout for
indirect geometry kernels, and 12-byte indirect-dispatch arguments. These are the argument
locations supplied to the encoder, not the dynamic draw counts or loaded values.

Packet-level CPU observations also cover graphics/compute memory waits, VO-label waits and
the initial `COND_EXEC` byte read. Register-only waits and fulfilled pending-fence shortcuts
do not falsely become memory observations. Spin loops are not instrumented on every poll;
their exact load count or final observed version remains unknown. The original memory reads,
barriers, waits, geometry, dispatch and render-list operations are unchanged.

A fixed seven-counter per-capture summary distinguishes inactive paths from executed observers
which miss the watched interval, even after the existing general-use record limit is reached.
Missing legacy counters remain missing, rather than zero. No GPU buffer, readback, resource
retention or normal-mode history is added.

The valid consecutive 5892–5894 capture has the following entry counts; **none overlaps the
watched 192-byte range**, and the CPU watcher has no match there either:

| Frame | Geometry indirect argument reads | Indirect dispatch argument reads | Graphics memory-wait entries |
| --- | ---: | ---: | ---: |
| 5892 | 1 | 1 | 0 |
| 5893 | 1 | 1 | 12 |
| 5894 | 1 | 1 | 4 |

Other tracked command-reader categories have zero entries in these frames. This narrows the
observed consumers but does not close raw CPU or actual paged-address coverage. All 35 writes
remain. The CPU metadata and nine analysis tests pass, as do the fifteen retained compatibility
invocations. All 58 adapters reconstruct the current engine source exactly. No new FPS result
or optimization default is inferred from this diagnostic capture.

### Death regression remains unverified

Two separate isolated attempts do **not** constitute a death/respawn pass:

1. A 420-second control session used ordinary movement in the frozen chalice scene and one
   validated native warp to Central Yharnam. It failed to reach a death trigger and expired at
   its existing bound (504.3 seconds including startup/warmup). Its snapshots still show the
   character alive. The warp is not counted as a death, and the owned game was cleaned up.
2. The earlier recorded player route was found and hash-validated. Its original private
   configuration enabled No Dead/Stealth/Silent, so those controls were not reused. A new private
   cohort combined that route's original frozen save with the current rendering settings and
   normal patch configuration, retaining a separately hashed 60-second input excerpt solely
   for an unscored death diagnostic. It stopped during startup at 34.84 seconds, before control
   readiness or playback, under the unchanged rapid-swap guard (about 553 MiB over the final
   6.6 seconds). No route or death result was accepted.

Both attempts preserve normal save hashes and make no launcher preference writes. No guard,
resolution, effect or cache budget was relaxed. A read-only footprint check found chronod at
108 MiB; it was not restarted. No other application was terminated. Candidate storage
retirement remains default OFF. Real death/respawn, exact dynamic copy-shader addresses and
raw game CPU loads remain incomplete gates rather than synthetic-test or map-travel claims.

Evidence: `cp-indirect58.json`, `compat58.json`, `death58-incomplete-attempts.json`,
`route-death58-provenance.json` and `goal-audit58.json` in the enclosing workspace evidence
directory. Input recordings, old frozen saves and newly derived test cohorts stay private.

## 0059–0060: allocation origin and an actual internal-table binding

0059 adds an opt-in watched-allocation caller record after the original allocator returns.
0060 follows at most four frame-pointer records through fault-reporting self-memory reads,
only after the watched address matches and within the existing sixteen-record process limit.
The first return must equal the compiler-provided direct caller; unreadable, misaligned,
backward, cyclic or more-than-64-KiB links terminate the walk. It never scans the stack or
reads resource payloads. Normal mode has no frame reads or retained caller history.

The real watched allocation is aligned to four bytes, 256 bytes, generation 1026. Its four
observed image-relative returns are `0x21d1e96`, `0x11e6736`, `0x11e6997`, `0x11d7375`;
all four are inside executable ELF segments. Static inspection of the user's prepared ELF
establishes the following chain, without copying game code into the repository:

* The original drawing-context initializer requests **192 bytes** at `0x11d7370`.
  Its allocation callback adds 64 bytes before entering the heap allocator. The callback is
  also installed during PPFX initialization; that does not make every user a Yebis object.
* The returned pointer is stored in context field `+0x198`. A setter copies a 16-byte slot
  into the CPU shadow at `+0x1a0` and unconditionally marks `+0x260` dirty.
* Both graphics and compute preparation paths upload the 48-word shadow when dirty, emit
  the existing synchronization, then clear the flag. The graphics path calls the observed
  `WRITE_DATA` builder at `0x11f9c8b`.
* The shader input-usage switch maps **0x1a** to the code at `0x11f80cb` which places this
  pointer into the shader's parameter table. The public
  [fpPS4 definition](https://github.com/red-prig/fpPS4/blob/trunk/chip/ps4_shader.pas)
  names 0x1a `kShaderInputUsagePtrInternalGlobalTable`, an internal driver table pointer.

Thus this is an allocated internal table with a real shader-binding path, rather than an
unowned upload scratch block. A binding does **not** prove an executed read, but it supplies
positive evidence against treating earlier missing watcher reads as dead-data proof. CPU
descriptor-walker loads and the dynamic copy shader remain unclosed observation boundaries.
Unconditional dirty marking suggests a comparison opportunity; equality of the CPU shadow
still does not prove that GPU-visible contents cannot have changed. No dirty flag, upload,
barrier or renderer-side write is skipped.

The new valid consecutive capture is 5905–5907. Each frame still has 35 writes and 34
same-generation equal successors. It has no critical-pressure event, and owned-process cleanup,
formal save hashes and launcher preference non-write checks pass. It is a diagnostic capture,
not a performance score. Native hook tests exercise the actual detours, preserved AVX/GPR/
flags, original allocator execution, bounded/invalid/cyclic frame links and a PROT_NONE frame.
The nine analyzer tests and fifteen retained Metal compatibility invocations pass. An initial
sandbox-only fixture could not enumerate a Metal device; it failed before setup and is not a
game crash or accepted regression result. The host rerun passes with Metal API validation.
All sixty patches reconstruct the current engine source exactly. The installed renderer
remains unchanged and the experimental fill/storage-retirement candidates remain OFF.

Compact evidence: `allocation-callers60.json`, `guest-hooks60-tests.log`,
`analyzer60-tests.log`, `patch60.log`, `compat60.json` and `goal-audit60.json` in the enclosing workspace evidence directory.
Raw stack locations, resource contents, disassemblies and prepared game executables stay private.

## 0061: observe the actual CPU descriptor walker

`BB_CAPTURE_SRT_READS=1` is accepted only with native capture and a bounded watch; scored
comparisons reject it. `RefreshFlatBuf` normally executes the original walker. During an
opted-in recording it can execute a separately generated diagnostic clone, including for
walkers loaded from the existing private cache. The original function pointer, code bytes,
`PersistentSrtInfo` layout and serialized representation stay unchanged.

The decoder accepts a closed set of straight-line integer/stack operations and known 32/64-bit
guest loads. Relative control flow, RIP-relative addressing, segment-based addressing, guest
stores and unfamiliar instructions fall back to the original with an **uncovered** count.
At most 256 entries share one lazily allocated 1-MiB code pool; individual programs/read-site
counts are also bounded. An inline range check precedes each accepted load. Overlapping loads
report address/width, logical stage and shader hash, without copying contents. Flags and live
integer registers are preserved. No GPU command, readback, buffer, cache-size or dependency
policy is changed. The existing zero-register access-violation repair also recognizes clones.

The executable test checks output/canaries, a live carry flag across an observed read,
immediate/indexed and boundary-overlapping loads, unsupported-code fallback, unchanged original
bytes and insufficient code capacity. It also triggers an actual invalid guest read and
checks that the real access-violation dispatcher repairs the clone instead of crashing.
Native metadata tests pass; the analyzer has ten passing cases and the benchmark has fifty.
The entire 61-patch source reconstruction matches.

| Native recording window | Walker entries | Instrumented entries | Uncovered entries | Encoded read sites across entries | Watched table reads |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5906 | 291 | 291 | 0 | 4584 | 0 |
| 5907 | 578 | 578 | 0 | 16313 | 0 |
| 5908 | 1433 | 1433 | 0 | 55475 | 0 |

All three captures are untruncated. These are **recording-window entry counts**, not GPU
timings, successful-load counts or an instruction-complete game history. A walker entered
outside recording, including one straddling a boundary, need not be observed. Other raw game
CPU loads and dynamic GPU aliases are still outside this observer. The zero hit narrows the
consumer search in this cohort; it does not authorize deleting any of the 35 writes or the
associated synchronization. The original live allocation and known shader-binding path remain.

The owned game exits normally; formal save hashes, launcher preference non-write and the
installed renderer hash remain unchanged. Warning pressure is recorded, not treated as a
failure; severe pressure and swap-growth guards are retained. This run is not an FPS score.
Evidence: `srt-read61.json`, `srt-watch61-tests.log`, `metadata61-tests.log`,
`analyzer61-tests.log`, `benchmark61-tests.log` and `patch61.log` in the workspace evidence
directory. Experimental native-fill and released-storage retirement defaults remain OFF.

A further unscored death attempt now reaches control readiness under the unchanged guards.
It verifies the recorded starting camera, plays the private 60-second excerpt and observes
85 seconds after playback starts, taking eight bounded display snapshots. The character remains
near the lamp/wall (maximum observed camera displacement 1.92 m); the snapshots do not establish
death followed by respawn. The control session finishes normally and owned-game cleanup passes,
but its valid harness result is **not a death-regression pass**. Warning pressure reaches 2,
swap changes from 9710.75 to 9706.06 MiB, and no guard is relaxed. The route needs calibration
or another reproducible death trigger before repeating this gate. See
`death61-incomplete-attempt.json`, `compat61.json` and `goal-audit61.json`.

## 0062: physical GPU pointer observations for the copy shader

`BB_CAPTURE_COPY_POINTERS=1` requires native capture and a bounded watch and is rejected for
scored comparisons. A separately compiled, in-memory-only version of the known `fefebf9f`
kernel observes its evaluated physical-pointer and direct-buffer expressions. Every observer
returns the original pointer; shader arithmetic, values and branch conditions are unchanged.
The observer resolves both page-table views of the watched guest range on the GPU, covering
physical aliases as well as the nominal guest address. A mapping flag distinguishes a mapped
negative control from a missing mapping. Source-expression counts are not hardware memory
instruction counts; counters also retain hit flags and explicit overflow information.

The diagnostic uses reserved buffer slots 29/30 and one fixed 32-KiB shared counter allocation
(128 aligned slots). Over-capacity, unsupported, indirect or cross-page cases retain the original
pipeline and report uncovered status. It requires a dual-view table with valid dimensions.
The original shader, MSL cache text and binary-archive entries are retained. Up to eight
diagnostic references are built lazily inside recording, after warmup. Their objects have the
same bounded owners as ordinary shaders/pipelines; the counter buffer is tracked and retired.
Reading counters waits for the GPU at recording boundaries, so **this is never an FPS result**.
During diagnosis only, the compute encoder declares the guest resources needed for the observer.

An initial eager-compilation attempt stopped at 34.46 seconds under the unchanged rapid-swap
guard, before any accepted capture: swap 9241.94 → 10017.81 MiB, warning level 2. It was not
counted as a result and its owned game was cleaned up. Compiling diagnostic references during
startup was removed; the subsequent lazy run completed in 77.94 seconds, maximum pressure 2,
swap 9889.81 → 10043.06 MiB (peak 10091.06). Separate runs do not establish a causal memory
delta. No guard changed. Chronod's measured footprint was 159 MiB; it was not restarted.

| Enclosing native frame | Observed copy dispatches | Mapped in both views | Uncovered | Read hits | Write hits | Overflow |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 5801 | 35 | 35 | 0 | 0 | 0 | 0 |
| 5802 | 35 | 35 | 0 | 0 | 0 | 0 |
| 5803 | 35 | 35 | 0 | 0 | 0 | 0 |

All captures are untruncated; all 105 dispatches have valid mappings. Thus the earlier broad
copy-shader write declarations do not correspond to actual accesses to this 192-byte table
in these windows. This does not prove an absence of raw CPU writes, accesses outside the
windows or relevance in another scene. All original 35 uploads and synchronization remain.
The capture's redundant `frame=0` observation field came from a counter not updated by Metal;
grouping above uses the verified enclosing file headers. Final source replaces that unused
field with the native recorder sequence. The GPU pointer logic is unchanged, and the label
fix is not represented as an additional live capture.

Directed GPU tests cover 32 configurations / 64 baseline-versus-observer dispatches: uint and
float, Safe and Relaxed modes, flat and two-level tables, source reads, destination/mirror
writes, unrelated and unmapped watches. NaN payloads, infinities, signed zero, subnormals,
complete outputs and untouched canaries match. Ten existing private copy-shader MSL variants
also compile after rewriting. The fifteen retained compatibility cases pass after rebuilding
the test executables for the changed C++ layout; benchmark and analysis tests pass (51/11).

A concrete next lead is the existing `ConstantRing`: stage A already copies small read-only
control lists into storage retained until their GPU submission completes. `RingBinding`
identifies the exact slice used by stage B. The old `NoteCopyShaderAccesses` instead reads
the original guest control list. Before narrowing any invalidation or dependency, validate
the immutable slice, the exact bound source/destination descriptors, offsets, formats and
overflow behavior. Missing snapshots or unsupported layouts must keep the broad fallback.
No production scope reduction or reclamation change is enabled by this diagnostic alone.

Evidence: `gpu-copy62-initial-guard.json`, `gpu-copy-pointer62.json`,
`copy-watch62-final-tests.log`, `compat62.json`, `benchmark62-tests.log`,
`analyzer62-tests.log` and `goal-audit62.json` in the workspace evidence directory. The
installed renderer and formal saves/settings/cache remain untouched. Death/respawn validation
still requires a reproducible trigger; the full goal remains incomplete.

## 0063: immutable-copy inspection and rejected scope prototypes

A closer audit corrects an earlier hypothesis: `InvalidateMemoryFromGPU` checks images at
exactly the descriptor base, not all images throughout its advertised size. Broad descriptors
also drive residency-use groups and GPU write clocks; those have different safety contracts.

`BB_CAPTURE_COPY_SCOPE=1` now only inspects a bounded native frame. It reads the exact
immutable constant-ring control slice and actual prepared buffer descriptors for direct
`fefebf9f` dispatches. The observer validates format, stride, bounds, arithmetic and dispatch
shape, constructs interval unions and emits metadata. It does not apply smaller ranges to
residency, dependencies, invalidation, hoisting or resource declarations. The benchmark
requires native-capture mode and forbids scoring this observer. No cache capacity changes.

An envelope-only prototype retained the entire descriptor envelope in all 13 supported
dispatches and was not scored. An interval-union prototype narrowed usage groups and virtual
write clocks: one complete native frame applied 11 paged bindings, 22,115,475,520 declared
bytes versus 706,240 union bytes, and 10,560 versus 28 usage-group touches. These are
bookkeeping counts, not GPU bytes copied or physical memory saved. Same-binary ABBA/BAAB
showed about +1.43%/+1.01%, but the prototype was rejected, never promoted.

A real shared-file alias test using the actual prototype clock and page-coalescing source
found dependency under-recording: aliases 128 MiB apart reference the same 16 KiB backing;
a new write through B is tick 9, while scoped virtual recording leaves A at tick 7. The
original broad recording returns 9 for both. The test reserves its own virtual region before
replacing pages. Initial failed address-hint setup attempts are excluded. This is a synthetic
counterexample, not proof of the cause of an existing game crash. Write-clock narrowing was
removed; future work must include physical aliases, mapping lifecycle and all relevant
writers before weakening dependency coverage.

A use-only prototype kept the original clocks and applied 10 bindings in another full frame:
10,524 versus 25 usage groups, with explicit metadata confirming broad clocks. Its ABBA
attempt recorded a valid OFF baseline but the first ON launch stopped progressing after
initial scene frames and timed out. Later supervised ON/OFF diagnostics both completed,
without enough stalls to trigger thread sampling; they do not establish a causal regression
or a valid performance comparison. Another cold-start capture hit critical pressure and was
excluded; the unchanged-guard warm retry completed.

A CPU cost experiment with the original steady-state NoteUse path and three-cluster synthetic
controls measured approximately 0.34 us for the legacy grouping versus 4.90 us for parsing and
sorting. This is a synthetic CPU comparison, not an FPS result. The use-only path was also
removed. Legacy residency bookkeeping, promotion behavior and complete write-clock coverage
are restored. The old `BB_COPY_RING_SCOPE` score override has been removed.

The observer's directed test enumerates 10,000 layouts and 70,343,578 eligible reads and the
same number of writes. Existing allocation-release retirement 0052 remains OFF. Its pending
death/respawn test is not a prerequisite for the independent work below.

## Rejected 0064 experiment: memoized conservative residency use

The 32-entry, 1,040-byte memo kept full ranges and timestamps and invalidated on idle
demotion, residency initialization, second changes and clock rollback. A completion token
prevented caching across an invalidation. A source-extracted differential replay using the
production interval container matched the legacy method after 30,000 operations / 59,952
NoteUse calls / 4,890 demotion insertions. The standalone memo test covered 100,000 random
operations and collisions, invalidation and wrap. Fifteen native compatibility cases passed.

A same-compiler x86_64 synthetic CPU comparison improved the repeated-large-binding pair
from about 548 ns to 13.4 ns, with an inactive-capture stub. That workload did not predict
whole-game behavior. The normal, same-binary, protected ABBA measured:

| Mode | Independent launches | Mean FPS |
| --- | ---: | ---: |
| Original full-range traversal | 2 | 62.29 |
| Full-range memo | 2 | 60.21 |

The candidate regressed FPS by **3.34%**, with higher measured process memory. Its first-hit
startup log confirmed the cache path was exercised. No resolution, effect, cache-capacity,
shader or GPU-clock change was made. The result does not establish the mechanism of the
regression, or a correctness failure; it is sufficient to reject promotion.

The functional memo, score override, extra BufferCache metadata and shipping patch were
removed. The experiment patch, replay tool, tests and compact measurements are retained
outside the shipping source in the workspace evidence directory `rejected-memo64/` and
`memo64-rejected-performance.json`. The active Metal patch series ends at diagnostic 0063.
The formal app, saves, preferences and caches were not replaced or cleared.

Further work should prioritize actual GPU buffer access and physical mapping. The current
`IsPagedBuffer` predicate marks every descriptor of at least 64 MiB as paged without checking
whether the whole logical range lies in one already-existing native memory source. Inspect
actual binding-time physical contiguity before proposing a pipeline specialization change;
a direct-source opportunity is not yet proven. Physical aliases, mapping changes and existing
hoisting/barrier behavior must remain part of that validation.


### 0063 mapping admission follow-up

The same opt-in `BB_CAPTURE_COPY_SCOPE=1` native-only observer now records binding-time
physical mapping admission after normal trap/upload processing. This is read-only metadata:
`Resolve`/`ResolvePrefix`, no data readback, source allocation, binding change, clock narrowing
or lifetime change. A fixed 256-state dedup table resets per native capture sequence; a
`paged_mapping_overflow` marker makes a bounded incomplete observation explicit. Normal runs
leave it inactive. It is never accepted as a scored FPS run.

One complete native capture (frame 3921, 1,123,576 bytes, native truncation false) observed
14 distinct paged mapping states, all for compute copy shader `0xfefebf9f`: 5 entirely in one
guest source, 9 mixed, none entirely in one mirror. These are deduplicated states, not binding
frequencies. No mapping-observer overflow occurred. Other per-site metadata caps still apply;
this does not establish exhaustive whole-frame dependencies or actual GPU access coverage.

This establishes an opportunity to investigate avoiding unnecessary page walks for certain
copy bindings. It does not justify changing the predicate: shader specialization and binding
must agree, mapping/mirror changes must remain ordered, and guest write-through cannot be
lost when a current GPU view differs. The observer itself makes no performance claim and
no functional optimization has been enabled. Compact evidence: `paged-mapping63.json` in
the workspace evidence directory. Formal app, saves, preferences and existing caches remain
unchanged.


## Controlled native death/respawn follow-up (0063 retained source)

The old route-only attempts remain failures, not retrospective passes. A separate private
probe now samples the local character health chain on the original FlipperUpdate thread,
validating HP/max HP against the independent PlayerGameData mirror. The layout was checked
against local 1.09 instructions and the open-source [ASNH0 GameMemory reader](https://github.com/ASNH0/Bloodborne-shadps4-mod-menu/blob/main/src/GameMemory.cs).
An explicit owner-only private command can call the byte-checked original SetHp routine
at most twice, after five valid stable observations. It does not patch damage/AI/immortality,
change rendering, call a loading shortcut or reuse a stale character pointer. The helper,
command files and rebuilt probe stay outside the shipping repository and formal application.

This is a **programmatically triggered real game death**, not an enemy damage-path test.
Both paired modes prove HP 9 -> 0, an actual world-down/loading interval, a newly created
player, restored positive HP and at least five stable seconds back in the same playable
chalice scene. Initial, first-respawn and second-respawn displays were captured; the ON final
display was inspected and contains the player, wall/floor geometry and normal HUD. Snapshot
readback and inventory overhead mean no FPS score is claimed. Two earlier ON cycles passed
by the same method, for six controlled death/respawns total. Enemy-hit validation remains a
separate boundary; it no longer blocks independent resource analysis.

A serial OFF-then-ON pair uses the same private app/probe, renderer, frozen save/cache/settings,
current quality and guarded harness. Each completes two deaths and four texture metadata
inventories. Every owned process exits normally, no explicit texture-format/validation error
is found, pressure reaches warning but not critical, and formal save/preference hashes remain
unchanged. The guards are unchanged. A sandbox-only controller first failed its process-liveness
check; no HP command was sent. The authorized host controller continued the same live instance.

At final inventory, OFF retains **59 released-tag, GPU-written storage images / 35.02 MiB**;
ON retains zero in that group. ON logs 54 eligible removals / 63.35 MiB summed backing capacity.
Final category snapshots have zero pending-retired bytes, and cumulative released counters
advance through actual Metal destruction. These observations support working safe retirement;
the earlier retained-view test remains the directed proof that references defer destruction.

They do **not** establish a stable resident-memory benefit. Initial cache populations differ
(707.28 MiB OFF vs 637.86 MiB ON), including resources the candidate cannot reclaim. After
two deaths, total cached capacity is 748.25 / 678.54 MiB; final process footprints are
5305.52 / 5567.77 MiB, respectively. That one-pair footprint result does not support default
promotion. The candidate remains OFF; no cache budget, resolution, effects or formal app has
changed. Raw resource capacity is never reported as memory saved.

Further offline analysis of the earlier consecutive 5801–5803 watch traces shows every
shader-identified unknown access names `fefebf9f`; generic `global` records have no shader
identity and are synchronization declarations. Watched records continue after the general
1024-record cap. The already retained GPU-pointer probe covers all 105 observed copy dispatches
with valid dual-view mappings and no watch hit. This narrows the consumer list but still does
not exclude raw CPU accesses, other physical aliases or other scenes/windows; no 192-byte
write or synchronization is elided. Compact evidence: `consumer63-unknown-audit.json`,
`death63-first-success.json`, `death63-paired-audit.json` and `goal-audit64.json` in the enclosing
workspace evidence directory. The full goal remains incomplete.


## Private CPU driver-table and physical-alias follow-up

All additional hooks in this section stay in a private probe outside the shipping repository;
the renderer remains the retained diagnostic-0063 binary. The probe byte-checks the local 1.09
routines before execution, preserves flags/GPR/AVX/x87 state and observes only active bounded
native windows. Preparation and executed `0x1a` case entry have independent hooks. The setter
probe reads at most the original 16-byte source/shadow pair, reports equality/zero-word counts
and the original caller, and leaves the original stores and dirty marking intact. Metadata
never exports those bytes. Hooks have independent explicit record limits; all accepted game
windows remain untruncated without observer overflow or uncovered records.

Three consecutive windows in each of three launches show 71 input-list preparations, all
using the same context/table, with **no `0x1a` input use and no actual case-entry hit**. This
corrects the interpretation of the earlier static binding lead: a binding path exists in code,
but it did not execute in these observed CPU windows. Preparation and GPU encoding are distinct
timelines, so these counts are not a per-command chronology or a whole-game unused-data proof.

The final three-window launch additionally observes **35 setters per window**, all slot 0,
all covered, all supplying 16 zero bytes identical to the old CPU shadow, all entering with
`dirty=0`. Their single observed image-relative caller is `0x11d872b`, through the forwarding
method at `0x11fbb30`. The original setter at `0x11f76b0` unconditionally sets `context+0x260`
after copying the slot. The retained original graphics/compute paths upload the 192-byte
shadow when dirty and clear it afterward; each native frame records 35 actual uploads. Thus
this is a confirmed source of redundant-looking dirty marking, not an orphan allocation or
an inferred renderer leak. Matching counts do not independently pair CPU and encoded events.

Read-only physical admission resolves the table and sixteen previously identified reused-image
intervals completely. Every image interval matches an actual current reused backing in all
three consecutive snapshots; none overlaps the table's physical interval. The table's source
handle and offset also match the actual command-write native buffer and offset. A later setter
capture independently repeats this. These observations rule out direct overlap of **these
sixteen intervals with this table at those observations**, not all aliases or future mappings.
The old textures and table uploads therefore must not be presented as the same-byte root cause.

A tempting global fix, suppressing dirty when CPU shadow data is equal, was **rejected before
any production change**. A directed native test executes the actual user-provided table-address
setter and slot setter in private executable test memory. Rebinding to a fresh table leaves
the dirty flag clear; the original same-value slot setter re-dirties and initializes the new
table, while the proposed equality skip loses that upload. A second context writing a shared
table similarly defeats equality of the first context's private shadow. Only the upload step
is modeled by a CPU shadow copy; this is an API-state counterexample, not Metal execution or a
reproduced game failure. It establishes the missing address/content-ownership proof and does
not assert that this cohort actually takes those paths.

Five private checks pass: preparation-entry preservation, actual-case argument/GPR/AVX/flags
preservation, original-caller extraction and preserved arguments for the setter, positive/
inactive/bounded/reset recorder behavior, and both native setter-state counterexamples.
The actual-case wrapper was corrected to read the saved original r12 for the usage index;
its register test passes before any accepted game capture. All owned games exit normally,
warning pressure stays below critical and original guards are unchanged. No same-shadow skip,
GPU write-clock relaxation, cache-size change or new formal default is enabled.

Evidence in the enclosing workspace: `driver-table63-reference.json`,
`driver-table63-physical.json`, `driver-table63-setter.json`, `driver63-private-tests.json`
and `driver63-dirty-counterexample.json`. Source, game-derived private test bytes, applications
and raw traces remain outside the shipping repository. Raw CPU payload accesses, arbitrary
other physical aliases and other scenes remain explicit limits; the full goal remains active.


## Paired physical-store candidate rejected; validation is not a global gate

A private copy-kernel experiment kept both physical address evaluations and the first store
in their original order, omitting the second store only when its evaluated device pointer
was identical. Distinct views, synchronization, write clocks and resource lifetimes retained
their original behavior. Directed checks passed 112 GPU configurations / 224 dispatches,
six recognized real MSL variants, and sixteen native compatibility cases.

The accepted same-binary ABBA comparison nevertheless measured **61.97 FPS OFF vs 61.35 ON
(-1.00%)**, with essentially unchanged process memory. Both ON launches were lower than both
OFF launches. This small cohort does not establish a causal slowdown mechanism, but provides
no evidence for enabling the candidate. Functional source, patch and override were removed;
only the retained diagnostic-0063 renderer was rebuilt and staged in private QA. The formal
application was not updated. Archived experiment: enclosing workspace evidence
`pair65-rejected-performance.json` and `rejected-pair65/`; no game-derived MSL enters Git.

An earlier attempt was correctly invalidated when an owned caffeinate helper exited between
`poll()` and `getpgid()`. The harness now reaps that specific Popen child on ProcessLookupError;
it does not change pressure thresholds or expand termination authority. A real-child race
regression and the full 53-test benchmark suite pass. That failed historical run remains
invalid; the accepted comparison was rerun after the correction.

Death/respawn validation applies to resource-retirement changes before promotion; it does not
block independent copy/access analysis. Six controlled SetHp-triggered deaths already cover
actual unload/reload and respawn in private saves, with the enemy-hit distinction retained.
A separate minimal Rosetta CPU watchpoint capability probe blocked before its first report
and was terminated/reaped. It establishes neither watchpoint support nor CPU access coverage.
Unknown raw CPU payload accesses remain a boundary; the 192-byte uploads remain unchanged.


## 0066: exact ordered-packet identity and CPU observation limits

Source tracing corrects the old generic `order=cpu_decode` label: graphics WRITE_DATA origin
records are emitted inside `RunWriteData`, which runs as an ordered task (or its immediate
fallback). Compute and constant-engine records occur on their own decode paths. The graphics
source bytes have already been copied from the current decoder input into a TaskPacket.
SubmitGfx may have made an earlier whole-command-buffer snapshot; decoder input must not be
called the original SDK packet without resolving that additional copy.
Pointer equality across those two stages and equal counts cannot establish original identity.

0066 records decoder-input source, copied payload and monotonic queue position at the existing
RunInOrder memcpy, then records queue position/payload at task execution. The origin records
its actual packet pointer; the native write records its existing payload pointer. The task
layout, queue capacity, memcpy, GPU commands, barriers, resource clocks and reclamation remain
unchanged. Metadata filters the 208-byte class (16-byte WRITE_DATA header plus 192-byte data),
dereferences no extra pointers, and uses the existing native-capture bounds. It allocates no
new GPU resource or persistent packet history. The analyzer joins queue plus monotonic position;
it rejects missing copies, payload mismatches, invalid order, stale execution reuse and later
identity collisions. Legacy captures report no identity coverage, rather than zero activity.

The private same-quality game capture, frames **5598–5600**, records 35 copies, executions,
origins and native writes per frame: **105 exact joins**. CPU hook summaries independently show
71 preparations and 35 unchanged zero slot-0 setters per window; those setter counts still do
not identify which setter generated a particular PM4 packet. The general-use list reaches its
existing cap in all three frames. Explicit ordered-packet/CPU-hook records are present and
untruncated, but that cap remains a limit on whole-access completeness. No GPU-completion or
unused-data claim follows from these joins. All owned game cleanup and original save checks
pass; pressure peaks at warning level, total swap grows about 592 MiB during the guarded run.
This is metadata validation, not a benchmark score or a measured optimization benefit.

The library and native metadata test build at -j1. The signed metadata fixture passes,
including fake source/payload pointer values that must never be dereferenced and reused ring
addresses with distinct positions. Eighteen analyzer tests pass. All 64 retained patches,
ending at numbered patch 0066 (0064/0065 functional candidates were rejected), reconstruct the
current engine source exactly. Only private QA applications received this diagnostic renderer.
Formal renderer, preference and save hashes remain unchanged.

A separate native ARM first-touch counterexample confirms the existing page-protection
blind spot on a 16-KiB page: an adjacent byte accessed first unprotects the page and a later
actual target read records no target fault; 192 target reads after a target-first fault record
only one exception. This tests the observation strategy, not actual game behavior.

A private x86 fixture initially stalled before main when unsigned. An otherwise identical
ad-hoc signed clone exits normally. The signed Rosetta page-step fixture observes **384 faults
and 384 single-step traps**, covering all 192 byte loads and 192 byte stores with unchanged
results. It establishes single-thread byte-access capability only. Concurrent threads while
a page is temporarily open, SIMD/RMW span decoding, shared mappings, aliases and integration
with runtime signal handling remain unverified. The LLDB attempts never returned a coverage
result; hardware-watchpoint limits are not inferred from their startup failures. Two earlier
unsigned helper processes remain kernel-waiting despite termination signals (8 KiB RSS each at
last check); they supply no results and will not be reused. No system debug permission was
changed. Source and binaries of these private fixtures stay outside the shipping repository.

Evidence: `packet66-live.json`, `driver66-analyzer-regression.json`,
`cpu-access66-tool-boundary.json` and `formal66-check.json` in the enclosing workspace evidence.
Next: resolve SubmitGfx command snapshots before associating the SDK upload with decoder-input
packet identities, and
validate multithread/alias-safe CPU observation before making any write-elision claim.


## 0067: submission snapshot source mapping; pass-preserving feasibility boundary

The graphics submission entry passes the API-provided dcb/ccb spans unchanged to SubmitGfx.
SubmitGfx's existing SubmittedCopy keeps the API pointer and its copied data range alive during
decoding. 0067 reuses that metadata at each 208-byte WRITE_DATA packet before task queueing:
checked range arithmetic maps decoder offset back to API input without reading original guest
contents. Invalid source/translated ranges remain null. Indirect/direct decoder input is
labelled separately from a submitted snapshot. No packet content, snapshot lifetime, buffer
layout, GPU command, write clock or resource policy changes.

The analyzer consumes the submission mapping when the decoder packet is copied, before that
storage can be reused while stage B still processes the previous task. Missing, invalid and
ambiguous mappings stay explicit. Directed tests include such reuse, exact last-byte boundary,
source and translated integer overflow, unmatched/legacy records and direct-input distinction.
The native metadata fixture passes after ad-hoc signing; 21 analyzer tests pass. All 65 retained
patches through numbered 0067 reconstruct source exactly. Library/fixture build uses -j1.

The accepted private capture, frames **5560–5562**, has **105/105 joins** through API-input
packet, submitted snapshot, decoder input, task copy/execution and actual native write.
All use SubmittedCopy. API packet addresses recur between first and third frames in distinct
submissions; this is observed address reuse, not proof that those original contents changed.
The SDK builder caller still requires a separate association. Each frame retains the explicit
general-use-list overflow; exact identity rows are present and untruncated, but no whole-access
absence follows. Cleanup confirms the owned game stopped and saves/preferences were unchanged.
Pressure peaks at warning; swap grows 278.93 MiB. This is diagnostic, not FPS measurement.
Formal renderer, save and preference hashes again match the preserved baseline.

A separate original 8x8 Metal prototype tests retaining all writes inside a render encoder via
a `vertex void` function with rasterization disabled. A matching-format pipeline and a single
pipeline without attachment formats both pass API validation on this M4 for **the narrow case
with no earlier fragment access to the destination**: all 192 bytes and outside canaries match,
all 64 subsequent pixels read the new data, and visibility query samples remain 128 for two
ordinary fullscreen draws. No new game pipeline is installed or enabled. The capability is
consistent with Apple's [rasterizationEnabled documentation](https://developer.apple.com/documentation/metal/mtlrenderpipelinedescriptor/israsterizationenabled).

An initial broader prototype tried waiting for prior fragments within the same pass and was
rejected by Metal API validation. Apple's [in-pass synchronization documentation](https://developer.apple.com/cn/documentation/metal/synchronizing-stages-within-a-pass)
confirms that Apple TBDR GPUs cannot wait for fragment/tile work via an intra-pass barrier.
Such dependencies must preserve the pass switch. IsBufferAccessed alone is insufficient to
prove eligibility: AccessGlobal reads only set memory_pending and are not tracker read entries.
Physical aliases, depth/stencil, MSAA, held/replayed command state and actual game performance
remain unvalidated. No old barrier is removed, narrowed or replaced on the strength of this
prototype. A safe candidate needs a conservative eligibility rule and original fallback.

Evidence: `packet67-live.json`, `packet67-reuse.json`, `render-update67-feasibility.json` and
`formal67-check.json` in the enclosing workspace. Prototype source/binaries stay private.


## 0068: bounded per-pass fragment conflict diagnostics

A diagnostic-only guard keys on canonical native Metal buffers, including fragment buffer
bindings and buffer-backed texture/attachment parents. Bounding ranges may over-report overlap.
Raw-address/synthesized fragment shaders, missing draw classification, unknown texture heaps,
invalid ranges and more than 64 resources mark the pass unknown. Metadata is under 2 KiB per
RenderList and allocated only when native capture plus BB_CAPTURE_FRAGMENT_GUARD=1 are selected;
no GPU resource, command, write clock, cache capacity or synchronization policy changes.

A same-pass transition initially exposed a blind spot: ReleaseHeldPass replayed commands and
called BeginDirect, which reset the new metadata. The first live capture is retained but excluded
from safety decisions. ContinueDirect now preserves only the access guard while performing the
same existing direct-mode/filter transition. TakeRecording copies guard state to its replay list;
new-pass Begin calls still reset it. Actual direct/recorded/moved-threaded/continued-direct tests
verify these histories and every output pixel on both ARM and x86, under Metal API validation.
Default runtime and scheduler regressions pass. A separate exact-interval model checks 1,024,000
queries without false negatives; unknown, capacity and boundary cases fail closed. Analyzer and
benchmark tests pass 22/54, including rejecting this diagnostic in scored comparisons. All 66
retained patches through numbered 0068 reconstruct current engine source.

The corrected private capture, frames **5565–5567**, has 35 observed updates per frame. Every
update follows one classified draw with one or three native-buffer fragment resources; none
observably overlaps the destination. These are 105 potential opportunities, not authorization
to alter GPU commands. General-use-list overflow remains explicit. The owned game exits cleanly,
formal save/preference hashes remain unchanged, and the guarded capture peaks at warning pressure
with 16 MiB swap reduction (not a memory-savings measurement). Formal application is untouched.

The extended original offscreen no-raster prototype passes all four combinations of 1x/4x MSAA
and depth/stencil. Earlier vertices must read the old destination into a witness buffer before
the update; subsequent constant-address-space fragments read the new words. All 192 bytes,
canaries and 64 pixels match, depth/stencil equality tests remain valid, and visibility samples
are exactly 128/512 for the two ordinary draws. A single no-attachment-format host pipeline is
used. This is private feasibility, not integrated game correctness or FPS evidence.

Next functional candidate must preserve every write and existing clocks, reject unknown aliases
or fragment conflicts, honor render tails/hoists and incoming fences, keep resource rebinding
correct in direct/held/replayed passes, and pass actual on/off comparisons before promotion.
Evidence: `fragment68-live.json`, `fragment68-validation.json`, `fragment68-first-unaccepted.json`
and `formal68-check.json` in the enclosing workspace. Raw private prototypes stay outside Git.


## Rejected 0069: no-rasterization in-pass buffer writes

A default-OFF candidate preserved every update and existing write clock, executing <=256-byte
updates with a no-rasterization vertex function in an existing guest render pass. Admission
required Runtime-owned canonical native buffers, valid bounds, complete classified prior
draws and no fragment overlap; unknown/raw/heap access, overflow, tails/clears and hoists
retained the original compute/staging fallback. Incoming fences and vertex barriers remained.

Actual Runtime tests covered ON/OFF x direct/held modes, payload snapshots/canaries, prior
vertex and fragment witnesses, constant fragment consumers and all output pixels. Safe cases
hit the actual Runtime counter; conflicts and two wrappers sharing one imported chunk fell
back. Replay, same-pass continuation, full Runtime/Scheduler Metal-validation regressions,
and 54 benchmark / 22 resource-analyzer tests passed. The final three-frame game capture
5258–5260 confirms 35 preserved 192-byte writes per frame, all using the candidate with no
observed fragment conflict. General-use overflow remains explicit.

The same-binary guarded ABBA nevertheless measured OFF **56.36 FPS** versus ON **51.50 FPS**
(**-8.63%**). Individual OFF runs were 56.22 / 56.50; ON runs 51.61 / 51.39. Both ON runs
were lower than both OFF runs. The fixed save is the existing chalice cohort, not Central
Yharnam. No GPU profiling/capture ran during scored windows; quality, cache capacities and
all other selected flags were unchanged. Lower process-memory medians in the ON group are
not treated as a causal memory saving. All four owned runs exit with unchanged normal saves.

The candidate removes observed compute encoder work, but earlier/later diagnostic captures
do not show an appreciable reduction in render encoders. The precise vertex/synchronization
slowdown mechanism is unproven. Fewer copies/encoders alone do not justify enabling it.
Functional source, patch and override were removed; private source/tests/provenance are
archived outside the shipping repository under rejected-render69. Retained engine source
remains the 66-patch series through 0068. Formal application is not replaced.

Evidence: render69-live.json and render69-rejected-performance.json in the enclosing workspace.
The repeated writes remain intact. Raw CPU/other-alias coverage limits still prevent claiming
that the all-zero updates are dead. Resource-retirement death/respawn verification stays
separate from copy/scheduling experiments; its six earlier controlled cycles do not block
further independent analysis.


## 0070 / 0071 bounded consumed-clear investigation — 2026-10-22

0070 records exact old/new per-slot state differences only during bounded native captures.
Physical render-pass analysis keeps queues separate, rejects clipped metadata and distinguishes
native views, dimensions, slots, load/store actions, submission ticks and compute/blit breaks.
Logical state-change requests are not counted as physical encoder savings.

Three complete diagnostic frames (5521–5523) show two kinds of differences:

- A 4096-square shadow depth clear and the 1920x1080 G-buffer clear become loads. The
  old depth value still contains float 1.0 bits while the new unused value is zero.
  `WithoutClears` clears flags but retains that stale value, so raw state comparison fails.
- Depth/stencil layouts change between attachment/read-only roles. These are kept as real
  boundaries; the current candidate does not ignore layouts or feedback roles.

The initially default-OFF `BB_MTL_CONTINUE_CLEARS` candidate only continues an already-open guest
clear pass into an identical load state. It requires exact views/layouts/dimensions and known
private, non-heap, non-buffer attachment backing, excludes sampled parent-view aliases,
unknown shader metadata, hoisted work, barriers, pending clears and render-tail work. The
remembered state becomes the same load state a separate loading encoder would have had.
It creates no GPU resources, changes no cache limit, removes no write and advances no clock.
A successful directed test does not establish real-game hits or a performance benefit.

Validation completed so far:

- State policy: 20 checks including captured nonzero depth clears, all attachment/role
  changes, fresh clears and later identical clears.
- Metal API validation, direct and held/replay modes: exact colour readback, preserved
  depth/stencil, a later fresh clear, sampled parent-view alias exclusion, raw shader
  metadata exclusion, pending tail, intervening compute, barrier and layout exclusions.
- Real visibility counters: the continued pass and subsequent fresh pass each preserve
  16 visible samples. Their sum agrees with the unmerged first/failed-depth/fresh draws.
- Eight physical-transition analyzer tests and 54 benchmark/guard tests pass.

Private warmed frames 5675–5677 each hit the candidate exactly twice: the 4096-square
shadow pass and the 1920x1080 G-buffer pass. Both consumed-clear split requests disappear;
other view/layout changes remain. Capture is valid with complete owned-process cleanup and
unchanged original saves. Same-binary ABBA is running without native capture or GPU timing
in its scored windows. No FPS benefit is yet established and the flag remains default-OFF.
The installed application is unchanged.


0071 same-binary ABBA completed on the frozen chalice cohort (not Central Yharnam):

| Metric | OFF | ON |
| --- | ---: | ---: |
| Mean FPS | 56.41 | 57.54 |
| Individual runs | 56.24 / 56.57 | 57.30 / 57.78 |
| Mean 1% low | 45.60 | 44.68 |
| Mean frame-time p95 (ms) | 19.305 | 19.150 |
| Mean process-footprint median (MiB) | 5486.54 | 5488.24 |
| Mean owned-Metal median (MiB) | 1100.89 | 1180.24 |
| Mean Metal-allocated median (MiB) | 5908.59 | 5988.03 |

Average FPS increases 2.01%; both ON runs exceed both OFF runs. 1% lows do not show a
benefit. The footprint difference is +1.71 MiB, whereas owned/allocated counters differ by
about +79 MiB and vary substantially across runs, including the OFF group. No GPU objects,
retains or cache capacities are added by the candidate; these counters alone do not establish
a causal allocation increase or saving. Both groups have warning-level samples throughout,
no serious-pressure event, and no scored swap increase (OFF 0, ON -8 MiB per run).
All four owned processes stop with unchanged original saves and no launcher preference write.
This is evidence of a small scene-specific average-FPS gain, not a universal 60-FPS claim.

Full Runtime and Scheduler tests pass with Metal API validation and the flag enabled.
Two automated private controlled-death/respawn cycles completed with independently observed
HP-zero, loading/world transitions and stable positive HP after both respawns. No fatal/Metal
assertion was recorded, and the owned supervisor/driver exited successfully. These are
SetHp-triggered cycles, not enemy-hit combat tests. The default was then enabled in development
source and direct/held native checks passed again without any enabling environment variable;
`BB_MTL_CONTINUE_CLEARS=0` remains an explicit diagnostic opt-out. The 68-patch series is
idempotent. The private QA renderer is updated; the installed application is unchanged.
All three captured frames still execute all 35 observed 192-byte command writes. Unobserved
raw CPU/alias accesses remain a boundary against deleting those writes.
Evidence: pass71-native.json and pass71-performance.json in the enclosing workspace.


Final current-state hash check confirms the installed renderer, launcher preferences and all
15 normal-save files match the pre-investigation snapshot. Test control/data/cache paths are
private; no formal cache reset or replacement was performed. Temporary build metadata/header
recovery used the pinned dependencies and existing libraries. Incremental CMake compilation
and the recorded native tests pass; this is not represented as a new clean-source release build.


## 0072: observed SDK producer through publication-time snapshot

An independent private hook now observes the original WRITE_DATA builder after it has selected
its actual output packet address, before its unchanged header/payload stores. Its original
caller, context/table/shadow relation, 48-word class and dirty byte are checked. Setter and
builder records have process-monotonic identities and actual host-thread IDs. Game-derived
hook bytes, private probes and original-function fixtures remain outside the repository.

A native fixture executes the user's original builder, with and without the private hook:
two modes produce identical complete packets, outside canaries and final write pointers.
A second fixture checks the observer's caller/context/table/class/inactive admission. The
first game launch was correctly rejected by these checks because the observer accidentally
used a base initialized only by optional World Warper. Its 105 uncovered records remain an
unaccepted capture. A separate base is now set when the private hooks install, independently
of World Warper; the directed fixture verifies that independence before the accepted run.

Generic adapter 0072 adds metadata immediately after the existing submission memcpy, before
draw preparation/queue delay. Each snapshot publication gets an observation identity plus its
existing native and API-input ranges. Decoder metadata reports the same native range. No copy,
payload, queue layout, shader, memory clock, barrier or reclamation rule changes. Inactive or
empty streams do not publish identities. Fake pointers, integer overflow and inactive calls
are tested without dereferencing payloads.

The analyzer freezes observed SDK identities at this copy boundary, then consumes them at
ordered-task copying. Thirteen directed checks cover original/snapshot/ring reuse, a rebuilt
packet after publication, overlapping construction, exact range ends, missing/unknown-thread
records, invalid pointers, wrong destinations, duplicate identities, overflow and frame gaps.
Existing 22 resource, eight pass-transition and 54 benchmark checks pass. The signed native
metadata fixture passes, and all 69 retained adapters are idempotent.

Accepted consecutive frames **5500–5502** each contain 35 covered setters, builders and actual
writes: **105/105 observed associations** through same-thread setter -> SDK packet -> API-input
snapshot generation -> decoder input -> task copy/execution -> native write. All setters have
unchanged zero slot-0 input; dirty-before is zero and the subsequent builder sees dirty one.
Thirty-five original packet addresses recur with distinct builder/snapshot identities; e.g.
builder 1/71 belongs to snapshot 16/116, rather than one timeless pointer identity. Both
consumed-clear continuations still hit in every frame. No native/SDK observer overflow or
capture truncation occurs; the existing general-access-list cap remains a whole-read limit.

Current physical/lifetime admission also rechecks all 16 original intervals against actual
current backings in all three frames: 46.207 MiB total, all physically disjoint from this table,
all with released original tags and current partial live overlaps. All have derived Reused
state, no single full covering owner and completed GPU ticks; 14 still carry GpuModified.
None meets invalid-backing release admission. Thus the released-image collector first protects
these images at its Reused-tag gate; GPU completion alone does not make the data dead. The two
non-GpuModified small copies account for only 256 bytes of backing capacity. They are not an
explanation for the large retained group.

The SDK association is observed causality, not proof that no other CPU writer, alias or GPU
consumer exists. Raw CPU payload access and arbitrary dynamic aliases remain incomplete.
The native API-state counterexamples still prohibit global same-shadow dirty suppression.
All 35 writes, dirty flags and synchronization remain intact; 0052/0049 remain default-OFF.
No new FPS claim follows from this diagnostic capture. The validated 0071 result is unchanged.
Owned cleanup succeeds, serious/swap/lock guards stay intact, and formal data is untouched.

Evidence: sdk72-live.json, sdk72-first-unaccepted.json, sdk72-private-tests.json,
sdk72-current-physical-lifetime.json and validation72 in the enclosing workspace.


## Private CPU observer capability follow-up — 2026-10-22

No game was launched in this phase. A signed standalone Rosetta fixture tests four registered
threads and two initial MAP_SHARED views of the same temporary backing. The intentional
unprotected-window control lets bystanders run only after the first fault opens the page:
only 1 of 193 byte reads and 0 of 192 writes are observed despite correct final data.
Single-thread trap-flag success therefore did not establish multithread completeness.

With other registered fixture threads suspended only across the replayed instruction, the
same byte workload records 388 faults/traps: all 193 target reads, 192 target writes and three
neighbor-page accesses. All 1,164 suspend/resume calls balance. The Rosetta fixture reports
4-KiB emulated pages; the earlier native-ARM first-touch fixture reports 16 KiB. Do not mix
the architectures' page-size observations.

The observer uses the pinned Zydis decoder to evaluate whole supported memory-operand spans,
not just the reported fault byte. Directed cases cover an SSE load/store beginning outside
the 192-byte payload, an SSE operation crossing a page boundary, and atomic XADD read/modify/
write spans. Counts describe overlapped potential bytes per decoded operand, not hardware
transactions or scalar instruction counts. Unsupported REP instructions stop the observer,
restore fixture permissions and resume every owned suspension; they produce incomplete
coverage rather than zero accesses. No game instruction is skipped or rewritten.

The final 85-case stress matrix passes: 60 supported positive cases, 20 deliberate unsupported
stops and five race-negative controls. Both aliases' entire surrounding mappings are checked
for unintended modification. The standalone source and results remain outside the shipping
repository under validation73 in the enclosing evidence workspace.

The mechanism uses the SDK's [thread_suspend](https://developer.apple.com/documentation/kernel/1418833-thread_suspend)
and matching resume API only on the fixture's own registered workers. It is not yet integrated
with the game: thread creation, remapped/copy-on-write aliases, original protection restoration,
GPU dirty-page/readback callbacks, runtime locks and nested fault recovery remain open gates.
Source inspection confirms probe.c resolves bbgpu_handle_fault before speculative recovery;
runtime_memory.c centrally composes independent trap reasons under runtime-lock -> trap_mutex.
A new observer must preserve this order/ownership rather than blindly granting read/write or
suspending a worker required by GPU readback.

A separate hardware alternative uses the debug-state layouts in the local SDK and checks
[Arm's control-field layout](https://documentation-service.arm.com/static/649ae5b238511951cb799288).
Native ARM range-mask tests trap at sampled offsets 0/128/319/511 and not 512; the ordinary
8-byte control traps at 0 and not 8. This is not a Rosetta result. Rosetta's public ARM-state
wrapper aborts with unsupported-thread-state flavor; a direct-MIG native control exits -9
without an accepted result. Neither path is installed in the game, no permission is changed,
and these failures do not prove hardware watchpoints are universally unavailable in Rosetta.

Evidence: cpu73-observation-capability.json and validation73. This phase adds no rendering
optimization or new FPS result; 0071's controlled result is unchanged. Formal data is untouched.

## CPU observer / actual runtime protection composition — 2026-10-22

This bounded follow-up includes the actual, unmodified `src/runtime_memory.c` in a signed
standalone Rosetta test, with the existing low-heap implementation and pinned Zydis library.
It exercises two fixed guest aliases through the real mapping table, trap-reason composition,
guest permission changes and `runtime_memory_write_backing`. Four registered data threads
and a fifth controlled readback worker participate; no game or Metal renderer is launched.

The test-only observer owns reason 128. It removes/rearms only that reason under the existing
runtime region lock -> trap_mutex order and recomputes the protection from the original VMA
permissions plus all remaining owners. Readback requests are resolved before owning these
locks or suspending other fixture threads. The callback worker is an emulation of a dependency,
not evidence that actual Metal callback lock/scheduling behavior has been tested.

The first unsupported-REP test exited 110: peers had already received protection exceptions
when another thread canceled observation. Their pending exceptions subsequently saw no
observer reason and were incorrectly rejected. The private test now permits a retry after
abort only if the actual VMA permission and every remaining trap reason allow the access.
It does not grant blanket read/write or treat a denied write as successful. Observation
remains explicitly incomplete after cancellation; instructions still execute normally.

The final 20-case matrix repeated three times passes 60 expected outcomes:

- 45 supported complete observations across scalar, SSE edge/cross-page and atomic spans,
  with combinations of existing Mirror, Image, QueryReads and VramData reasons;
- 12 unsupported-instruction stops preserving both aliases' data and surrounding bytes;
- three negative controls deliberately reproducing the generic mapped-read retry loop,
  then terminating observation without claiming read coverage.

The 21 read-only cases retain original permissions, including after unsupported instructions.
Write-only reasons remain until a deliberate denied-write probe invokes their normal owner.
Every case clears only the observer bit, preserves applicable other reasons and balances its
owned suspend/resume calls. The corrected tests observe 13 pending exceptions after abort.
Source inspection separately verifies why the old ordering is insufficient: probe.c invokes
the GPU handler first; page_manager's generic read path calls RasterizerCore::ReadMemory,
which returns true for mapped memory even without a GPU read-trap reason. A new independent
read observer would therefore need explicit dispatch ownership; this phase changes neither
production dispatch nor runtime reason definitions.

Game integration is not established. The fixture deliberately excludes accesses while holding
runtime locks, dynamic thread/map/unmap changes and the permanent unprotected backing alias.
The latter intentionally bypasses guest GPU traps for uploads/readbacks; observing only guest
VMAs cannot establish all host reads/writes. These are admission gates, not absent consumers.
No upload suppression or additional resource retirement follows from these synthetic results.

Evidence: cpu74-runtime-protection-composition.json, validation74, goal-audit74.json and
formal74-after.json in the enclosing workspace. The installed renderer, launcher preferences
and all 15 normal-save files still match the preceding formal hashes. Formal caches were
not accessed by the fixture. No new performance claim, source renderer patch or app update.

## Observer admission counterexamples — 2026-10-22

CPU75 closes a bounded 13-case admission matrix using the same actual runtime source. A new
shared alias performs a load/store while the two original aliases still report all expected
193 reads and 192 writes. Comparing the runtime mapping generation rejects this false whole-
window completeness; an unrelated protection change is also conservatively rejected.

Three transient-thread controls create/join an unregistered thread within the temporary open
page interval. Its load/store produces no protection fault, the original counters still match,
and the mapping generation does not change. The fixture-managed thread population returns
to six; these are fixture counts, not a macOS task census. A private create epoch marks the
sample incomplete. No global/native thread creation interception has been installed.

Two more controls use the actual backing read/write APIs or the raw backing pointer. Both
bypass guest VMA traps with unchanged mapping generation and otherwise matching counters.
The experiment explicitly rejects whole-host completeness; its operation count is known by
construction, not a production host-read observer. Source inspection finds the raw pointer
used by Metal's no-copy guest buffer import. The installed probe and Metal library are both
x86_64 and run in process; remote Vulkan GPU-process stubs are not the active implementation.

Four nested runtime read/exclusive-lock cases cancel only CPU observation and retain the
caller's original lock depth, data, unrelated protections and balanced suspensions. The cases
with existing Image write protection are covered; nested locks plus a no-access GPU reason
and real readback dependencies are not established. The unchanged control remains complete.

These results prevent unsupported absence claims, not guest accesses. Guest create hooks alone
also miss native copy/pipeline/IO/service/videoout threads found in the source. The next bounded
game evidence can identify a positive first-access consumer without thread freezing or TF;
such partial sampling cannot replace the full read/lifecycle coverage required for elision.
All original writes, dirty flags, synchronization and retirement policy remain unchanged.

Evidence: cpu75-observer-admission.json, validation75, goal-audit75.json, formal75-after.json.
All 13 expected outcomes preserve data and surroundings. No game launched or FPS claim added;
formal renderer, preferences and 15 save-file hashes remain unchanged. Formal caches untouched.

## Bounded first-attempt probe in the game — 2026-10-22

CPU76 uses a private copy of probe.c and the existing private SDK observer. The first valid
48-word builder in the native capture arms reason 128 on the target's guest page. A handler
records one attempted access, removes only that reason, then either retries under actual
guest permissions or falls through to the original GPU/speculative-recovery path. It neither
single-steps nor suspends threads. It does not claim successful instruction completion, whole
operand coverage, unknown aliases, host backing coverage or absence of other reads.

31 actual-runtime fixture cases pass: existing trap combinations, original read-only denial,
nested locks, neighbor-page traffic and concurrent pending faults where a peer needs the
caller's original runtime write lock. This last case permits either pending handler to remove
the same observer bit, without waiting for the other handler to release an original lock.
Binary diagnostic events are bounded and carry no game resource payload.

The first game attempt stopped before arming: the unchanged rapid-swap guard detected growth
from 10,325.75 to 11,008.88 MiB in 6.33 seconds. Cleanup confirmed owned process termination
and unchanged original saves. Pressure returned to normal. chronod's SDK rusage footprint
was 313.70 MiB, so it was not restarted; a footprint CLI attempt returned no data and only
that owned diagnostic was terminated. No guard threshold or unrelated app was changed.

One bounded retry succeeds with the same guards and frozen private cohort. Frames 4783–4785
are untruncated; each preserves 35 target writes and all 105 SDK/publication/decoder/task/native
joins match. The one page fault is a write at target+832, on the arming host thread, with
only the observer bit present and unchanged mapping generation 286. It is outside the target.
The same RIP in a signed Rosetta helper resolves to the shared-cache system memmove variant;
this lookup is not a game caller-stack capture or a full write-span proof.

The retry's sampled swap ends 24 MiB below its starting sample, pressure stays at warning or
below, and maximum logged footprint is 5,550.40 MiB. These are diagnostic run conditions,
not a performance/memory A/B result. The first neighbor write consumes the page sample, so
no target CPU-read absence follows. Host backing paths and the actual copy caller/span remain
open evidence work. All original writes, dirty/sync behavior and retirement defaults remain.

Evidence: cpu76-first-access-status.json, validation76, goal-audit76.json and formal76-after.json.
The private app is outside the source repository; private game hooks are not shipping content.
The formal renderer, preferences and 15 normal-save files retain their hashes; formal caches
were not used. No new renderer optimization, formal install, release or FPS claim in this phase.

## Neighbor copy / physical host-backing provenance — 2026-10-22

CPU77 extends the private first-attempt probe with registers, scoped guest memcpy/memmove/
memset context, an initialization-time loaded-image map, and completed read/write-backing
spans filtered by actual physical overlap. The hooks do not read resource contents. Event
output is bounded to 4,096 records; the observed run has 127 and no overflow. No production
source or renderer is changed by these private runtime copies.

A compatibility correction is necessary: the original runtime prepare-CPU-write predicate
reacts to any trap bit, so CPU76's diagnostic-only reason 128 could cause an extra GPU
invalidation hook. A directed test of the original predicate reproduces one extra call;
the private CPU77 predicate excludes only 128, makes zero diagnostic-only calls and retains
the real Image reason's call. 32 first-attempt/copy/physical-alias cases also pass. CPU76's
first-fault location remains observed; its resource/performance behavior is not a pure
noninterfering control. No shipping runtime uses this diagnostic bit or predicate change.

The isolated CPU77 run records a guest memcpy return PC at game+0xc814f3, destination
target+832, source 0x702a7007a8 and size 256. The actual game image map matches the signed
helper's shared-cache image base. With recorded registers, the faulting system instruction
decodes to `mov [rdi], r12`, an 8-byte store at target+832. Static examination confirms the
preceding five-byte call; the enclosing guest function has no established symbolic name.
Ten scoped copies (five 256-byte and five 16-byte) return normally and their declared virtual
ranges do not overlap the target. This is not an arbitrary physical-alias exclusion.

By the last status event, the new backing hooks count 2,296 completed mapped write spans,
zero read spans and zero physical target matches. Hooks are placed after each successful
mapped copy; zero-fill gaps, retained raw backing pointers and direct Metal/GPU access are
not covered. Last counters are a sampled prefix, not a completion fence or an absence proof.
Source audit also finds imported Metal buffers expose no mapped-data span to generic buffer
copies; Runtime::ImportBuffer returns coherent-only state, while owned buffers return a CPU
mapping. GuestMemory's raw host pointer is passed to the no-copy Metal import and startup
self-test. This source distinction does not establish all future CPU/GPU consumers.

Frames 4774–4776 remain untruncated with 35 target writes each, all 105 SDK/native joins
matched. Their 213 covered SDK preparation declarations contain no usage-1a entry; that is
observed declaration metadata, not a general proof that no shader or raw pointer can read.
Maximum logged footprint is 5,396.71 MiB, pressure <= warning and sampled swap +44.5 MiB.
These are diagnostic conditions, not a performance A/B or an allocation-regression claim.

Full read coverage stays incomplete and no upload is suppressed. A next admissibility question
is whether complete CPU/GPU write-version invalidation can prove unchanged contents without
assuming absent reads. It would still need guest alias/mapping lifecycle, host backing writes,
unknown GPU writes, rebinding and the existing SDK counterexamples; no candidate is enabled.

Evidence: cpu77-copy-backing-provenance.json, validation77, goal-audit77.json and formal77-after.json.
Owned game cleanup succeeds; formal renderer, preferences and 15 normal-save hashes are
unchanged. Formal caches are not used, reset or replaced. No install/Release/FPS claim added.


## CPU78: write-certificate rejection and an actual query/gather invalidation bug

The write-only certificate prototype is **not admitted** and is not linked into any game.
Its 15 directed cases include 13 supported outcomes and two negative admission witnesses:
legacy `runtime_memory_gpu_protect` can lift the observer's read-only permission without a
mapping/CPU epoch change, and re-composing observer permissions can expand a legacy NOACCESS
mapping. Neither a successful alias fixture nor a trap count licenses eliding game uploads.
Two native hardware fixtures (arm64 and Rosetta x86_64) also show that real Metal GPU blits
write a shared no-copy buffer while all three CPU aliases remain read-only. This is measured
M4 behavior, not a universal API guarantee. Apple documents shared CPU/GPU access and requires
proper synchronization: [shared storage](https://developer.apple.com/documentation/metal/mtlstoragemode/shared).

An independent GPU-side gap is fixed by retained patch 0073. `OcclusionQueries::Snapshot`
uses its own ordering and directly dispatches writes to a guest buffer. It bypassed the
Runtime access hooks, leaving BufferWriteEpoch unchanged. A native test now exercises the
**actual GuestReadSnapshots/GatherGuestRead code**: cache a zero-filled source, Snapshot four
valid query words in the same packet/tick, then look up the cached source. Both old query
chains return the stale cached copy (zero), while a fresh ordered gather contains the valid
bits. With the notification, lookup rejects the old entry and fresh bytes match all 64 bytes.
This establishes a cache correctness bug, not merely a missing counter observed in isolation.
The permanent test runs before the regular occlusion test, with separate query object lifetime
so its scheduler visibility callback cannot interfere with the subsequent test.

The production implementation adds one existing epoch increment after the dispatch. It does
not add GPU commands, resource allocations or barriers. It conservatively invalidates the
existing bounded same-packet memo; no cache capacity is reduced. This covers one identified
external guest-buffer writer and is not a proof that every writer is accounted for.
`BB_MTL_QUERY_WRITE_EPOCH=0` is a developer reference control, accepted by the benchmark CLI;
the default is fixed behavior. No frontend option to disable this correctness fix is added.

Four private-cohort runs (OFF/ON/ON/OFF, each 30 seconds warmup and 30 seconds score) all
completed cleanup under unchanged critical-pressure, rapid-swap, total-swap and lock guards.
They used the same packaged binary, settings hash, frozen save fingerprint and camera. The
cohort is a chalice scene, not Central Yharnam. Group means were 57.8424 versus 58.6797 FPS;
the reference itself varied by 1.7012 FPS, so the observed +1.4476% is not a proven gain.
Median-footprint group means were 5474.12 versus 5376.65 MiB. Resource-retention differences
were present from startup and the tested game window did not log a cross-chunk gather; no
causal memory-saving or real-game stale-hit claim follows. Retain this as a demonstrated
native correctness fix, not a speed or memory optimization. Full Runtime suites with both
query chains and the permanent exact-byte memo regression passed, as did 54 benchmark tests
and 70/70 patch idempotence. No gameplay re-test was run just for the added regression.

Evidence: workspace `cpu78-write-version-and-query-fix.json`, `validation78/query-memo-results.json`,
`validation78/runtime-memo-suite-results.json`, `validation78/query-abba-summary.json`, and
`formal78-after.json`. The installed renderer, preferences and 15 formal save files are
unchanged. Formal caches were not cleared or replaced. Full raw CPU/alias and other-writer
coverage remain incomplete; all 35 observed uploads and synchronization are preserved.


## CPU79: protection ownership inventory and stale VMA reapplication

Source inventory distinguishes guest kernel protection, canonical per-reason traps, legacy
AddressSpace GPU protection, pool-host aliases, VRAM diagnostic traps, FreeCheck label traps,
guest detour installation, owned executable stubs, and loader image protection. These owners
are not interchangeable. In particular, a present `BB_VRAM_ACCESS_TRAP` enables its direct
mprotect path even when its string value is `0`; future observation admission must reject
that case until ownership is composed. Source inventory is not a complete dynamic access trace.

A separate existing correctness bug was reproduced in real `runtime_memory.c`:
`protect_locked` applied the requested host protection then `trap_reapply` called trap_apply,
which used the **old** VMA permissions. Under a write-only trap, NONE→READ remained unreadable,
and READ/RW→NONE incorrectly remained readable. The fix passes the uniform newly requested
host protection into trap reapplication, applying each trap as a further restriction. It keeps
the original host-protection/carve/metadata order, so it does not introduce a new unprotected
window on a carve failure. It adds no allocation, GPU work or cache policy change.

The durable, game-free regression can be run from the repository root:

```sh
python3 tools/macos/test_memory_protection.py --output /private/tmp/protection-results.json
```

It builds the actual runtime with the real low heap, signs/verifies the executable before
execution on macOS, and uses signal recovery to check actual data access permissions. It
requires Clang and, on Apple Silicon, Rosetta for the x86_64 runtime. Each case checks the
combined trap, removal of one owner, removal of all owners, current guest permission metadata
and the neighboring page. The identical final fixture has 13 failures against the original
runtime and 0 failures across 75 cases against the corrected runtime. Six cases explicitly
validate that an INVALID result from macOS refusing a new EXEC permission leaves the original
permissions intact. CPU instruction execution is not tested; unsupported elevation is not
silently counted as successful elevation. Early test assertions demanding that elevation
succeed are preserved in validation history rather than hidden.

This fix is development source only; no gameplay or performance claim is made for it yet.
It does not resolve legacy GPU protection bypass or the raw diagnostic mprotect paths, and
no CPU-write certificate, upload skipping, or additional texture retirement is enabled.
Evidence: workspace `cpu79-protection-ownership.json`, `validation79/`, `formal79-after.json`.
Formal renderer, preferences and 15 save files still match the previous snapshots. No game was
launched during this phase and no formal cache was cleared or replaced.


## CPU80: private legacy-owner composition

A private (not public/game-integrated) runtime prototype uses two previously unassigned bits
in the existing trap table for the legacy GPU owner's read/write revocation state. One locked
mask replacement removes only those bits and adds the new legacy state atomically, preserving
canonical GPU and observer ownership. It allocates no second page table. A separate policy
level preserves EXEC for a legacy no-read request, matching the old API's bit policy, while
canonical noaccess traps continue denying all access. Real instruction execution is not tested.

All 15 original write-certificate fixtures pass with the two known negative admission cases
now inverted: a legacy RW request cannot lift observer RO, and arming cannot expand legacy
NOACCESS. An independent expected-permission fixture (not using the implementation's trap
level helper) checks 648 old/new guest permissions, original GPU/observer masks, four legacy
read/write requests, and both call orders. Each checks setup, guest mprotect, removal of one
canonical owner, legacy release/reapply, removal of all canonical owners, full release, and
neighbor permissions. The current public reference fails 290 cases; the private prototype
passes all 648. This reference already includes CPU79's new-permission reapplication fix, so
the result isolates the still-uncomposed legacy path rather than re-counting the old VMA bug.

A second fixture holds the observer reason throughout while two threads repeatedly change
legacy and canonical GPU ownership, with a third execution context attempting CPU writes.
Three reference runs allow 51, 36 and 90 writes despite the observer bit still being present.
Three private-composition runs each attempt at least 5000 writes and allow zero; both the
observer-retained and all-owners-released final states are correct. These stress runs validate
owner composition only: observer arm/cancel races and the real GPU fault dispatcher remain
unverified, as do actual executable mappings and raw diagnostic mprotect coexistence.

Evidence is in workspace `cpu80-legacy-protection-composition.json`, `validation80/`, and
`formal80-after.json`. No game was launched. Public runtime source remains exactly the CPU79
version; private code is not in a QA app. All 35 uploads/synchronization remain. No performance
or game-memory improvement is claimed. Same-page neighbor writes still conservatively
invalidate the certificate, so composition correctness alone is not an upload optimization.

## CPU81: known neighbor writes and pending-fault rearm

The CPU77 raw copy events establish two shapes: five 256-byte writes at table+832 and five
16-byte writes at table+368. All are disjoint from the 192-byte payload but share its 4 KiB
page (table offset 0x6e0; offset 0x36e0 within 16 KiB). Replaying each shape through a guest
alias and the host backing alias, each 100 times, gives 400 unchanged payloads and 400 coarse
certificate invalidations. This is a required conservative fallback, not a byte-precise write
proof. It does not show a game hit rate or prove the candidate has no value between writes.
The CPU producer callback order is retained separately: it is not a GPU execution/completion
fence and cannot be used as a physical GPU timing model.

A directed observer lifecycle fixture holds a CPU fault just after its fast invalidation,
re-arms on the other thread, then resumes the older fault and completes the CPU write. The
existing second epoch/active invalidation under the trap lock correctly invalidates the new
certificate in all 32 trials. A negative control removing only that under-lock invalidation
leaves a valid certificate after completed data modification in all 32 trials. Thus the
under-lock step is necessary; the test is not merely counting faults. This uses gated known
aliases, not arbitrary concurrent mapping replacement, cancellation, or actual game dispatch.

All code is private, signed before execution, and remains outside public runtime/game apps.
No game was launched, no upload was deleted, no cache capacity or quality was changed, and
no FPS or memory-saving claim is made. Public runtime remains CPU79's version. Evidence:
workspace `cpu81-lifecycle-and-neighbor-fallback.json`, `validation81/`, `formal81-after.json`.
The compact status document has been consolidated into current decisions; detailed chronology
and negative controls remain here and in the evidence files. Formal application/preferences
and the 15 normal saves still match the preceding snapshots; formal caches were not modified.


## CPU82: mapping boundary and private observer inherited-lock deadlock

Eleven directed native cases cover unmap/bit forgetting, rebinding a new physical range,
cancellation preserving a new image trap, readonly alias replacement, an unrelated physical
rebind followed by rebind back, guest noaccess, writes while holding an exclusive lock,
cross-page 192-byte writes, split-VMA rejection, late aliases, physical release, and backing
writes with an inherited read lock. Five negative controls omit mapping-generation validation;
each wrongly retains a valid certificate. These qualify the need for mapping versions, not
all possible concurrent remaps.

A further pending-writer test exposes a real deadlock in the **private observer**, which has
never been linked into a game. A thread holds the runtime read lock, a second thread confirms
the writer lock is busy and waits for it, then the first thread faults on the watched host
alias. Re-requesting the read lock in observer invalidation blocks; the independent process
exits via its 12-second alarm (124). No game was started or killed by this test.

The private fix reuses existing thread-local runtime read/exclusive ownership for invalidation
and reads the VMA directly under inherited ownership in guest fault validation. Otherwise it
acquires/releases its own lock as before. Outer depths are preserved. Thirteen mapping/lock
cases (including both pending-writer host and guest alias faults) pass, as do all fifteen
previous write-watch cases. This does not prove arbitrary signal/mapping lifecycle or real
GPU fault dispatch. Subsequent private builds must use CPU82's
`runtime_write_watch_inherited.c`, not the older CPU80/81 watch.

Evidence: workspace `cpu82-mapping-and-inherited-lock.json`, `validation82/`, and
`formal82-after.json`. Public runtime remains CPU79's version. No GPU/resource policy, game
quality, cache capacity, installed app, preferences, or formal saves/cache were changed.
No performance improvement is claimed; the work qualifies a bounded observe-only probe,
not upload elision. GPU write ordering and retained/reused texture consumers still need proof.


## CPU83: accepted observe-only CPU producer certificates in the isolated game

A signed private QA probe now uses CPU82's inherited-lock watch and CPU80's composed legacy
policy. It observes at most 128 matching SDK builder callbacks / 256 JSON records, reading no
target payload and performing no upload elision/reclamation. Five native wrapper tests qualify
init/normal invalidation, reject raw VRAM diagnostics even with value 0, reject FreeCheck/label
trap coexistence, and enforce the record/call limit. It is not a production build dependency.

The first isolated start was stopped at 43.6 seconds by the unchanged rapid-swap guard before
any target producer sample. Cleanup stopped its owned process and preserved formal saves and
preferences. After pressure returned to normal with no game running, one retry used the same
frozen cohort and settings. Its swap-total cap was reduced to 1070 MiB so the cumulative
2 GiB batch allowance was preserved. The retry accepted three consecutive frames 3902–3904;
all 35 writes/frame and 105 SDK/publication/task/native joins remain intact. No global trace
truncation occurred. No game pixel contents were read back.

The CPU-only observer records 105 producer callbacks: first arm, 102 valid prior certificates,
and two invalidated/rearmed at calls 2 and 72. Last producer counters show two prepare-write
invalidations and zero write faults, stable mapping generation286 and one registered guest
alias plus the host backing protection. This is not a post-exit counter fence. All recorded
builders share one host thread and match counts/target, supporting ordinal association in this
window only; a generic multi-thread identity claim would require explicit IDs. No GPU-unchanged
condition is proved, and 102 is not a count of skippable uploads.

Each frame has 19 unknown write observations: shader/hoisted shader0xfefebf9f and global
fallbacks, all logical watched version0. Source-log order before the first write is not a
native GPU completion proof. The existing general-use audit also overflows its1024 entries;
watched-table and SDK provenance continue, but complete general resource coverage is absent.
The analyzer initially rejected every overflow together; the retained report explicitly
qualifies these general-use overflows rather than hiding them or claiming complete coverage.

The retained group is refreshed in every accepted frame:16 exact backings46.207MiB,14 with
GpuModified, all Reused with partial live allocation overlap, all physically disjoint from
the small table. No new reclamation is licensed. Highest retry pressure was warning, sampled
peak footprint5482.62MiB, maximum cumulative batch swap growth1149.94MiB. These are conditions,
not a before/after memory saving or FPS score. Both runs' cleanup succeeded; no remaining game
was found and post-test pressure returned to normal. Formal renderer, preferences and15 saves
still match; formal caches untouched. Evidence: workspace `cpu83-observe-only-game.json`,
`validation83/` (including the first guard stop), and `formal83-after.json`.


## GPU84: same-window producer certificates and copy pointer counters

The current signed private CPU83 package was run once with both COPY_POINTERS and COPY_SCOPE.
The unchanged guards used only1010MiB of remaining swap allowance from the CPU83 initial
9626.56MiB baseline. Three accepted frames3776–3778 have35/35/36 recognized copy dispatches;
all106 have a matching scope record, mapping3 (both current/guest BDA views), positive static
read/write sites, no uncovered marker or counter overflow, and zero overlapping read/write
hits. Capture sequence1/2/3 is checked separately from game frame3776/3777/3778; confusing
these identifiers is an analyzer error, not a bad capture. The third frame's extra dispatch
is retained rather than assumed to be a duplicate. GPU counters complete at the native
boundary via EndEncoder/JoinReplay and Finish, before reading the fixed32KiB counter buffer.
No resource pixels or small-table contents are read back by this diagnostic.

CPU-side producer observation in that same window gives103 valid prior certificates and one
prepare invalidation (rearm at call37), plus the first arm. All105 SDK/publication/task/native
writes remain. This is not103 safe skips: the producer interval is distinct from the later
ordered native encoding/execution interval. Copy pointer zero hits cover the recognized
source expressions and two selected BDA views only, not arbitrary extra physical aliases,
hardware instruction counts, or unobserved windows. General-use audit overflow remains.

Source review explains the generic declarations: RasterizerCore::BindResources emits a
shader storage-use declaration for a paged bound buffer, then Runtime::AccessGlobal marks
pending barriers and advances its version. AccessGlobal itself dispatches no GPU store. All
six generic declarations in these frames immediately follow a0xfefebf9f storage declaration
with consecutive use sequence. The19 unknown records per frame must not be read as19
independent stores. No barrier/cache scheduling change is made from this association.

Immutable control-list scope admission succeeds for31 dispatches and fails conservatively
for75; the prior rejected narrowing is not revived. WriteDataOnGpu may update both an optional
CommandWriteMirror and the CommandWriteTarget. Each UpdateBuffer snapshots payload with
setBytes or staging and orders its compute/blit work; complete elision proof must account for
both destinations, native identity/offset changes, CPU mutations through execution, and
held/hoisted scheduler order. Stable CPU shadow plus one destination is insufficient.

Maximum sampled footprint5525.12MiB and pressure warning; cumulative swap peak1230MiB above
initial remains below2GiB. These are diagnostic conditions, not comparative memory or FPS
results. All owned processes stopped and post-run pressure was normal. Formal renderer,
preferences and15 save hashes match; caches were not cleared or replaced. Evidence: workspace
`gpu84-pointer-and-cpu-joint.json`, `validation84/`, `formal84-after.json`.


## RACE85: reject host-precheck-only write elision

A private native execution bridge was compiled as one replacement object/library without
editing the public engine. It records WriteDataOnGpu before/after mirror/target updates, but
was not staged into a QA app or run. Before extending that experiment, a more fundamental
counterexample was tested using the real private CPU page observer and real Metal shared
memory/event ordering. No game/save/settings/cache is involved.

One MTLBuffer and one physical backing are reused. For each reference/elided case, a prior
GPU fill of192 bytes0xA5 completes, then the observer is armed and its certificate is valid.
A second command buffer waits on a shared event before either performing the original fill
or omitting it. After commit but before releasing the GPU event, the CPU writes0x6B to the
same destination. The actual observer correctly invalidates the certificate. The original
GPU fill then yields0xA5; the elided path leaves0x6B. All192 bytes are checked. This excludes
native identity change, different initial GPU contents, and a missed dirty notification as
explanations. It demonstrates the unclosed check/execute gap: a late notification does not
retroactively restore an omitted command at its original GPU stream position.

The candidate is therefore rejected, not enabled experimentally. The trace does not claim
this scheduling race happened in the real game, or that every write-elision algorithm is
impossible. A proof that the game cannot perform those writes under exclusive GPU ownership
could exclude it; current sampled CPU certificates do not establish that interval. More
encode-point-only validity counts would not qualify the same candidate. All uploads, their
synchronization and previous native correctness changes stay intact.

Fixture history is retained: the initial helper attempted no-copy Metal buffer creation after
CPU read protection and received nil. The final helper creates the buffer first, reuses it
for both cases, seeds the same completed GPU payload each time, then arms observation. This
is a fixture setup correction, not a general API guarantee about read-only imports.

Evidence: workspace `race85-precheck-elision-rejected.json`, `validation85/race85/` and
`validation85/native85/`, `formal85-after.json`. Public engine source, installed application,
preferences and15 save hashes are unchanged; formal caches untouched. No game or FPS run was
performed this phase. Full resource ownership/alias and retained-texture consumer/lifetime
coverage remains incomplete. Continue there and favor consolidation that preserves stores,
rather than resuming a precheck-only deletion candidate without a stronger ownership model.


## TEXTURE86：实际缓存部分 mip 所有权反例与预算核对

`GpuModified` 是历史来源标记，不是“全部最新数据只在 GPU”的证明。实际
TextureCache 与 Metal 像素测试表明：建立 CPU mip 哈希后，GPU 改 mip1、CPU 改 mip0，
原路径只刷新 mip0，保留 mip1。若在这次刷新后清除 GpuModified，下一次 CPU 局部更新
会把旧 CPU mip1 重新上传，覆盖有效 GPU 内容。每个有效像素均已检查，Metal API
Validation 开启，合成测试退出 0；不涉及游戏文件。不能据此放宽失效纹理回收条件。

CPU83 三帧各 16 张对象的 flags 分布均为 74×8、65×2、73×5、75×1：全部有 Dirty，
14 张有 GpuModified，按实际 `SafeToDownload = GpuModified && !Dirty` 判定为 0 张。
这不代表可以直接丢弃，因为 Dirty 可能只来自某个 mip 的局部修改。LRU 年龄
1146–1752 的单位是提交 tick，不能解释成秒或帧。全部仍有部分活分配重叠。

同一次运行的四条预算日志中，调整后图形用量约 384–1128 MiB，动态预算约
8547–10726 MiB，明显低于 70% GC 触发线。它们解释了采样时保留缓存的合理原因，
不是这些对象泄漏的证据；预算日志与逐帧纹理日志并非每条同步，不能扩大为全程结论。
没有缩小缓存，也没有修改回收逻辑。

另发现一个已有边界：初始 Dirty 包含 GpuDirty，首次上传会跳过 mip 哈希建立。
没有后续建立哈希的刷新时，同样的局部 CPU 更新会重传未改 mip，合成测试已复现。
当前 engine HEAD 中也有该分支，非本次观察器新增。修复前需分清初次 CPU 上传与
GPU 修改过的缓冲区来源，不能直接给 GPU-dirty 路径增加任意 CPU 读取。
这尚未证明是游戏丢墙或闪退的原因，不作为性能收益。

证据：工作区 `texture86-lifetime-and-mip.json`、`validation86/`、`formal86-after.json`。

完整测试源码、构建命令及签名摘要归档在 `validation86/`。最初无 GPU 的沙箱执行退出 23；
主机上首次像素断言暴露初始哈希边界，后续将其保留为第三项已知行为对照，没有把它
误记为修复通过。最终正例、清标记负例和初始哈希边界均通过其各自逐像素断言。

## TEXTURE87：第一次写入通知不等于完整修改范围

实际 TextureCache/Metal 测试已复现：先建立两级 mip 的 CPU 哈希，再由 GPU 清除 mip1。
只向纹理缓存报告 mip0 的 CPU 写入后，`InvalidateMemory` 调用 `UntrackImage`，
整张图变为未监视；随后 CPU 可以再改 mip1，而不必产生第二次纹理失效通知。
真实 UpdateImage 的完整哈希检查发现两级均改变，GPU 读回分别精确等于 0x33/0x66。
测试使用合成映射，底层保护回调是空实现；`IsTracked` 状态与原缓存代码证明取消整图
监视这一边界，不冒充真实故障处理器测试。Metal API Validation 开启，退出 0。

因此拒绝仅依据第一次失效范围生成 mip 上传掩码；那会遗漏后续 mip1 写入。
不能用它替代完整哈希，也没有因此新增 CPU 读取或改变缓存预算。

首次上传的实际来源分支也已核对：LayerBind 的镜像或原地缓冲、游戏物理块、GPU 修改
过的 arena、预上传 arena、以及可能异步执行的 CPU staging。两级 mip 合成测试的
同步 CPU staging 情形不能代表这些来源；目前没有为修复初始哈希而给全部分支增加
任意 CPU 读取。既有的所有上传、同步及 GpuModified 标记均保留。

证据：工作区 `texture87-untracked-mip-range.json`、`validation87/`。

## CLEAN88：当前源码组合的两次死亡重生回归

重新以当前公开 probe/runtime 源码（包含 CPU79 权限修复）构建私有 QA 主进程，
搭配已验证的 0071/0073 renderer。没有接入私有 CPU 写入观察器或 SDK 产生链探针；
只保留开发用 SetHp 驱动，最多显式触发两次。包及实际执行文件均已签名、严格验证，
正式应用未替换。两次均观测到 HP=0、加载/世界卸载、恢复正 HP 和稳定可操作状态，
控制器与监督进程退出 0，日志没有捕获到断言或 Metal 命令错误。不是敌人攻击测试。

四份原生纹理清单（死亡前、第一次重生后、第二次重生后、闲置 15 秒后）容量为
638.880→708.762→708.777→708.777 MiB，描述符数 623→802→803→803。
第一次约多 69.88 MiB，第二次仅多 16 KiB，最后没有变化。初始 602 个 UID 仍存在，
其最终容量约 566.11 MiB；UID 保留并不保证同一 Metal 背板一直保留，也不是实际驻留量。
有限两次循环没有显示线性累积，不能替代长测或宣称不存在泄漏。

此次可见 57 个 Released-tag 对象约 46.43 MiB，其中 storage-only 历史角色为
51 个、约 30.94 MiB，GPU tick 均完成。它们是后续精确准入核对的候选，CSV 没有
完整的锁内重叠、年龄、记录丢失及背板绑定证明；不能直接当作已安全回收的内存。
0052 仍关闭，缓存预算与回收逻辑未改。

最高内存压力为警告级，批次累计交换峰值相对原始 9626.56 MiB 基线增加约
1493.75 MiB，未越过既有 2 GiB 上限；快速增长、严重压力与锁屏保护保持不变。
测试游戏已正常清理，退出后压力读回正常，正式 renderer、设置及 15 个存档哈希未变。
本轮没有新 FPS 结论；静止场景也没有覆盖运动速度图工作负载。

证据：工作区 `clean88-combined-death-regression.json`、`validation88/`、`formal88-after.json`。

## ADMISSION89：回收准入与物理别名的只读核对工具

已私有编译并签名准入专用 renderer，新增的 CSV 列仅在 BB_TEXTURE_AUDIT_DIR 诊断
请求时输出：标签覆盖、记录丢失/截断、两个分配域的活重叠、2000ms 锁内只读预检、
ReclaimReady、实际 storage 回收谓词、两秒闲置、收集器状态和背板数量。
生产源码、回收逻辑与缓存预算均未修改，0052 仍关闭。

真实 TextureCache 合成测试配合 Metal API Validation 通过三阶段检查：
GPU 未完成时 safe_storage=0；GPU 完成但有 32 字节 Range 活重叠时 unused_2s=0；
释放该重叠后，unused_2s/safe_storage/cold_2s/reclaim_ready 均为 1。
这些是预检值，实际删除必须再次执行 WithReleased 的锁内证明，不能把 CSV 当许可。

私有 QA 驱动通过实际运行时 sceKernelVirtualQuery 获取有界 VMA 清单，在自己的
游戏更新线程上调用，避免持有纹理锁再进入运行时映射锁。前后映射代次必须一致且为偶数，
最多 8192 个 VMA、4 次请求。真实运行时测试建立两个共享物理区间的不同游戏虚拟映射，
快照正确保留两份；查询途中修改保护、导致映射代次变化的负例被拒绝。沙箱内共享池分配
被系统拒绝，主机运行签名助手通过；没有以失败的沙箱结果声称验证成功。

离线分析器有 8 项通过的测试，覆盖实际准入状态、真实别名数据、代次拒绝、跨 VMA
连续覆盖和缺口等情况。它要求同一隔离 run 及相同序列，拒绝缺列、重复非零 UID 和
映射缺失；任何结果均标明“不授权未来释放”。记录中的额外游戏物理别名会拒绝候选；
这不是任意主机指针/未来新别名已排除的证明。

实际 ADMISSION89 游戏采集尚未开始。本轮先新增 1200 MiB 启动余量门槛，再尝试恢复
512 MiB 并启动时被自动审批拒绝：当前交换约 10664 MiB，上轮单次增长约 1192 MiB，
审批认为降低门槛存在卡死风险。被拒命令没有执行，门槛仍为 1200 MiB；原始批次基线
9626.56 MiB 和总增量 2 GiB、严重压力、快速交换增长、锁屏停止条件均未改变。
完成离线工作后的读回仍只有约 990 MiB 可用运行增长额度（含 20 MiB 余量保留），
未用新批次基线、短跑或间接方式绕过拒绝。实际 51 张候选的完整准入记录仍缺失，
没有将 CLEAN88 的 CSV 补猜成许可，也没有启用任何新回收。

正式 renderer、设置、15 个正常存档再次哈希核对未变。证据：工作区
`admission89-readonly-and-start-gate.json`、`validation89/`、`formal89-after.json`。

## GUARD90：未改启动线的实际尝试与重复预算限制

上一轮等待期间交换占用自然回落；新的启动读回为 9463.81 MiB、压力正常，
满足未修改的 1200 MiB 启动准入。此前下调启动线的例外不再需要，也未使用。
原签名 ADMISSION89 包实际启动一次，仍关闭0052；启动43.7秒时被监督器停止，
没有进入准入采集，未产生目标 CSV/VMA 快照。清理成功，无游戏进程残留，
正式 renderer、设置及15个正常存档哈希再次未变。不是游戏崩溃或兼容性结论。

实际触发项是临时脚本保留的1550 MiB单轮增长上限：起点9463.81、最后观测峰值
11183.19，单轮增加1719.38 MiB；相对原批次9626.56基线则仅增加1556.63 MiB，
尚未达到原先2 GiB批次上限。检查按周期采样，因此停止读数可能超过触发线；
不能把这个1719.38 MiB读数冒充2 GiB批次限制被触发。脚本叠加限制导致这次提前中止。

只读源码核对还确认：FreeImage先取消监视、注销并增加registry_generation，再延后销毁
视图和背板；DeleteImage会移除下载队列项。目标memo、图像描述符memo、TextureSet
及helper准入都检查注册代次。storage-only谓词排除绑定、target、rebind、general、
proxy、depth、MSAA和历史RT/depth/VO角色。这是源码证据，不证明任意外部原生指针
或未来虚拟别名都已受控；既有原生缓存测试另有注册失效及持有视图的覆盖。

已准备但未执行的预算修正提案：保留原批次基线和2 GiB总额，单轮上限改为剩余批次
额度（最高2 GiB），并预留256 MiB采样缓冲；保留1200 MiB启动准入、严重压力、
快速增长和锁屏保护。删除额外1550 MiB常量，不重置批次或增加总额。
7项离线测试及数百个余额点的边界检查通过。按实际启动点计算，提案上限为1954 MiB，
故不会在上述1550线提前停止，但不证明后续加载能在预算内完成，也不承诺零瞬时超调。
提案目前仅在仓库外的review-only文件中，不被现有启动器或测试脚本引用，没有自动重试。

证据：工作区 `guard90-overlapping-limits-and-source-audit.json`、`validation90/`、
`formal90-after.json`。51张storage候选的实际完整准入和别名证据仍缺，回收仍未启用。


## 2026-10-22: retained alternatives and bounded admission retry (92–94)

The owner authorized the guarded run and requested that safe, uncertain/slower alternatives
remain selectable. Patches 0074/0075/0076 restore the former 0064 full-range residency memo,
0065 same-physical-pointer store merge and 0069 guarded in-pass updates, all OFF by default.
They retain the prior performance-negative results as evidence rather than treating one M4
cohort as proof of universal futility. These supersede only the earlier implementation-removal
decisions; the historical measurements and correctness limits above are unchanged. Native
clear batching, merged overlay and repeated-byte native fill also gain default-OFF front-end
controls. Consumed-clear continuation remains ON. See METAL-OPTIMIZATION-CONTROLS.md.

The original aggregate swap baseline and 2 GiB limit were retained. Admission92 entered the
frozen chalice scene without hitting a memory/session guard, then completed its initial
texture CSV. The diagnostic used a symbol name where the actual runtime resolver accepts
scoped NIDs; the simpler earlier fixture accepted names and missed this integration error.
The controller therefore finished only its owned session, before requesting any deaths.
No FPS or successful death/admission result is accepted from that run.

Private admission93 fixes the call to the scoped VirtualQuery NID. A new fixture extracts the
actual runtime.c import table and resolver, proves the old symbol-name request fails, then
validates two real guest aliases and rejection after a mapping-generation change. This is
not yet a successful in-game inventory. Its subsequent game preflight had only 797 MiB
usable swap-growth allowance after the fixed aggregate budget/reserve, below the unchanged
1,200 MiB startup gate, so it launched no process. No budget baseline was reset.

Current source exposes 32 controls (21 ON / 11 OFF), with dependencies, explicit environment
values, import of existing developer choices and English/Simplified/Traditional descriptions.
Wrong-result algorithms remain excluded. Formal application, normal saves and settings are
hash-identical to formal90. No formal cache was cleared or modified by these tests.


## 95: actual runtime + tracker physical-alias boundary

A new reproducible CPU-only diagnostic combines the real runtime memory implementation,
production NID lookup/import table and real lifetime Tracker. Two distinct guest VMAs map
the same direct-memory physical interval; writes in each appear in the other. The synthetic
Heap tag at A is released while the tag at B remains live. Even after the production two-second
grace, InspectRange(A) reports no virtual overlaps and both ReleasedRangeUnused / WithReleased
permit the metadata callback. No texture is created/freed and no game state participates.
This proves the virtual ownership predicate does not exclude physical aliases; it does not
prove such aliases exist in the current game population or establish visible corruption.

Reproduce with `python3 tools/macos/test_retirement_alias.py --output NEW_PRIVATE_DIRECTORY`.
The generated helper extracts the actual runtime.c lookup rather than the permissive earlier
stub, compiles/signs/verifies the x86-64 fixture, and keeps all memory synthetic. Source:
`tests/test_retirement_alias.cpp`; evidence: workspace alias95-retirement-boundary.json.

The actual admission92 initial snapshot has 623 images, 137 passing the storage-state portion,
26 reused tags, but zero Released tags / zero unused_2s intervals. None may be reclaimed from
this snapshot even without the missing mapping inventory. Do not join it with a prior process's
post-death UIDs. The 51-object post-death group from clean88 still needs its own mapping proof.

0052 remains OFF. Before any enablement, physical alias exclusion and mapping stability must
hold through actual retirement, alongside the existing allocation/texture locks. A read-only
mapping snapshot or generation comparison before deletion is not that transaction. Source audit
also finds FreeImage -> UntrackImage -> runtime page protection/write-watch callbacks. The
current write-watch path updates atomics and the protection path takes nested read locks;
this audit does not establish a read-to-write upgrade in FreeImage. A new mapping lease must
still prove nested-lock behavior, ordering and nonblocking admission, with no GPU wait under
mapping locks. No new retirement path, smaller cache or speculative readback was enabled.


## Mapping transaction96, admission97 and visual-overhead policy98

Patch0077 wraps only the disabled storage-retirement extension in a nonblocking runtime
mapping transaction. The actual runtime passed17 mapping/lock checks and75 protection cases;
a real runtime+Tracker alias test refuses deletion despite the old virtual proof passing.
Five Metal cache groups pass, but their mapping guard is a model, separate from the runtime
fixture. The74-patch series reconstructs. This proves only current DIRECT-VMA exclusions: a
historical unmapped live owner or a hidden native/GPU alias is not excluded. Keep0052 OFF.

ADMISSION97 met the unchanged1200MiB startup gate naturally, then stopped around38.49s on
rapid swap growth10173.0->11064.62MiB, not warning pressure alone. It produced no post-death
mapping inventory or FPS result. The growth accompanies loading/uploads and does not prove
a leak; retirement was OFF. Owned processes stopped, with no immediate restart.

For future scored A/B windows, stop visual observation, screenshots, screen recording and
remote visual access for the full suite; keep the game normally visible. Perform pixel/UI
correctness checks outside scoring and settle again. Match lightweight telemetry and memory
readback in both arms. `run` and `benchmark_candidates.py` accept
`--visual-observation absent` as an **operator declaration**, not automatic external recorder
detection. Known `present` is refused for scoring/comparison; mismatched states cannot
compare. Legacy missing metadata remains `unverified`, with an explicit comparison warning;
never retrospectively certify old results or blame all null gains on Computer Use.
73 benchmark-related tests pass. Cua recording previously read disabled; the latest status
query found no Cua daemon and did not start one. No visual snapshot was taken this turn, but
this does not certify every external recorder. Formal app, preferences and15 normal save
files still match formal94; no cache was cleared or release installed/published. Evidence:
workspace `mapping96-admission97-visual98.json` and `formal97-after.json`.


## HISTORY99: first-map witness for historical physical aliases

The actual-runtime negative control now maps B, unmaps it, then maps C to the same
physical page, with no simultaneous B/C VMA. The old transaction permits C. The actual
runtime+Tracker fixture also retains a live logical owner after its VMA disappears.
These are synthetic ownership counterexamples, not real-game corruption evidence.

The disabled storage extension now requires `runtime_memory_reclaim_if_unaliased_v2`.
A bounded physical-page witness starts before the first DIRECT map if and only if
`BB_TEXTURE_RELEASED_STORAGE=1` at that time. A page seen at different guest VAs is
permanently refused, even after unmap/release/reallocation; same-VA remaps, VMA splits
and unrelated pages remain eligible. No late reconstruction or OOM fallback authorizes
retirement. Default OFF allocates no table. Payload is1294336 bytes at5056MiB pool,
4194304 bytes at16GiB pool; allocator rounding/process footprint is not that payload.
The v2 weak import refuses older v1-only runtimes.

Five history configurations,17 actual mapping/lock checks, actual Tracker+runtime alias
checks,75 protection cases and five Metal cache groups pass. The74-patch series reconstructs;
73 benchmark tests pass. The cache's mapping proof remains a model, separate from the
runtime fixture; a joint actual-runtime+Metal regression is next. Hidden raw host/GPU
references are still not certified. No new game/death/FPS/memory-saving result is claimed.
Retirement remains OFF, all35 uploads and synchronization remain intact, formal app/data
hashes remain unchanged. This turn used no game window or visual capture. Evidence:
workspace `history99-historical-physical-alias.json`, `formal99-after.json`.


## JOINT100: actual runtime + TextureCache + Metal in one process

`test_runtime_metal_retirement.py` now adapts the existing cache fixture using checked
substitutions. A locally loaded library compiles actual runtime_memory.c/low_heap.c.
Mapping admission, backing reads, generation and page protection delegate to that library;
there is no modeled permission for these services. Unrelated services remain fail-fast
stubs and the rasterizer's IsMapped is minimal. All guest test addresses are actual DIRECT
mappings; no guest executable, assets, save or disk cache participates.

Five cases passed on Apple M4 with x86_64 test process and Metal API Validation: OFF keeps
the object and allocates no history table; unique mapping invokes actual v2 and normal
cache/deferred native destruction; active aliases, historical unmapped live owners and
missing first-map history retain byte-exact GPU pixels without entering the free callback.
The unique case also exercises one real nested page-protection call while the mapping write
transaction is held, releases the final native view/backing, and validates fresh address-reuse
pixels. This closes the previous separation between runtime and modeled Metal fixtures for
these synthetic lifetime cases only.

An extra unique-case run reads currentAllocatedSize at final view release:160940032 ->
160923648 bytes (-16384). Cache-reported allocatedSize capacity is49152 bytes. These ledgers
differ and must not be added or treated as OS resident-page decommit proof.

Reproduce with a current existing low-parallelism Metal build:

```sh
cmake --build /private/tmp/bbport-metal-build --target texture-cache-views-test --parallel 1
python3 tools/macos/test_runtime_metal_retirement.py \
  --build /private/tmp/bbport-metal-build \
  --output /private/tmp/blood-echoes-runtime-metal-new
```

Use a new private output directory, no concurrent game, and host permission for runtime
shared-memory mappings. Helpers are signed/verified. `--mode released-storage` runs only
that case; reports list their expected modes explicitly. This is small offscreen correctness
readback, never an FPS score or screen capture. Full game/native-pointer coverage, released
candidate admission and death/performance validation remain pending;0052 stays OFF and all35
uploads remain. Formal app, preferences and15 save hashes unchanged; no release/cache change.
Evidence: workspace `joint100-runtime-metal-retirement.json`, `validation100/`, `formal100-after.json`.


## GUARD102: explicit capacity expansion after closing foreground apps

Swap fell10848.62->9807.62MiB. A10.02s read-only sample saw pressure1, zero swap-out,
~0.019MiB/s swap-in and no net swap growth. Compressor physical occupancy was~1.26GiB;
available disk~393GiB. This is a short idle observation, not a loading-pressure guarantee.

The owner explicitly authorized a larger swap budget. Default growth is now12GiB, with a
24GiB absolute system-swap ceiling. No OS swap setting changed. Critical, rapid-growth,
lock/sleep,9GiB game-footprint,1200MiB startup and256MiB reserve guards remain.
The original9626.56MiB batch baseline is preserved; current allowance11850MiB.
Policy v5 and CLI track absolute ceiling/shared baseline; ABBA shares one growth budget
across runs. Unlike budgets cannot directly compare.79 benchmark tests pass, including real
CLI parsing for the new limits. Heavy gpucapture's separate1GiB diagnostic growth limit is
unchanged; no capture or game was launched.

Private admission101 history/snapshot sources and10 analyzer controls are prepared, not yet
compiled/validated as a native probe or package. Its sampler will not increment mapping
generation or free anything. Sampled+storage is no longer incorrectly filtered out by the
private analyzer when the actual state predicate permits it. The next private runner uses
guard102 policy and omits display readbacks. Remote observation remains unverified, so no
FPS score is accepted. Formal app/preferences/15 saves unchanged; caches untouched.
Evidence: workspace `guard102-expanded-capacity-and-memory.json`, `formal102-after.json`.


## ADMISSION101 / GAME103 / REVIEW104

The private snapshot primitive passed14 actual-runtime checks: no generation change or free
callback; aliases/history/mirror/contention failures are refused. Actual runtime+Metal+CSV+
parser also validated one UID across GPU-pending, cross-domain-live-overlap and snapshot-
eligible phases (false,false,true). Its VMA parser input is the known fixture layout, not
a game inventory, and every result authorizes_future_free=false.

Admission101 is compiled, relocated and deep-signature verified. The actually executed
embedded probe exports the private snapshot/v2 interfaces; the renderer imports the private
snapshot symbol. The public runtime was not given a forced audit flag.

GAME103 used the authorized12GiB growth/24GiB absolute policy. Starting9775.62MiB swap,
pressure1/unlocked, it terminated cleanly at44.57s after rapid growth to10393.38MiB
(+617.76MiB, pressure maximum2). Two late intervals at~94MiB/s triggered the retained6s
rate rule. Capacity/critical/lock/9GiB-footprint guards were not the cause. No texture/VMA
CSV, death cycle or FPS result was obtained; all visual snapshot requests were omitted.

REVIEW104 is offline only:30s continuous>=32MiB/s plus>=1GiB soft threshold, and a separate
>=128MiB/s,>=6s,>=1GiB emergency tier. Ten controls/replay checks pass, including the known
103 prefix, but later loading is unobserved and safety is not proved. Critical, capacity,
footprint,session/time controls would remain. No live code imports this proposal. The prior
30s exception was explicitly one-time; current authorization expanded capacity, so a new
one-round answer was requested and is pending. No automatic restart. Formal data unchanged.
Evidence: workspace `admission101-game103-review104.json`, `validation104/`, `formal104-after.json`.

## Phase105: authorized continuous-rate policy and bounded metadata run

The human authorized testing until2026-10-22T17:00:00+08:00, resolving the104 pending question. Policyv6 adds explicit6/30-second modes (default6). The30-second mode implements the reviewed soft/emergency thresholds exactly, resetting streaks on a slower/flat/recovery interval.83 benchmark tests pass. Capacity, footprint, critical/session guards remain. A timezone-required stop-at deadline is checked before state access/launch and on every guard call, then existing owned cleanup runs. Candidate suites forward the same mode/deadline/shared baseline and compare rejects unlike rate modes. GAME105 is a metadata-only isolated run with storage retirement OFF; results pending, not FPS evidence.

## Actual game follow-through105..117

105/106 completed two controlled SetHp death/respawn cycles each with52 OFF/ON. Both completed/cleaned successfully with no logged GPU, incompatible-view or missing-binding error.106 retired54 images/66.4MB capacity; this is not equal OS physical decommit.108/109 each completed three Dream/Central round trips (12 verified world-unload/load transfers total); ON retired168 images/258.1MB cumulative, final native allocated~70.8MiB and footprint~54.4MiB lower in that pair. Both later same-map memory curves plateau/fall, not an observed linear leak over this~6min route. Neither guarantees unbounded play or arbitrary aliases.

The first static comparison110 was still content-audited: BB_TEXTURE_AUDIT_DIR enables per-draw ledger work even without snapshot requests.111 was stopped through owned signal cleanup. These were not accepted as clean FPS evidence.114..117 remove the audit variable entirely from process start (NativeMode false), use separate prospective45s static windows, then perform display readback only after each window. OFF46.025/45.487 versus ON46.354/46.270 yields+1.214%, comparable to the OFF group's own spread; no repeatable FPS gain is established. Native window memory differs only~8.7MiB while footprint differs~321MiB, illustrating why footprint cannot be substituted for direct texture-lifetime attribution. Static surface spot checks found no new missing geometry/textures in these images; this is not whole-game pixel proof. External visual-observation state remains unverified. Storage52 remains development-only/defaultOFF. Production app/settings/saves/caches were not targeted. Full case evidence is retained in the workspace105..117 records.

## Restored-experiment bundle117..120

The six restored safe-but-uncertain candidates were tested together (storage52 OFF throughout) without per-draw content audit or score-window captures. OFF45.487/45.996FPS versus ON45.713/45.357 gives45.742→45.535FPS(-0.452%), within this suite's variation; no overall benefit established and no independent per-flag inference. All six controls remain experimental/defaultOFF. Copy-pair activation is logged; absent render-update Info logging does not establish zero eligibility. Post-window visual spot checks show no new major missing surfaces/HUD; not whole-game pixel equivalence. Normal renderer/preferences/15 saves match formal105 hashes; game INI equals the frozen current settings. All owned processes ended, pressure1, swap11092.06MiB. Next investigate unknown range frees by missing-tag/extent-mismatch/address-domain categories before authorizing any broader reclamation.
