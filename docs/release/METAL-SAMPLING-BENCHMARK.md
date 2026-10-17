# Automated Metal gameplay benchmark and fragment sampling audit (0.2.17)

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
at least 100 real game frames, nonzero camera position and over 200 draws/frame. Shader
compilation invalidates a warm-cache sample. Static scene position must remain within 5 cm;
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
