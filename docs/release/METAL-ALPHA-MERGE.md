# MetalFX alpha merge and launcher alignment (0.2.16)

The remaining per-frame scene colour resolve before MetalFX's final RGB merge preserved only
alpha: MetalFX supplies RGB while Bloodborne's post-processing consumes the original alpha.
The old path scaled the dirty RGBA16F scene proxy to the full native texture in a render pass,
then read that native alpha and overwrote RGB in a compute pass.

Patch 0017 reads alpha from the existing dirty proxy in the same compute pass that writes the
MetalFX RGB. It uses the old nearest pixel-centre mapping, identical format conversions and a
complete RGBA overwrite. No additional texture, buffer, cache entry or residency is created.
This removes one full-size write/read cycle and one render encoder, where the guarded path
applies. It does not eliminate the other copies required by guest reads, depth/stencil,
velocity depth seeding, or occlusion visibility.

Eligibility is deliberately narrow: normal output (not motion debug), reduced rendering,
existing dirty scene-level RGBA16F proxy, identity source texture, single mip/layer/sample,
matching native/upscaled dimensions, correct read/write usages and no source/destination
aliasing. A rejected call leaves coherence untouched and uses the original merge. After a
successful complete overwrite the proxy is invalidated; later native readers see the merge,
and a later proxy reader is refilled from the merged native texture. Resources and pipelines
continue through the existing scheduler/tick retirement. Two-level BDA, velocity batching,
blur bypass, clear discard, lean usage and memory budgets are unchanged.

Controls: default enabled; `BB_MTL_UPSCALE_ALPHA=0` retains the reference path.
`BB_MTL_UPSCALE_ALPHA_FILE=/absolute/path` optionally reads one 0/1 byte at most once per
second, for same-process A/B without recreating resources. With no file there is no polling.
`Metal upscale alpha: fused=...` counts actual encoded merges. GPU timing remains opt-in.

The pinned Metal engine also missed the launcher's existing Ultra Quality preset. The six
preset IDs, scales, names and menu order are now taken from the already implemented Vulkan
launcher branch. IDs 0..4 keep their meaning; 5 is Ultra Quality (x1.25), not a clamp to 4.
At 1080p it renders 1536x864; at 720p, 1024x576. The startup environment, game menu and overlay
use the same mapping. The user-selected preset is preserved.

Launcher corrections: retain all six quality choices, show actual even-rounded Metal render
resolution, correct the Native AA hint, offer only supported Metal presentation modes,
disable unused background upload on Metal, and wire the GPU diagnostics toggle to
BB_MTL_GPU_TIMING on Metal / BB_GPU_PROFILE on Vulkan. Diagnostic timing is labelled as
potentially affecting FPS. English, Simplified Chinese and Traditional Chinese are updated.
A legacy Metal FifoRelaxed selection normalizes to its existing runtime fallback, Mailbox.

Validation and gameplay performance results are recorded separately; implementation alone
is not evidence of a frame-rate improvement.

## Validation before installation

40 native regression groups, 24 Swift launcher tests and 12 Python packaging/analyzer tests
passed. The alpha test compares the actual old GPU resolve + generated merge shader against
fusion, byte-for-byte across all five reduced presets and a non-divisible ratio, then checks
native reads, proxy refills, rejected aliases, clean-proxy fallback, live policy switches,
and tracked allocation counts. Preset tests cover stable IDs, exact even-rounded sizes and
loading/saving/reloading preset 5. MetalFX temporal scaler creation at 1920x1080 -> 1920x1080
and 1536x864 -> 1920x1080 succeeded on this M4. These are offscreen correctness and creation
tests; real-game FPS, copy counts and memory behaviour still require the same-scene A/B.

## Same-process gameplay ABBA (2026-10-17)

Measured the installed 0.2.16 code (266cc69), Hunter's Dream at the Yharnam headstone,
1920x1080 output / preset 3 (960x540 scene), normal motion debug off. Four 30-second phases
used reference / fusion / fusion / reference in one process, with identical instrumentation
(GPU stage sampling every eight encoders plus frame statistics). First ten seconds of FPS
and first two GPU windows per phase were excluded. The user held the camera; recorded player
position was identical in all phases and mean draws differed by 0.15%. No steady-state shader
compiles occurred. Reference and fused use the same existing textures and pipeline cache.

| Measurement | Reference | Fused |
| --- | ---: | ---: |
| Mean FPS (mean of two phases) | 50.08 | 49.56 |
| Separate phase FPS means | 51.02 / 49.14 | 50.56 / 48.56 |
| Host-copy sampled span, estimated ms/frame | 1.330 | 0.718 |
| Upscale-merge sampled span, estimated ms/frame | 0.935 | 0.954 |
| Main guest-render span, estimated ms/frame | 9.727 | 9.932 |
| Mean process footprint, GiB | 5.572 | 5.562 |
| Classified owned resource bytes | 1,540,885,264 | 1,540,885,264 |
| Scene proxy allocations | 18 / 46,415,872 bytes | unchanged |

The scene-colour `shader-write` resolve occurred every frame in both reference phases and
was absent in both fused phases; the independent `transfer-read` resolve remained. This
confirms elimination of the intended copy, with host-copy spans about 46% lower. It does
not establish an FPS improvement: this run was about 1% slower on average with fusion, within
the observed between-phase drift. Retain the change as a validated redundant-work reduction,
not as a proven frame-rate gain. The roughly 10 MiB process-footprint difference is not a
meaningful memory saving; every classified resource category had exactly the same bytes
and object counts throughout the comparison. System memory pressure was warning level (2)
in all four phases; this change did not solve overall system pressure.

GPU counter invalid/missing/capacity/busy drops were all zero. No GPU command-buffer faults
were logged. Two-level BDA, game velocity merge, both motion-blur bypasses, occlusion,
direct proxy clear and lean usage markers were present. The user reported seeing this scene
above 50 FPS, consistent with several measured intervals around 50-52 FPS. There was no
separate instrumentation-off baseline for this version, so the user observation and the
instrumented single-change comparison should remain separate evidence.

Remaining measured GPU work is led by main guest rendering (about 9.9 ms/frame of sampled
encoder span), with fragment-stage spans about 6.2 ms/frame on the fused phases. These spans
include dependency and memory waits; they do not separate texture bandwidth/cache misses
from fragment arithmetic and must not be summed as frame time. Other depth/stencil and
post-processing copies still have genuine readers and were preserved.

After the user exited, extraEnv was restored to its prior empty value, frameStats and
GPU profile toggles were restored off, and the normal 0.2.16 launcher was reopened. No A/B
control path remains in the launch environment. Alpha fusion stays on by default; preset 3,
1080p output, effects and saves were preserved.
