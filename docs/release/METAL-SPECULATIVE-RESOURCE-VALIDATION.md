# Speculative resource-format validation

## Failure and evidence

A gameplay log ended in `MapNumberConversion`, `data_fmt = 15`, followed by the GPU shim's explicit exit 23. This is an assertion-driven process exit, not a memory-guard termination. The original log has no call stack, so it cannot identify the original caller or prove that death/respawn caused the invalid field.

The draw-preparation workers read guest resource tables speculatively before the ordered GPU command thread. `GetProgram` already treats an unreadable guest pointer or an unmatched specialization as an unavailable preparation result. However, `StageSpecialization` previously called the strict number-conversion mapper for formatted V# buffers and T# images. A readable transient descriptor with `SnormNz` and unsupported format 15 terminated the entire game instead of returning unavailable.

A no-GPU regression calls the real `PipelineCacheCore::GetProgram` worker branch with such a table. The old renderer exits 23 for both buffer and image cases. The patched renderer rejects the worker result; after the table contains a valid format, the ordered lookup and a subsequent worker lookup reuse the original module. Program, module and pipeline cache counts stay unchanged. This establishes the prefetch failure mechanism, but does not retroactively establish the thread/descriptor that caused the original live exit.

## Upstream corroboration

[deadinside28/bloodborne_pc PR #166](https://github.com/deadinside28/bloodborne_pc/pull/166) captured `bb:DrawPrep2 → GetProgram → StageSpecialization` with an impossible `SnormNz` buffer descriptor. The author identified guest table rewriting during speculative reads. The PR was closed because the maintainer adapted its fix into [0.6](https://github.com/deadinside28/bloodborne_pc/releases/tag/0.6), not because it was rejected. The current upstream [`gpu/gcn/formats.h` at `8185b88`](https://github.com/deadinside28/bloodborne_pc/blob/8185b88d80573fa300595d5c54450183b4109935/gpu/gcn/formats.h) returns no conversion for that impossible combination.

This branch implements a narrower boundary on its pinned Metal engine: explicitly reject the worker result while retaining ordered-path validation. It does not import the upstream renderer reorganization or change any valid conversion.

## Change and safety boundary

- `TryMapNumberConversion` provides a nonfatal query for speculative readers. The existing strict mapper retains every conversion and continues to reject unsupported input on the ordered path.
- A speculative specialization with an unsupported conversion is invalid and never published as a prepared draw. The command thread reads the guest tables again through the existing fallback.
- Speculative color-target key construction uses the same nonfatal check. No format is substituted, no geometry is dropped to recover, and no invalid specialization is inserted in a cache.
- At most eight malformed buffer/image preparation reports per process include the shader hash and raw descriptor words. The assertion shim records its current thread and a bounded stack only when an assertion occurs.
- No saved shader/pipeline/resource structure, cache version, resource budget, attachment behavior, page-table lookup, motion-vector shader, occlusion path or network module changes. Existing disk caches remain compatible; no cache clear is requested.

The regression exhausts all 1,024 combinations of the 4-bit numeric and 6-bit data-format fields. All 966 previously supported combinations retain their conversions, and the 58 unsupported `SnormNz` combinations are refused only by the speculative query. Strict-invalid tests still exit 23. Valid ordered/speculative specialization keys compare equal.

## Running the regression

After the normal source dependency setup:

```sh
bash tools/macos/build_metal.sh
cmake --build out/gpu-metal --target speculative-descriptor-test --parallel 2
out/gpu-metal/speculative-descriptor-test
```

No GPU device, window, game files, saves, cache directory or server access is required for this target. Explicit strict-invalid cases intentionally exit 23:

```sh
out/gpu-metal/speculative-descriptor-test buffer invalid strict
out/gpu-metal/speculative-descriptor-test image invalid strict
```

Live fixed-scene observation is a compatibility check, not proof that every rare gameplay crash is eliminated. If an ordered-path assertion recurs, the new thread/stack diagnostics are needed before changing its interpretation or suppressing it.

## 中文

该修复解决已用真实管线入口复现的后台预处理容错缺口：读到可访问、但暂时无法解释的资源描述符时，拒绝后台结果，交回原有顺序路径重新读取。不会把格式 15 强制改成某种纹理格式，也不通过丢弃绘制掩盖错误。旧现场没有调用栈，原始闪退的具体调用线程仍不能追溯确认；再次断言时将记录线程与调用栈。存档、设置、现有缓存和缓存容量不需要修改。

## Local compatibility observation

Four isolated three-minute fixed-scene samples retained the same save, camera, resolution, effects and five existing experimental selections. Old-library samples averaged 43.45 FPS (45.48, 41.42); patched samples averaged 43.46 FPS (41.21, 45.71). This does not establish a performance improvement. All accepted runs were free of GPU assertions; no malformed-format fallback occurred during them, so they do not prove the original rare exit is eliminated.

Process-footprint medians varied across runs: old 5,773/5,498 MiB, patched 5,691/5,939 MiB. The highest sampled footprint was about 5,990 MiB old and 6,003 MiB patched. These sequential trials do not establish a zero change in median footprint. The patch introduces no persistent GPU allocation or cache-budget expansion; source analysis and deterministic cache-count tests are separate from this noisy residency observation. Critical-pressure interrupted startups are rejected and excluded.

Validation also covers the 1,024-value format matrix, real worker rejection/ordered retry without module growth, four motion SPIR-V modules, Metal pipeline/core/runtime/raster/scene targets, GPU address tables, resource lifetimes and hook ABI, scheduler, and the offline/online quickload and routing regressions. This is not a new live login, co-op or invasion test.
