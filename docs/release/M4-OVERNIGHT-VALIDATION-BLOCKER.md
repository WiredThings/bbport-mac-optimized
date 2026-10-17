# M4 fixed-scene validation status

The >=60 FPS objective has **not** been achieved or verified. The normal installed app remains
0.2.18; new transfer, residency, staging and query-chain experiments are OFF by default.
No quality/effect/resolution/cache-budget or memory-protection change was accepted to obtain
an FPS score. Tested source candidates and their limits are recorded in the adjacent reports.

## Repeated validation failure

The fixed private save now enters Central Yharnam directly, with a frozen camera/settings
cohort; it does not include the old death/respawn sequence. The original-path and combined
candidate runs repeatedly reach system warning pressure during warmup. The unchanged guard
stops sustained warning during warmup and rejects any warning in a scored window. Thus there
is no qualifying new gameplay FPS, slow-frame or memory non-regression comparison. Isolated
GPU timings, short captures, warmup frames and HUD observations cannot replace that evidence.

On the third consecutive goal turn with this verification blocker, another original-path
run (`20261018-041317-memory-attribution-reference`) was inspected using bounded OS memory
summaries on its exact owned process. This is a memory diagnostic, not an FPS benchmark:
OS inspection can perturb CPU timing. All four summary commands completed without truncation;
there was no GPU resource capture. The same warning guard subsequently stopped the run.
Cleanup confirmed the owned process stopped, original save hashes unchanged, and no launcher
preference write. No unrelated apps were terminated.

## What the memory evidence supports

The OS snapshot reported a 5,752,998,272-byte process footprint; later renderer samples were
around 5.83 GB decimal. Guest direct allocation/imported capacity was 4,812,046,336 bytes,
plus about 338 MB flexible allocation capacity. Imported Metal buffers alias the guest's CPU
pages: these capacities must not be added again as independent resident GPU allocations.
The renderer reported about 1.33 GB owned Metal capacity and 352 MB unclassified Metal
capacity. Capacity, footprint, graphics ledger and VM categories measure different things.

The OS summary includes roughly 3.08 GB in its untagged category, 1.14 GB owned unmapped
graphics and 589 MB IOAccelerator graphics. These are coarse accounting categories, not a
proof that individual resources are dead. A translated-process `footprint --wired` category
reported an impossible hundreds-of-GB value for aliased virtual memory, so that column is
not accepted as physical-wiring evidence. The bounded `vmmap` summary and actual footprint
remain available locally; no claim is made that all 4.81 GB imported capacity is wired.

Guest counters did not identify free imported chunks to return. Staging rings were recently
active, and existing draw/replay autorelease scopes plus an ARC descriptor probe provided no
basis for a speculative lifetime fix. Shader IR is reset between translations while arena
capacity is preserved. None of these facts proves there are no further optimizations; they
mean the audited paths do not justify freeing live data or shrinking caches.

Reducing the scope of large guest imports may eventually improve residency, but it would
require coordinated buffer binding, indirect address translation, page-table updates,
GPU-write hazards and tick-retirement work. Dropping their residency declarations without
that work would violate correctness. Dirty-proxy native-backing reclamation also needs
complete native-access and in-flight-use tracking, not just a timer or a cache-size cut.

## Resume condition

The immediate blocker is a repeatable gameplay comparison under the required memory gate.
Resume in a system state with enough headroom for the original fixed-scene workload to remain
at normal pressure through scoring. Preserve the frozen settings/save, 45-second warmup,
45-second score window, real audio cadence, lightweight telemetry and all existing guards.
Then run interleaved baseline/candidate comparisons before enabling or installing a candidate.
Do not infer 60 FPS from a microbenchmark, close unrelated apps automatically, or relax safety
or quality requirements to make the test pass.

Private evidence is under `outputs/metal-owned-memory-attribution` and
`outputs/benchmark-m4-attachment-audit`; game data, caches, saves and shader sources are not
published with this report.
