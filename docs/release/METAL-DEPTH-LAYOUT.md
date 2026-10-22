# Depth/stencil layout continuation

Experimental `BB_MTL_DEPTH_LAYOUT`, default OFF in 0.2.49. This is separate from consumed-clear continuation (0071).

## What is actually redundant

Metal's physical render-pass descriptor does not have a Vulkan image-layout field. The backend's exact `RenderState` comparison includes that field. Some requested layout changes can therefore reopen an otherwise identical encoder. Three consecutive diagnostic frames showed ten depth/stencil-only changes per frame; just three had no recorded barrier/hoist/tail blockers and known current texture bindings. This was eligibility evidence, not permission to merge them.

Patch 0082 marks capture-initiated boundary-drain windows explicitly. Requested state splits run on the recording thread, whereas physical encoder events may come from replay; they are not interchangeable timing records. A zero blocker mask does not prove all resource access is safe. The general buffer-use audit still reaches its 1024-entry bound.

## Conservative continuation policy

Patch 0083 permits only known **read-only to writable** plane promotions. All other attachment fields, views, dimensions, layers and clear values must match exactly; no fresh color/depth/stencil clear is accepted. Write-to-read-only changes always preserve their boundary: a later draw in the read-only phase could begin sampling the attachment after the present draw's texture check.

Both planes must be present. Every attachment must have an independent private root, excluding heaps, buffer-backed textures and unresolved view ancestry. Known directly bound shaders are required. Tracking covers previous texture bindings, including aliases later unbound, and the caller checks the current draw's texture list. Existing binding records avoid rechecking unchanged bindings; no additional memo or GPU allocation is added.

Recorded barriers, pending fences, hoisted work, render tails, pending/folded clears and closed/query-snapshot boundaries retain their original paths. All command writes, query-write epoch notifications, two-level BDA addressing, velocity/motion work and cache budgets are preserved. Unknown/synthesized/raw-address shader paths remain conservative. Full write ownership and alias coverage for the separate 192-byte upload investigation are still unresolved.

## Visibility counts require more than identical attachments

The occlusion resolver scales and rounds each physical pass independently. At 720p the factor is 2.25: `ceil(1*2.25)+ceil(1*2.25)=6`, but `ceil(2*2.25)=5`. Raw counts may be additive while the scaled values are not.

Unknown visibility callbacks forbid the new continuation. The production callback explicitly selects native-density policy. With a callback installed, reduced-size depth/stencil targets retain their split; at/above the native 1920x1080 pixel count the existing resolver uses scale 1. A separate explicitly additive policy exists for synthetic raw-counter tests. No query buffers are resized, and snapshots still close and join the scheduler before resolving counts.

## Validation and measurements

- 355 CPU state/policy checks, including every legal plane promotion, rejection of every unrelated state byte, unsupported layouts, dimensions and the per-pass rounding counterexample.
- Actual M4 Metal API Validation: candidate ON/OFF × held/direct encoding, exact final pixels and visibility sums, depth and stencil rejection, prior attachment feedback, feedback later unbound, parent views, unknown access, barriers, tails, closed snapshots and query-policy rejection.
- Existing consumed-clear GPU tests also pass with the new flag ON and OFF.
- 13 launcher optimization tests cover the new default-OFF control, persistence, migration, backend isolation and three languages.
- A bounded in-game native diagnostic confirms actual continuation, with a separate fixed-view image spotcheck. These diagnostic runs are not FPS results; external recording state is unverified.

The initial 172 prototype, before density-policy and binding-check refinements, measured 45.288→45.081FPS (−0.457%) in four interleaved runs. The OFF group itself varied by roughly1.45FPS. This did not establish a performance benefit or a repeatable regression. Its native allocation difference is not a causal memory-saving claim. The refined 173 comparison used the same binary and private fixed camera in ABBA then BAAB order (30 s warmup + 45 s scoring per run). Do not pool its results with prototype 172.

| Refined candidate | OFF | ON |
| --- | ---: | ---: |
| ABBA mean FPS | 46.285 | 45.808 |
| BAAB mean FPS | 45.333 | 45.820 |
| Eight-run group mean FPS | 45.809 | 45.814 |
| Mean of run 1% lows | 34.627 | 32.992 |
| Mean of run median footprint (MiB) | 5745.04 | 5772.42 |
| Mean of run native allocation medians (MiB) | 6030.02 | 6043.75 |

The pooled FPS difference is +0.010%, effectively neutral in this sample; order-specific results disagree. This does not establish a gain or a repeatable low-frame regression. The footprint difference is +27.38 MiB, not a memory-saving claim. Native resources/cache budgets are unchanged. All eight runs completed, had fixed camera and zero scoring-window compiles, and preserved original save hashes. Pressure was warning-level throughout scoring; swap did not grow during the scored windows. All eight used the normal 6-second rapid-swap observation window. The one authorized 30-second exception was consumed by the separate 173 diagnostic, not used by this suite. Severe pressure, lock/session and capacity guards stayed enabled. No visual automation, frame readback or native frame capture ran inside scoring windows.

No formal saves, launcher preferences, game configuration or formal cache are modified by this investigation. Private probe code, World Warper, test saves/routes and captured user data are not public package payloads.

The final candidate ON completed two isolated native SetHp death/respawn cycles (HP zero, loading/world-down, stable positive HP), with valid mapping inventories before/after each cycle. The owned game exited normally and original save hashes were unchanged. This is not enemy-combat or online coverage; the fixed-scene comparison had MetalFX/object motion inactive, so it is not full active motion-path coverage. No new texture-format or Metal command-buffer failure appeared.
