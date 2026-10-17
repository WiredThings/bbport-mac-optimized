# Sampled Metal stage timing

Set `BB_MTL_GPU_TIMING=1` in the launcher's explicit extra-environment field for a diagnostic
run. Inherited `BB_` variables are deliberately removed by the launcher. Normal launches leave
timing disabled. `BB_MTL_GPU_TIMING_EVERY=8` is the default sampling rate; `1` samples every
command buffer for bounded offscreen verification. Restore explicit overrides after measuring.

The profiler attaches timestamp counter samples to existing physical render, compute and blit
passes, including held passes replayed by the encoding thread and hoisted compute passes.
Descriptors are copied before instrumentation, so reusable guest descriptors never retain stale
sample indices. Labels separate guest rendering, camera motion, velocity geometry, velocity depth
snapshot/rescale, vector merging, blur bypass copies, occlusion snapshots, UI and host copies.
Mixed labels in a single encoder are reported as mixed rather than creating new passes to time them.

MetalFX hides its internal encoders. In sampled command buffers only, two four-byte blit probes
bracket it using the scaler's fence interface. The original fence property is restored after
encoding; an existing external fence is preserved. The reported `metalfx-boundary` span includes
the probes and their dependency cost. No CPU waits, texture readbacks or extra command-buffer
submissions are introduced. Unsupported timestamp hardware or counter allocation failure disables
profiling and leaves normal rendering active.

Storage is bounded to four reusable slots, each with 4096 timestamp samples and 1024 metadata
entries, plus a four-byte private probe buffer and fence per slot. Busy slots and full metadata arrays skip samples, record drops and continue rendering.
Slots stay busy until the GPU completes AND CPU resolution finishes. Completion blocks retain
only the profiler/counter storage, not game images or the scheduler. Only completed command
buffers are resolved. Two paired device clock reads convert GPU ticks to CPU nanoseconds, following
[Apple's timestamp conversion](https://developer.apple.com/documentation/metal/converting-gpu-timestamps-into-cpu-time).
Invalid counters are excluded and counted. Missing/optimized-away stages and vertex-only or
fragment-only render samples are counted separately. Timestamp ranges must lie within the
current command buffer's paired device clocks, so unwritten slots cannot reuse stale samples. No texture/staging cache budget, guest lifetime,
two-level page-table mapping, visibility query or frame setting is changed.

Every five seconds the log reports sampled stage spans, mean/max per encoder and a weighted
per-frame estimate; physical encoder transitions (including render-to-render), requested barriers,
actual fence waits, native encoder setup/closing CPU time and command-buffer commit CPU time.
Sample selection hashes command-buffer numbers to avoid always selecting a fixed rendering phase.

With adapter patch 0012, sampled guest render/compute passes also retain a bounded label:
the first pipeline of the physical pass, with the monotonically increasing render-pass ID
removed. Up to 128 labels are accumulated per report window; the top eight are printed as
`Metal GPU hot pass`. Overflow is counted without dropping the overall stage totals.
Labels identify pass families, not isolated shader execution times. The analyzer includes
their sampled estimates; labels outside the top eight of a window are absent from its ranking.
No additional passes, shader variants or resource references are created by label collection.

Stage spans may overlap. Their sum is NOT frame time. An interval union measures covered GPU
time within each sampled command buffer; remaining command-buffer time includes dependencies,
tile work and uninstrumented work. It is NOT a direct measurement of switching overhead. CPU
boundary time excludes draw-command encoding and replay joins. Presentation uses a separate
queue and is excluded. Measure profiler-off FPS before/after in the same scene to distinguish
diagnostic overhead from an optimization. Skip startup/warm-up windows when ranking stages:

```
python3 tools/macos/analyze_metal_gpu.py last-run.log --skip-windows 4 --output gpu-timing.json
```

Instrumentation alone makes no claim of gameplay performance improvement. Optimize a measured
hot path afterward and rerun velocity/depth ordering, occlusion isolation, BDA remapping and
memory retirement tests before replacing the installed app.
