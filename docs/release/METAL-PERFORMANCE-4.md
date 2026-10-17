# Direct scene-proxy color clear (0.2.13)

The 0.2.12 split-stage run in Hunter's Dream showed vertex-stage spans dominating the
largest six-output pass families (about 80-85% of those passes, not isolated vertex ALU).
First-pipeline family frequencies changed from the previous run, so retain their hashes
when interpreting the samples. A complete CopyImage overwrite encountered no dirty
destination proxies; its FPS fluctuation is not evidence of an improvement.

The copy UID/reason audit instead showed full-size transfer writes followed by proxy fills
every frame. Source inspection identifies fast-clear elimination and buffer-clear shader
HLE calling Runtime::ClearImage. Previously a dirty reduced image was resolved to native,
cleared there, invalidated, then filled back into the proxy. The old pixels are irrelevant
to a complete constant color clear.

Adapter patch 0014 clears an existing proxy directly for a full single-layer/single-mip
color clear. The image must remain eligible, have identical guest/backing/Metal formats,
and a single-sample renderable 2D native texture. It creates no proxy/cache entry just for
the optimization. Depth/stencil, mip chains, arrays, converted/special formats and ranges
outside the one complete color level retain their previous behavior. Clear values use the
same signed/unsigned/float conversion as the native clear. A later native reader resolves
the current proxy normally; a native writer still invalidates it and loading refills it.

The clear records a normal Metal load-action clear with existing barriers and scheduler
ordering, then marks the proxy current and dirty. Runtime still marks the image GPU-modified.
No game shader, BDA addressing, visibility query/history, game velocity, blur bypass, cache
capacity, resource-retirement rule, resolution or effect setting changes. Other native
read/write cycles remain: a read-modify-write operation cannot discard its input safely.

`BB_MTL_PROXY_CLEAR=0` restores native clear/resolve/refill behavior. The optional diagnostic
`BB_MTL_PROXY_CLEAR_FILE=/private/tmp/bbport-proxy-clear-ab.control` accepts one byte `0` or
`1`, polled at most once a second when an eligible clear occurs. Normal launches poll no
file. Cumulative `Metal proxy clear: direct=N` counts actual direct clears. A counter alone
does not establish an FPS gain; use a same-process, same-camera comparison.

Pixel validation compares the proxy path against native clears for RGBA8, sRGB, BGRA8,
signed/unsigned integer and RGBA16F formats, then verifies native reads and native writes
followed by proxy refills. Fallback, live switching, depth/mip rejection and the existing
velocity, blur, occlusion, BDA and lifetime regression groups must pass before installation.
