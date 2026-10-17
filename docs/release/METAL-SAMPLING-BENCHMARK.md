# Automated Metal gameplay benchmark and fragment sampling audit (0.2.17)

This report describes the historical 0.2.17 cohort. The owner's newer save enters
Central Yharnam directly; its separate cohort and measurements are documented in
[the attachment audit](METAL-FRAME-CAPTURE.md). Do not mix scores between these saves.

## Reusable benchmark

`tools/macos/benchmark.py` builds a private test cohort from the owner's installed assets,
current configuration and save. Game data and saves are not distributed. It preserves the
original save, normal launcher preferences and normal shader cache. Each run restores the
frozen private save, enters the game with scripted input, warms caches, collects frame times
and system/renderer memory, then terminates only its own newly created process group.

```sh
python3 tools/macos/benchmark.py init --workspace /path/to/new-benchmark
python3 tools/macos/benchmark.py run --workspace /path/to/new-benchmark --name before
# Install a renderer update, retaining the frozen benchmark cohort.
python3 tools/macos/benchmark.py run --workspace /path/to/new-benchmark --name after
python3 tools/macos/benchmark.py compare /path/to/before/result.json /path/to/after/result.json
```

Use one new directory per save/config cohort. The app needs the 0.2.17 explicit `--benchmark`
entry and input isolation. Normal Play still removes all scripted input controls. Live
keyboard/gamepad input and overlay shortcuts do not drive the benchmark. Window close still
allows the owner to abort it. No global `pkill` or original-save restore is used.

Default warmup/sample are 45/45 seconds; startup is bounded to 300 seconds. The sample needs
at least 100 real game frames and a nonzero non-title camera. Entry requires a stable
three-second window: every camera must match, and at least 99% of recorder frame counters
must exceed 300 draws. Occasional low recorder counts retain their complete frame times in
the score; no slow frames are excluded. Shader compilation invalidates a warm-cache sample. Static camera position must remain within 5 cm;
`--expected-camera X Y Z` during init can enforce the intended location. Comparison rejects
changed config/save cohorts, instrumentation, host, camera position and draw count shifts
over 2%. This is a static scene test, not a whole-game average or bit-exact simulation replay.
Interleave multiple runs when attributing small improvements.

Results include average FPS, median/p95/p99 frame time, 1% low (reciprocal of mean of slowest
ceil(1% of frames)), worst frame, draw counts, compiles, CPU recording, process footprint,
system pressure/swap and renderer resource logs. GPU stage timing is off by default to avoid
its sampling overhead in the FPS benchmark; `--profile-gpu` opts in and comparisons require
the same mode. CSV, log, machine-readable results and cleanup records remain in each run.

The tool refuses concurrent game processes and requires normal pressure before boot. It
aborts its own run at critical pressure, more than 2 GiB swap growth, a 9 GiB game footprint,
or timeout. Interrupted runs and changed original saves are not accepted as valid results.
These guards reduce exposure to the memory problems observed on the 16 GB Mac; they are
bounds, not a guarantee against every external process changing system pressure.

## Fragment texture audit

The real hot cached shaders have 3 samples in 71c96598, 8 in e21e283a, 2 in 765450df and 5 in
d92266b9. Repeated UVs generally address different images, while repeated images use different
UVs. Those operations cannot simply be removed or merged without changing the result. No
texture resolution, format, filtering, mip selection, anisotropy, precision or effect changes.

Small material/view parameters used in those texture coordinates are declared as read-only
`device` buffers. The opt-in `BB_MTL_FRAGMENT_UNIFORMS=1` experiment specializes proven bounded
uniform accesses to Metal's `constant` address space. This follows Apple's recommendation to
use constant data for uniform accesses; see [Optimize Metal Performance for Apple silicon Macs](https://developer.apple.com/videos/play/wwdc2020/10632/).
Apple's newer architectures share cache hierarchy, so a benefit must be measured rather than
assumed; [Metal shader best practices](https://developer.apple.com/videos/play/tech-talks/111373/)
explains this distinction.

The source rewrite accepts only read-only uint data[1] blocks in generated fragment entry
arguments. Every use must be a literal index plus an optional immutable per-draw packed
8-bit offset, with the entire possible access bounded below 4 KiB. Dynamic indices, pointer
helpers, atomics, writable buffers/images and unproven forms retain device access. There is
no upload, new buffer, new texture, retained reference pipeline or cache resizing. Buffer
bindings, data bytes and shader math/sampling are unchanged. Failed compilation falls back
to original source. The canonical disk MSL remains untouched; specialization applies on
fresh compilation and disk-cache loading alike. The experiment is disabled by default until
gameplay measurements establish whether it is worth enabling.

Validation includes exact float-output comparisons for linear/sRGB textures, trilinear mip
filtering, repeat/mirror addressing and all packed-offset extremes; unsafe patterns and
integer-overflow cases must fall back. Existing BDA, velocity, blur, occlusion, proxy, resource
retirement and memory-budget groups remain regression gates. Runtime benchmark results are
recorded below after the autonomous end-to-end run is validated.

The initial autonomous smoke run exposed a state-detector bug: the title's 3D preview at
(0,4,-8) briefly crossed the old draw threshold and was mistaken for gameplay. Warmup then
aborted the private process at 38 seconds. This was a script error, not the memory guard.
The original save hashes were unchanged. Title-camera exclusion, continuous stable-window
entry and warmup grace now have regression tests; the failed run is never accepted as a
performance result.

The current local cohort starts from a pre-boss-death save. The owner confirmed that it
naturally dies and respawns at Central Yharnam. Its expected camera is
(-190.784, -27.226, 71.116); warmup starts only after this respawn settles. This is a separate
Central Yharnam cohort and must not be compared with the earlier manual Hunter's Dream
samples. Title input stops permanently on the first world frame, including during death
and respawn transitions. A second smoke run reached the stable respawn scene but was
rejected by an overly strict per-frame recorder-count check; it is not an accepted baseline.

The frame recorder can emit an isolated present with absent camera telemetry. Up to 1%
missing samples are accepted only when each single gap is bracketed by valid identical
world cameras within 50 ms. Endpoints, consecutive gaps, title/zero/invalid coordinates,
long gaps and scene changes remain invalid. Only the absent coordinates are carried from
the preceding matching record; every original frame time, including the neighboring slow
frame, remains in the score. Raw CSV and missing-camera counts are retained. The first
repeat that exposed this condition remains rejected rather than being retroactively scored.

## Accepted autonomous results — Central Yharnam

M4 Mac mini (10 GPU cores, 16 GB), frozen current owner settings and save, private cache,
45 seconds warmup plus shader quiet window, 45 seconds sampling, GPU stage timing off.
These are instrumented frame-statistics runs, not the earlier uninstrumented HUD readings.
Four accepted runs used identical renderer/probe binaries and camera
(-190.784, -27.226, 71.116). Average draws stayed between 1202.99 and 1203.77.

| Order | Parameter address space | Mean FPS | 1% low FPS | p95 frame ms | Median process GiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Original device | 41.90 | 31.54 | 26.18 | 5.853 |
| 2 | Experimental constant | 41.39 | 26.14 | 26.64 | 5.767 |
| 3 | Experimental constant | 41.62 | 25.20 | 26.58 | 5.814 |
| 4 | Original device | 41.60 | 28.23 | 26.34 | 5.755 |

Mean of the two run means: device 41.75 FPS, constant 41.50 FPS (-0.59%). Both experimental
runs also had lower 1% lows than either reference. This small sample does not establish a
precise causal regression, but it provides no reason to enable the experiment: **default
remains device access**. No claimed sampling speedup. Differences in process footprint are
not evidence of memory savings; this specialization creates no new resource or upload.

The specialized paths actually compiled, including the hot 71c96598/e21e283a shaders; no
GPU faults or compilation occurred in the accepted measurement windows. Every accepted
run stopped its own private process and verified unchanged original save hashes. No normal
launcher preferences were written. All accepted windows had complete camera telemetry;
the separate rejected repeat remains excluded. Local detailed results and raw traces are
in the owner's benchmark workspace, not distributed with saves or game assets.

Validation: 43 native renderer test groups, 17 benchmark checks, and 12 packaging/analysis
checks pass. Exact GPU sampling tests include linear/sRGB, mip filtering and packed offsets;
existing two-level page table, velocity/blur bypass, occlusion, proxy clear, lean usage,
alpha fusion and resource-retirement/memory-budget tests remain passing. This does not
establish a whole-game visual or performance guarantee beyond the tested paths.
