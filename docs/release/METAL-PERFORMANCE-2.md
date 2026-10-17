# Metal scene clear efficiency (0.2.11)

The 0.2.10 diagnostic run in Hunter's Dream, facing the Yharnam headstone, averaged
38.9 FPS at 960x540 scene rendering and 1920x1080 output. Sampled GPU spans estimated
13.70 ms/frame for guest rendering and 2.45 ms/frame for host texture copies/scales;
game velocity draw, depth, merge and packing together estimated 0.39 ms/frame.
Native encoder boundary CPU work was 2.99 ms/frame, with about 305 physical encoders/frame.
Spans overlap and are sampling estimates, so these figures do not add up to frame time.
Earlier measurements at another loading point are not a valid FPS comparison.

Adapter patch 0012 eliminates a redundant old-content copy into a reduced scene color
target when its actual Metal load action will clear the entire attachment. A Metal load
clear covers the full texture even when the following draws use a smaller scissor.
Only eligible reduced attachments sharing the complete proxy extent opt in, before
UI/display redirection. Depth/stencil retain their existing preservation path, including
partial depth or stencil clears. Normal loading attachments still refill after native
writes; native reads still resolve the current proxy. Vulkan's default adapter is unchanged.

The default is enabled. `BB_MTL_CLEAR_DISCARD=0` restores the previous copy policy.
For same-process diagnostic comparisons, set the launcher's explicit extra environment to
`BB_MTL_CLEAR_DISCARD_FILE=/private/tmp/bbport-clear-copy-ab.control`. A file containing
one byte `0` or `1` switches policy for future attachments, polled at most once per second.
This optional path does not reset caches or reinterpret already recorded resources.
When unset, normal rendering never polls a control file. Logs count `skipped-fills`;
these are eliminated copies, not measured GPU milliseconds or memory savings.

No cache capacities, resolution, visual effects, shader addressing, guest allocation hooks,
GPU retirement rules, velocity processing or visibility history are changed. Two-level BDA,
motion-blur bypass and occlusion continue through their existing implementations.
The profiler additionally ranks bounded pass labels to refine the remaining guest-render hotspot.

Verification includes core coherence tests and real Metal pixel checks for full clears,
native reads/writes, loading contents, fallback and live policy switching. The existing
velocity, blur-copy, visibility overflow, BDA remapping, memory budget and resource retirement
regressions must also pass before installation. Gameplay FPS improvement remains unverified
until the same camera is measured with both policies in the same scene.
