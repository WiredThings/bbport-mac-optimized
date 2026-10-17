# Metal render parts and full-copy coherence (0.2.12)

0.2.11's same-process ABBA run in Hunter's Dream averaged 38.53 FPS with the previous
copy policy and 38.72 FPS with clear-fill elision. No clear-fill opportunities occurred,
so the 0.49% difference is not evidence of a performance improvement. Two six-color-target
pass families occupied about 5.7 ms/frame of sampled spans; host copies about 2.2 ms/frame.
Those spans include dependencies and may overlap other work.

Adapter patch 0013 separates vertex, fragment, overlap and inter-stage gap timestamps
without creating extra passes or samples. The same bounded profiler additionally ranks
copy labels by resolve/fill reason, source/target dimensions, format and mip. A bounded
numeric UID/mip audit identifies actual copied resources. Instrumentation remains off
by default and shares the existing four-slot counter retirement rules.

A source audit found that Runtime::CopyImage previously resolved a dirty destination proxy
before immediately overwriting every native pixel. The new path discards the obsolete
proxy contents only after the source view is valid and a complete copy is proven:
distinct image/backing, disjoint texture roots/buffers/IOSurfaces, single-sample 2D,
one mip/layer, identical dimensions and actual Metal pixel format, and the destination
is exactly the image's native texture. It then records the existing native blit.
All partial, aliased, unsupported, multi-level/array/MSAA and format-mismatched cases keep
their existing coherence path. Color/depth conversions and uploads are unchanged.

The target proxy is invalidated, so subsequent reduced-size reads/attachments refill from
the copied native pixels. Other mip levels are never discarded. No cache budgets, image
quality, resolution, game shader, two-level BDA mapping, motion-vector path, blur bypass,
visibility history or resource-retirement rules change.

`BB_MTL_OVERWRITE_DISCARD=0` restores the previous copy policy. An optional
`BB_MTL_OVERWRITE_DISCARD_FILE=/private/tmp/bbport-overwrite-ab.control` reads one byte
`0` or `1` at most once a second for same-run A/B. Normal launches poll no file.
`Metal overwrite discard` reports cumulative candidates, dirty candidates and actual
skipped resolves; candidates alone are not a performance benefit or memory saving.

Validation includes real GPU pixels for complete overwrite and proxy refill, partial-copy
untouched pixels, aliases, unsupported format preservation and runtime policy switching;
core mip-local invalidation and hardware timestamp decomposition; and the existing velocity,
occlusion, BDA remapping and memory retirement regressions. Gameplay benefit requires a
same-scene measurement with actual skipped-resolve hits. Do not infer it from code removal.
