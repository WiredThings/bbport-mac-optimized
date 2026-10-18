# Single-frame attachment audit and color-clear batching (0.2.18)

Current profile note: the owner later enabled the nine-flag combined profile, including
clear batching, after the [0.2.19 combined comparison](METAL-BENCHMARK-GUARD-V3.md).
The standalone 0.2.18 results below remain historical. The subsequent
[attachment lifetime audit](METAL-ATTACHMENT-LIFETIME.md) evaluates folding queued clears
into later render load actions; it does not authorize discarding arbitrary guest stores.

## Capture scope and the 16 GB host

The new owner save loads directly into Central Yharnam. It was frozen as a new cohort;
the older pre-death cohort remains untouched. The measured camera is
(-190.784, -27.226, 71.116). Current resolution, effects and cache budgets are retained.

Apple gpucapture was attempted for one CAMetalLayer frame with unused resource recording
and backtraces disabled. Automatic shared-memory capture repeatedly snapshots the large
no-copy guest address buffers: export grew to roughly 70 GiB and hit the bounded timeout.
A forced shared-memory optimization attempt also hit the diagnostic memory guard. No
complete replayable .gputrace was produced; rejected runs are not performance results.
Apple warns that forced shared-memory optimization may omit system-call writes, so its
replay pixels/profiling cannot be assumed correct for this runtime. See
[Apple's advanced capture options](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-in-xcode).
No diagnostic limit was widened beyond 1 GiB swap growth or critical pressure.

The practical fallback records one decoded GPU command frame: physical Metal encoders,
color/depth/stencil attachment IDs, parent views, sizes, formats, subresources, Load/Store
actions, labels and barriers, alongside the existing semantic guest resource log. It
copies no texture or buffer payload, creates no GPU resources and adds no GPU completion
wait. Render replay is joined at the diagnostic frame boundaries. Native log storage is
bounded to 8192 records / 2 MiB; truncated or unbalanced captures are rejected. Actual
complete files were about 388 KiB. This is command/attachment metadata, not a replayable
GPUTools capture or a complete shader/CPU/alias resource-use graph.

```sh
python3 tools/macos/benchmark.py run --workspace /path/to/frozen-cohort \
  --name attachment-audit --capture-native
python3 tools/macos/analyze_metal_attachments.py /path/to/run/frame_TIMESTAMP.txt
```

The private benchmark enters the scene, checks stable camera, warms caches, records one
frame, and stops only its own process group. Normal Play removes the capture controls;
performance runs remove inherited Metal capture variables. A capture run never produces
an FPS result. Its capture.json becomes valid only after process cleanup and unchanged
original-save verification. The owner's original save/configuration and normal shader
cache are not written. Resource snapshots and local game traces are not distributed.

## Observed work and safe optimization

The first full metadata frame had 263 physical encoders: 144 render (124 guest, 20 host),
68 compute, 51 blit, and 328 attachment records. Six adjacent full color proxy clears
at 960x540 each opened and closed their own render encoder. No intervening draw, compute
or blit needed an intermediate clear value. Other depth/stencil and post-processing
copies have actual readers; the metadata is not sufficient to remove arbitrary stores.
The analyzer's clear-load candidates alone do not prove the absence of draws. The six
passes were confirmed as clear-only using their labels and the ClearProxy source path.

The opt-in scheduler experiment queues only complete, standalone private 2D color clears. Same-size
independent targets share one clear-only render pass (at most eight color attachments).
Repeated clears of the same target keep the final value. Different sizes or a full batch
flush the preceding batch. Shared, depth/stencil, multisample, mipmapped/array, heap,
parent-view, buffer, IOSurface and aliasable targets retain the original path. Scene
proxy eligibility and format rules remain the existing ClearProxy checks.

The batch is flushed before any subsequent encoder, EndRendering, submission or wait.
Barriers encountered between queued independent overwrites conservatively wait for
prior encoders before the batch AND for the batch before its next consumer; dependencies
are not dropped. The pending descriptor retains at most eight existing textures at the
same scheduler tick. No new texture/buffer, history image, cache entry or larger budget
is introduced. It is disabled by default; BB_MTL_BATCH_CLEARS=1 enables the experiment.

The matched single-frame check reduced that six-clear sequence to one six-attachment
pass and total encoders to 258 (five fewer). Other guest-render/blit counts varied by two
between these individual frames, so only the exact clear sequence is attributed to this
change. Counts are not GPU milliseconds or FPS savings.

## Verification and performance comparison

48 native regression groups passed, including existing two-level BDA, velocity and blur
bypass, visibility-query overflow, retirement/idle staging/budgets, strict vertex math,
lean proxies and alpha fusion. New real-Metal checks verify exact clear pixels, repeated
targets, dimension/storage/format fallbacks, the eight-target bound and fence ordering.
The metadata tests verify frame boundaries, escaping, no buffer dereference and truncation.

Warmup scene detection allows occasional low per-frame recorder counts in a three-second
window, with a stricter stationary camera check. The scored 45-second window still requires
99% high-count records; every original frame time, including low-count/slow frames, remains
in the score. No frames are excluded to improve results.

## Accepted automatic ABBA results — direct-entry Central Yharnam

M4 Mac mini, 10 GPU cores / 16 GB, current frozen owner settings, 960x540 scene proxies,
1920x1080 output. Each run has at least 45 seconds warmup plus a 15-second quiet window,
then 45 seconds scored. Single-frame capture and detailed GPU stage timing were disabled;
lightweight per-frame statistics and periodic memory accounting remained enabled. These
scores must not be equated with uninstrumented HUD readings.

All four runs use one trial renderer (SHA-256
`27b42bc6391601b4b5c0e5e9ec29e539311eca47cbba2a2b1727b59a3db5ccf1`), the same
probe, cohort `f443903ad7a2b0f974434b93d76e5d80924a1321b534f57759383db0ad5dfff2`,
settings and camera. Mean draws varied by less than 0.02%. No shader compilation or GPU
fault occurred during accepted measurement; no slow frames were removed.

| Order | Clear path | Mean FPS | 1% low FPS | p95 ms | Median process GiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Original | 44.44 | 37.64 | 23.77 | 5.304 |
| 2 | Batched | 44.63 | 35.73 | 24.02 | 5.383 |
| 3 | Batched | 43.38 | 32.65 | 24.53 | 5.510 |
| 4 | Original | 44.12 | 35.37 | 24.25 | 5.436 |

Two-run means: original **44.28 FPS**, batched **44.00 FPS** (-0.61%); 1% low
**36.51 -> 34.19 FPS**; p95 **24.01 -> 24.28 ms**. CPU recording averages were
11.90 / 11.91 ms, while GPU time was 21.35 / 21.62 ms. Fewer encoder boundaries did
not remove the dominant GPU workload. Four runs do not establish statistical significance,
but they do not justify enabling this change for performance.

Mean per-run median process footprint was 5.370 / 5.446 GiB; Metal-owned allocation was
1254.54 / 1291.08 MiB. These counters vary across runs and retirement/collection phases;
the experiment creates no GPU allocation, but the measurements do not support a claim of
lower memory use. System pressure stayed normal (level 1) throughout all four runs and
swap did not grow. No-copy alias capacity must not be added to process resident memory.
Every run stopped its own process and confirmed original-save hashes unchanged.

**Decision: retain the experiment for investigation, default OFF.** The final renderer
changes only its default selection relative to the measured trial; the explicit enabled
and disabled paths are preserved. Both are covered by the native tests. Normal play keeps
the prior clear path, existing quality/effect settings and all cache capacities. Previous
constant-parameter experiments also remain disabled. Single-frame metadata is opt-in;
normal play removes its controls and adds no resource snapshots or new GPU allocation.
Older Hunter's Dream and pre-death save scores are not reused as a baseline.

Raw local evidence is retained under `outputs/benchmark-m4-attachment-audit`: each run has
`result.json`, `cleanup.json`, original frame CSV/logs and sampled accounting;
`clear-batching-abba.json` contains the aggregate. Game saves/caches/traces are private and
excluded from GitHub and source/application archives.
