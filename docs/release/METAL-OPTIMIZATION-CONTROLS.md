# Metal optimization controls — 0.2.24

The launcher exposes 23 runtime controls under **Advanced → Metal optimizations**, in English,
Simplified Chinese and Traditional Chinese. Existing object-motion and occlusion controls stay
in their current sections. No resolution, effects, save state or cache capacity is changed.

## Defaults and evidence

The 19 already adopted controls retain their current ON defaults. A missing preference uses
the current runtime default; existing explicit preferences remain authoritative.

- The two-level address table, direct scene-copy clear and idle/startup object reclamation
  remain adopted. The address table reduces capacity by about 0.9 GiB relative to the flat
  table; direct clear improved the earlier matched scene by about 5%.
- Attachment discard, strict vertex inlining, lean texture usage and alpha merge are marked
  **Experimental · adopted** where isolated full-game FPS benefit remains uncertain.
- The ten accepted clear/copy/staging/residency/tiling/barrier/query controls are marked
  **Experimental · adopted bundle**. Their ON defaults preserve the owner's previous choice
  and accepted bundle memory tradeoff; the badge does not claim an isolated gain per flag.
- Four optional experiments remain OFF: fragment-parameter specialization, inline small writes,
  relaxed fragment/compute math and binary pipeline persistence. Notes describe uncertain
  benefit, possible numeric changes and the observed memory costs. Binary persistence has a
  separate higher-memory label (about 0.6 GiB in the earlier archive-load experiment).

Environment names and individual notes live in `MetalOptimizations.swift`. Every checkbox
explicitly sends `0` or `1`. Required parents gate direct-compute precision, half-size texture
tiling and pending-clear folding. Disabling a parent suppresses the child's effective runtime
flag without losing the child's stored choice.

Settings persist in the optimized launcher's own UserDefaults domain. Existing matching `0/1`
developer environment entries are imported once when no optimization dictionary exists.
Thereafter visible controls override matching developer entries; old file-based A/B selectors
are excluded so they cannot silently override checkboxes. Vulkan receives none of the managed
Metal flags. Reset affects this section's controls only; game quality and saves are retained.

## Binary archive OFF now means no disk use

Patch 0033 gates loading an existing `MTLBinaryArchive` and automatic destructor serialization
with `BB_MTL_PERSIST_WARM_ARCHIVE`. Previously only the startup save was gated, so disabling it
could still load an old file. OFF now ignores existing files without deleting them. A fresh
compilation archive and the accepted live-PSO lifetime remain as before; all active pipeline
maps, functions and source/SPIR-V caches retain their capacities. Explicit low-level `Flush()`
remains available to native tests; ordinary startup already calls it only when opted in.

Native tests verify ON save/next-start hits, OFF with an existing file (zero archive hits and
byte-identical file preservation), OFF without release (no destructor-created file), retained
pipeline identity, late pipeline creation and GPU output. The 33-patch series reconstructs
idempotently. Swift tests exercise every toggle, defaults, parent gates, persistence, migration,
reset, environment precedence, backend isolation and all three localized descriptions.

## Retired offline-library experiment

The owner requested removal after eight same-renderer ABBA runs. The loader/export/compiler
helper, its patch and local compiled library artifacts were removed. No offline-library option
is present, and the experiment was never enabled in the installed application. Apple's Metal
Toolchain remains an installed development tool.

| Same-scene comparison | Source path | Offline library |
| --- | ---: | ---: |
| Warm cohort, mean FPS | 47.63 | 47.03 |
| Warm cohort, process median MiB | 5585.2 | 5575.9 |
| First-use proxy, mean FPS | 47.89 | 48.26 |
| First-use proxy, first world observed | 26.90 s | 28.31 s |
| First-use proxy, mean of first-5-second maximum frame intervals | 187.43 ms | 222.72 ms |
| First-use proxy, process median MiB | 5586.1 | 5502.2 |

The warm source-path prewarm times were 0.656/0.664 s. Offline first load was 8.845 s and its
subsequent warm load 0.824 s. Driver-cache warmth differs on the first artifact load; these
numbers do not prove a universal offline-code regression. No repeatable startup/hitch benefit
was established, and small steady-FPS differences were not accepted as a gain.

The first-use proxy removes only 560 recorded pipeline-key files from a private copy while
retaining MSL/SPIR-V and artifacts; it forces creation while loading Central Yharnam. It is not
an unvisited-region walkthrough. Each run used the frozen save/camera, unchanged 1080p output,
540p internal resolution and accepted patch defaults, 15 s quiet warmup and 30 s scoring.
Compiler audit/export/capture/stage timing were OFF in scored runs. All score windows had
warning-level system pressure and zero swap growth; original saves and preferences were
preserved. Memory differences vary with retirement phases and are not exact causal savings.

Retained local evidence: `outputs/benchmark-m4-offline-libraries/retirement.json`.
The experiment illustrates that offline Metal IR does not eliminate final GPU pipeline
compilation or supply unknown runtime descriptors. It does not establish that precompilation
is useless in engines with a complete pipeline manifest.
