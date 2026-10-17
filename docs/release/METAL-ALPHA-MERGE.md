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
