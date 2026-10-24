# 探索卡顿：慢编译只读调查

本轮未启动游戏或 GUI，未改源码、二进制、QA/正式设置或任何现存缓存。分析的是保留的三代 F3 日志和当前隔离 GPU 缓存；不构成新一轮性能测试。完整统计在 analysis.json。

## 已确认

1. `Slow compile` 来自 GPU 的 `pipeline_cache_core.cpp:374–390`。shader 计时覆盖 GCN 解码/IR/发射 SPIR-V、MSL 源码缓存查找、MSL 转换/重写以及 Metal library 编译；pipeline 计时覆盖同步管线创建。**不是 FEX CPU JIT 的计时。** 目前不能把 326.6 ms 全部归给 Apple 编译器。
2. 三代慢项数为 2、0、14。最后一代 VS 3 项共 547.5 ms，FS 5 项共 556.6 ms，PSO 6 项共 493.2 ms，总 1597.3 ms。慢项原样身份在三代中均不重复。阈值为 16 ms；没有日志的快速编译或不同的运行期 permutation 顺序不能据此排除。
3. 最慢 VS `0x9c893ebf/0` 为 326.6 ms；FS `0x44a82ffb/0` 为 209.7 ms；PSO `0x8a097c85c7d112d1` 为 188.4 ms。对应 shader 的 .metal 和 .spv 修改时间为本次探索中的 22:50:06 / 22:50:12，而旧缓存仍保留 10 月 23 日 23:44 的文件。结合不同慢项和缓存持续新增，首先支持**首次遇到未热身的 shader/特化/PSO，同步在绘制路径编译**，没有支持整套缓存每次 F3 丢失的迹象。
4. GPU 实际目录是 `state/gpu-stutter-fresh-1792770276384734000`，不是 QA 游戏 user 目录。核心缓存 `cache/CUSA03173-metal-relaxed-pt2-img2d-v2-arm64-srt1` 有 450 SPV、450 meta、367 key、1 profile，约 11.70 MiB；MSL `cache/CUSA03173/metal` 有 447 文件，约 6.53 MiB。日志未见兼容性重建、损坏重建、二进制归档不可用警告；INFO 级 preloaded 数并未记录，因此不能仅凭文件存在宣告所有项都命中。
5. 此前同入口 ABBA 的两轮 Rosetta 和两轮 ARM64 均使用上述相同 GPU 缓存目录、同一 GPU baseline SHA `2da2c735...`，以及 `LIBRARY_REUSE=0`、`PERSIST_WARM_ARCHIVE=0`、`RELEASE_WARM_ARCHIVE=1`。当前 QA prefs 和原脚本也是这组策略。不是本轮替换 ARM CPU 时独自关闭了这些 GPU 选项。当前人工探索路线与此前站桩 ABBA 不同，不能拿两者帧数证明退化幅度。

## 各缓存层到底省什么

- 核心 `.meta/.spv/.key`：保存特化信息、SPIR-V 和管线描述，供 WarmUp 还原已知工作。未遇到的新特化仍要生成。版本/Profile 与 ARM SRT 布局有独立 namespace，不能直接把 x86 ABI 序列化内容混进 ARM namespace。
- `.metal`：**MSL 源码和绑定元数据，不是已编译 metallib**。`mtl_shader.mm:1275–1291` 在源码命中后仍调用 BuildLibrary；`:950` 仍是 `newLibraryWithSource`。可省 SPIRV-Cross，不能承诺省驱动编译。
- 运行期 module/PSO maps：同进程命中直接复用，不受纹理回收影响。未发现正常路径主动清空它们。Metal PSO map 的创建前/提交后锁没有覆盖驱动编译，原则上允许并发重复创建后丢弃，但本次 Metal 同步绘制路径没有相应并发证据。
- `MTLLibrary memo`：现有 `mtl_library_memo.h`，当前开关 `BB_MTL_LIBRARY_REUSE=0`。按完整有效 MSL、entry、数学选项做 SHA256，最多 1024 元数据槽、弱引用，不增加 MTLLibrary 持有期；同项并发 single-flight。**不是缺少实现，无需另写一套。** 当前源码 447 文件只有 387 种 MSL，48 组重复、60 个同源冗余文件；`d4b5fbf2` 两个变体和 `6a833138` 三个变体分别同源。它可能减少同进程重复 driver API 调用，但最慢的 9c893ebf/44a82ffb 在此缓存中没有同源副本，不能宣称单开 memo 就会去掉这两次最大停顿。默认禁用原因是旧实测收益不确定，GUI 已列实验性。
- `MTLBinaryArchive`：能够在完全匹配时免 PSO 驱动编译；当前与旧配置相同，磁盘持久化开关关闭，预热后释放临时编译归档。新探索内容没有长期持有归档兜底。这是降低驻留内存的已接受策略，不宜为卡顿直接开全量归档：原 UI 明确记录曾多约 0.6 GiB 进程内存。已有单个 compute PSO 微基准中，热 OS cache 的无归档/归档均约 0.5 / 0.17 ms PSO，整体进程启动时间无收益，不能据此推断冷复杂图形 PSO 没收益，也不能当作全游戏验证。

## 异步/锁与 FEX

- `MakePipelineCapabilities():135` 明确 `vertex_input_dynamic_state=false`，Metal vertex descriptor 本就是 PSO 的一部分。
- `AsyncSkippable()` 因上述 cap 直接 false，所以现有 `BB_ASYNC_PIPELINES` 开关即便未设 0，也**不会让 Metal 的这些慢项异步**。不能伪装 cap 或直接使用跳过绘制的 Vulkan 策略；这会影响顶点输入和新内容可见性，不满足画质要求。
- PipelineCacheCore 的 GetProgram 新 shader/新 spec 在 GPU 线程同步 CompileModule；draw preparation worker 只读已缓存 shader，miss 交回有序线程，不会安全地预编译新项。
- 现有 FEX 追踪还有独立的首次执行问题：每代约 3890–4064 条 UNALIGNED_FIX，最后一代 3986 条/3971 个不同 host PC。绝大多数是新 PC 的一次性修正，部分修正在慢编译附近，但日志没有统一时钟，不能相加或判断它比 Metal 更大。三代 BUFFER_REUSE 各 50 行（两次 startup 批次）均早于探索慢编译，不支持探索期间反复整体清 JIT 这一说法。需短门控 wall/thread CPU 计时区分真实 FEX codegen、异常修正、GPU等待。

## 根因排序与最小下一步

1. **目前最强证据：新探索/战斗内容首次 shader + 新 PSO 同步编译造成可见尖峰。** 已记录 200–326 ms 的 shader 和 188 ms 的 pipeline 足以造成瞬间卡顿；尚不证明相较旧 Rosetta 的全部额外卡顿均由它造成。
2. **FEX 首次执行/未对齐访存修正的叠加成本**：数量明显，但缺 CPU/wall 时间；不可用 Slow compile 日志替代 FEX 归因。
3. **相同 MSL 多特化的重复 driver library 调用**：源码确认存在、现成弱 memo 可复用，是不增持有期/不缩缓存的最小候选，但不是最大慢项的完整解释。
4. 暂无支持“磁盘缓存全丢”“同一 shader 在同代不断慢重编”“归档失败造成全部卡顿”“纹理 reclaim 会清 shader cache”的证据。

建议先让当前隔离入口短暂使用已有 **BB_STUTTER_AUDIT + gate**（固定 1 MiB 环形元数据，不录屏，不读资源负载），覆盖同一段首次探索和第二次热路线，拆 MetalLibraryBuild/MetalPSO 与 FEXBlock/IR/Codegen 的 wall/thread CPU；保留画面和原缓存。随后只对现有弱 `BB_MTL_LIBRARY_REUSE` 做 ON/OFF/ON/OFF 同路线对照，记录 builds/reuse/waits、尖峰与去重内存；不用重写 memo、不扩缓存、不持有更多 GPU 对象。若最大停顿仍是唯一新 library，应另评估短窗口、有界、完整 key 的预编译队列，而不是跳绘或把全量 archive 常驻。

**诊断接线注意：** 原 `run_optimized.sh:149` 在普通 Play 模式会 unset `BB_MTL_COMPILE_AUDIT`。仅把它填进 GUI extraEnv 不会生效。不要为了保留该变量误开要求独占脚本输入的 benchmark。现成 gated stutter trace 不在该 unset 清单；如需详细 compile log，必须增加明确隔离 QA 的诊断透传并验证，不能改变正式入口策略。

未证明：真正 Metal driver compile 占 shader 326 ms 的比例、OS compiler cache 命中率、并发锁等待占比、热重走路线是否消除尖峰、memo 对玩家路线的收益、白模与编译有因果关系。此次没有修复/启用任何候选。
