# Warning-tolerant benchmark and combined candidate comparison

On 2026-10-18 the owner authorized replacing the old warning-level abort rule, adding
lock detection, and testing the overnight candidates together before changing normal play.
This supersedes the old normal-pressure-only resume condition in earlier experimental reports.
Historical stopped runs remain invalid; they are not retroactively scored.

## Safety versus measurement validity

The guard policy is `critical-sustained-swap-session-v3`. Samples are retained about every three
seconds, including the sample that triggers an abort. System warning level 2 does not stop
startup, warmup or scoring, and those frames are not removed from the score. Critical level 4
still stops immediately. A rolling window of at most 15 seconds stops on at least 512 MiB
swap growth at an average of at least 32 MiB/s, with at least six seconds of observations and at least two consecutive sampling intervals
both growing at 32 MiB/s or faster. A single loading burst followed by flat swap does not
qualify.
The existing per-run total swap growth limit (default 2048 MiB) and 9 GiB game-footprint
backstop remain; the combined suite also bounds net swap growth from its own initial sample.
Pressure is system-wide and cannot by itself attribute memory growth to a renderer change.

The harness reads `IOConsoleLocked` through IOKit and checks all online displays through
CoreGraphics. A locked console, all displays asleep, unavailable state, a monitoring gap over
12 seconds, or a wall/monotonic clock divergence over two seconds invalidates the run.
No screen images are taken. An owned `caffeinate -di -w PID` assertion prevents idle display
and system sleep for the owned game only; it does not disable manual lock or change system
preferences. Both game and assertion processes are cleaned up on success, error or interruption.
Comparison refuses unlike guard policies/limits, telemetry and audio modes.

## Combined experiment

`tools/macos/benchmark_candidates.py` runs same-binary all-OFF / all-ON / all-ON / all-OFF,
with the private fixed direct-entry Central Yharnam cohort. Each run uses 45-second warmup,
the existing 15-second settled/compile-free condition, and 45-second measurement. It keeps
every measured frame. Per-stream mute retains the real CoreAudio cadence. Detailed stage
timers and capture are disabled. Resolution, effects, filtering, precision, cache budgets,
velocity, occlusion, two-level address table and existing lifetime patches remain unchanged.

The nine toggled flags are clear batching, direct proxy copy, adaptive staging, texture tiling,
precise residency, duplicate-only render barriers, query compute chain, precise direct compute,
and half-float tiling. This deliberately includes previously unhelpful clear batching to match
the owner's all-candidates request. Rejected local compiler/vertex/presentation prototypes and
the older fragment-uniform experiment are not added. A combined score cannot attribute benefit
to individual flags. All runs use one private candidate binary; the normal app stays unchanged
until performance, slow-frame, memory and correctness evidence justify enabling anything.

The suite retains full result/cleanup paths, score-window process/owned-Metal median and peak,
pressure-warning fraction, total/peak/score-window swap growth, and workload identities. Memory
capacity and imported aliases are not summed as independent resident allocations. A single
faster mean is insufficient to claim memory non-regression or an individual patch benefit.

Validation: 42 benchmark unit tests pass, including long warning-level scoring, rapid versus
slow swap growth, critical/cumulative limits, lock/display/sleep invalidation and comparison
policy matching. Live unlocked-display detection and private-app code signature also passed.
Eight attachment, three GPU report, six memory report and three patch-series tests passed.
The new suite's measured outcome is recorded after its runs, rather than predicted here.

## First guard revision correction

The initial v2 combined baseline stopped after 27.86 seconds. Swap was flat at 8639.06 MiB
through 24.72 seconds, then reached 9181.38 MiB at 27.85 seconds. This was a single observed
542.32 MiB loading burst, not evidence of sustained growth. The console was unlocked and
display awake; pressure was warning, not critical. No FPS score was accepted. v3 now requires
consecutive fast intervals in addition to its rolling byte/rate criteria. Tests replay the
observed plateau/burst/plateau, sustained growth, and interrupted-growth sequences. The failed
v2 record is retained and is not compared with v3.

## Completed same-binary ABBA and activation decision (0.2.19)

All four runs completed under v3, kept every scored frame, and verified original save hashes
and launcher settings unchanged. Each used the same renderer `7ee150329b7337ff2e1076a5247d18c131d18732bdb02818137d4736e91df2df`, probe,
cohort, camera, 1920x1080 output and preset 3 (960x540 scene). These are static Central Yharnam
measurements with lightweight telemetry, not an uninstrumented HUD or whole-game claim.

| Order | Profile | Mean FPS | 1% low FPS | p95 ms | Median process MiB | Scored warning fraction |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| a1 | All OFF | 47.91 | 28.72 | 23.77 | 5307.25 | 100% |
| b1 | All ON | 48.85 | 36.17 | 21.84 | 5463.76 | 100% |
| b2 | All ON | 48.33 | 37.32 | 21.99 | 5576.17 | 0% |
| a2 | All OFF | 45.85 | 31.91 | 23.58 | 5523.48 | 100% |

Mean of the two run means: **46.8791 -> 48.5905 FPS (+3.65%)**, 1% low **30.3143 ->
36.7451 FPS (+21.21%)**, p95 **23.675 -> 21.915 ms**. Mean draw counts differ by less than
0.03%; no shader compilation or logged GPU fault occurred during scoring. No run had positive
net or peak swap growth relative to its own initial sample. No lock/display-sleep event occurred.

Mean of per-run process medians: **5415.36 -> 5519.97 MiB (+104.61 MiB, about +1.93%)**.
Owned-Metal capacity medians averaged **1228.10 -> 1276.42 MiB (+48.32 MiB)**. These are distinct
accounting ledgers and must not be added together. The two baseline process medians themselves
differed by 216.23 MiB. Candidate B2 retained more render-target objects than the other three
runs, and system pressure was normal during B2 but warning in the other scored windows.
Thus this is an observed combined benefit with run-to-run variation, not proof of individual
patch causation, statistical significance, zero memory regression or lower system pressure.
The >=60 FPS objective remains unmet. A future comparison must retain these conditions and
report all samples rather than discard warning or slow frames.

The owner explicitly accepted the observed approximately 105 MiB process-memory increase after
seeing these results. Consequently **0.2.19 enables the nine measured flags in the Metal launch
entry**, preserving explicit zero overrides for future same-binary A/B. The renderer library's
instructions are unchanged from the measured candidate, and unknown/unsupported resource paths
retain their conservative fallbacks. This combined decision includes clear batching despite
its earlier standalone no-benefit result; it is not an individual endorsement of that flag.
Vulkan launch defaults, all quality settings, cache capacities and existing velocity/blur bypass,
occlusion and two-level-address patches are retained. Normal Play clears automation/capture
controls and remains audible; the benchmark-only safety/session checks do not run in normal Play.

All 12 all-nine-flags native combination groups passed serially with Metal API validation,
covering held, direct and no-replay runtime, scene-target, BDA and scheduler paths. Two stub-runtime
launch integration tests verify the normal nine defaults, diagnostic/input cleanup, and explicit
all-zero isolated benchmark overrides without booting the game. No compiler/renderer rebuild is
needed to enable this environment profile. Packaging checks renderer instruction identity against
the accepted report, eager-load linkage, signatures and all dependency paths.

Local raw results: `outputs/benchmark-m4-attachment-audit/combined-v3.json` and its four referenced
run directories. All-nine-flags native evidence: `outputs/metal-all-candidates-combination`.
No game assets, saves, shader caches or captures are included in published source.

## Icon-only follow-up (0.2.20)

The launcher now includes original vector moon-and-blade artwork, with a complete 16..1024
pixel ICNS resource declared by CFBundleIconFile. Finder and the running Dock instance share
that resource. Game-folder validation no longer replaces the application icon with the raw
sce_sys/icon0.png. The icon generator is retained as source; no game artwork is packaged.
The GPU/probe instructions and nine enabled defaults remain those measured above. This is a
launcher/resource update and does not imply another gameplay speedup.
