# Bounded buffer dependency audit

`BB_MTL_BUFFER_RANGE_AUDIT=1` is a diagnostic, OFF by default. It does not change returned
hazard decisions, GPU ordering, shader arithmetic, resource allocation or cache capacities.
The normal installed 0.2.18 app remains unchanged; there is no new accepted gameplay FPS or
60 FPS result.

The existing buffer tracker keeps up to 16 intervals per resource, then replaces them with a
conservative bounding range and union of access flags. An independent observer records the
original insertions before any contained-range shortcut. It merges only exactly compatible
intervals and compares queries with the original answer. The observer is bounded to 64
resources and 256 intervals per resource, under 400 KiB CPU memory, with no GPU allocation.
Overflow and untracked resources are explicitly unknown, never classified as a proven
redundant dependency. False-hit examples are bounded to eight and summaries to one per five
seconds. Clear reuses its bounded storage. The default path allocates no observer.

A private direct-entry Central Yharnam diagnostic observed 3,679 range collapses across 222
steady frames, but only 26 proven false write-dependency queries (about 0.117/frame), zero
false read-dependency queries and zero observed missed dependencies. Some reference queries
were unknown, so this is not a complete exact-hazard proof. Query count is not a physical
GPU barrier count or time saving. The evidence does not justify changing the tracker as a
primary performance optimization; its conservative production behavior is retained.

`benchmark.py` permits buffer-range, staging and copy audits only with native diagnostic
capture, and refuses to compare older scored results containing an enabled audit. Capture
manifests record explicit variants, telemetry mode and per-stream mute state. Private traces,
shader text, game files and saves stay local.

A separate compute threadgroup compiler-hint probe used three actual hot vertex shader
bodies converted to offscreen compute, without changing arithmetic or dispatch size. Auto,
64-thread maximum, exact 64-thread requirement, and exact/maximum/SIMD-multiple variants had
identical tested output bits including NaN payloads. Balanced 40-sample medians were nearly
unchanged; noisy means did not establish a gain. This synthetic probe is neither a vertex
stage performance result nor game FPS. No threadgroup hint was added to the renderer.

Focused native checks verify unchanged conservative answers, independently qualified false
hits, insertions hidden by the original contained-range shortcut, Clear, and bounded interval
and resource overflow. The entire Runtime-dependent native test set must be rebuilt after
this diagnostic changes the tracker object layout; old test binaries must not be reused.

Validation: all 71 existing native regression groups pass after rebuilding their 19 test
targets; the focused audit case passes with Metal API validation. There are 33 benchmark
checks and 16 Metal packaging/report/patch checks. The 22-patch source series reconstructs
and reapplies idempotently. Normal Play clears the three audit environment flags.
