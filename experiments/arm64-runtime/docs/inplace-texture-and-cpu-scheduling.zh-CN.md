# in-place 纹理与生产线程调度检查点

## 实际导入路径的像素证据

新增 `tests/metal/test_texture_inplace_gpu.mm`。它调用真实 Metal GuestMemory::Install 和 chunk allocator，先通过 gpuAddress/buffer 双路径自检，再断言 BufferCache::ObtainBuffer 返回的对象确为导入 chunk 的 MTLBuffer。测试只用自制8MiB backing；guest VMA/physical translation为夹具，BB_IMAGE_TRAPS=0，不验证跨进程页保护/映射退役。没有游戏文件、窗口或正式缓存。

现有首次上传修复库下，零长度/无实际写入/首像素部分写/内部部分写仍未通过，整图实际写入控制通过。范围校验候选下，零长度/无实际写入/整图写入通过，两个真实部分写入仍漏更新。证据分别为 first-upload-regression-1792873733110023000、1792873772538688000。这比上一轮外部shared-buffer模型更接近当前Metal路径，但仍不是游戏战斗白模复现。

最初未关闭夹具不支持的image trap时，五例在stub处SIGABRT；完整覆盖控制也失败，不能算像素证据。补齐实际chunk、禁用未模拟trap并完成正控制后，才取得上述结果。未将范围校验候选启用。

## 有界游戏日志

`build_partial_audit.py` 创建独立签名的日志候选，保持失效条件、遍历范围和渲染行为。只在显式诊断gate开启时打印，最多64条。它记录**原函数当前首字节查询能看见的**GpuModified局部重叠，不是全地址空间、全写范围的完整别名审计。

- launcher-engine-abba-1792873922454962000：93s，原固定QA副本，进场17.79s；日志确认启用，0条候选命中。压力normal，swap较起点+1254MiB，无保护中止。
- launcher-engine-abba-1792874232663345000：此前手动QA正常存档另建副本，89.61s，进场16.83s；同样0条候选命中，压力normal，swap -32MiB。CPU/GPU trace完整，无覆盖/丢弃/时钟拒绝。

两轮都没有新截图/录屏，也没有独立确认敌人受击或白模，不能宣称视觉正确。自定义seed仅放行Shared/state内部的诊断/功能测试，拒绝savedata中的符号链接；实际源QA文件摘要保持不变。正式存档和设置从未参与选择。

第二轮抓到178.386ms帧：GPU22.06ms，记录5.732ms，compiles=0；FEX lookup/compile总窗口仅1.908ms，CPU0.039ms。渲染线程condition wait165.559ms；对应生产线程168.275ms区间CPU19.225ms。同期Codex Renderer约109% CPU。这说明不是该帧有178ms编译或GPU绘制，尚不能唯一归因于调度/内存或某个锁。

该暖缓存轮整个运行282次shader scope共515.17ms，其中Metal library共450.23ms、PSO共53.47ms，类别嵌套不能直接相加。最长单次shader14.645ms，全部在第一个动作请求前；动作阶段没有新library/shader/PSO。真实新场景的全新shader复杂度仍需另测，不能拿暖缓存替代。

## 只读调度计数

新增 process_schedule_audit.py，低频读取已归属QA的进程rusage v4。SDK编译证明结构296字节，关键字段偏移16/160/248/288；自进程CPU循环与实际API计数通过，保留原始累计值，不把缺失/失败当0，也不把全进程runnable值直接算成某个线程的off-CPU时长。公开XNU的task_power_info_locked也是累计任务/线程runnable计数：[Apple XNU](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/task.c)。SDK新增page_wait_time字段未找到足够公开语义，本轮没有用它排除内存等待。

launcher-engine-abba-1792874960290559000再次抓到95.37ms帧：GPU18.21ms，compiles=0，无FEX编译scope重叠；condition wait85.839ms、生产线程92.51ms区间CPU26.655ms。周围进程累计显示游戏CPU主要是legacy QoS，Codex Renderer约210%、backupd约21%。仍是相关性证据，不是唯一因果证明。

## QoS实验：保留、默认关闭

新增C/C++共用qa_cpu_qos.h，只允许inherit/user-initiated。默认不调用调度API。只在私有loader session和显式BB_ARM_QA_CPU_QOS_EXPERIMENT=1下接受选项；游戏工作线程及guest pthread入口显式准入，原生网络/音频callback池不改，不修改guest优先级字段或原启动脚本。错误不会返回75/76。

原生测试发现pthread_attr_init后的普通新线程没有自动沿用父线程user-initiated，故不能只在main设一次；现在测试父/子显式准入、默认无操作、非法输入和EPERM失败，以及同头文件C/C++编译。先发现并修正夹具/准入假设，再进行真实游戏实验。

CPU fbbe99cbbd38031e5bb355d1dbd8a6ea86e47f7502a42157a94d20cadcc0bbb5，cpu-qos-regression-1792875472222356000：117无窗口、7数据导入、19libc跨度、17退出码+6原监督器，以及QoS/调度计数测试全通过。源端时钟下溢保护也已随该CPU构建，不再仅是解析器修复。

launcher-engine-abba-1792875547709454000实际游戏准入：CPU约97.9%时间进入user-initiated，进场17.58s，两轮动作请求正常结束，未触发保护。但仍有140.108ms长帧，不能称卡顿已修。

随后同二进制、同GPU、同boot/content/patch/settings摘要及镜头做纯静态ABBA，关闭额外诊断/host sampler/Cua/录屏。每轮90s，取末30s，compiles=0。证据 launcher-engine-abba-1792875683322786000：

| 策略 | FPS | p99 ms | 聚合footprint MiB |
| --- | ---: | ---: | ---: |
| inherit A1 | 48.015 | 22.86 | 5841.23 |
| user-initiated B1 | 46.537 | 23.36 | 6128.78 |
| user-initiated B2 | 47.624 | 22.67 | 5823.28 |
| inherit A2 | 46.989 | 23.95 | 6091.84 |

A均值47.502、B47.081，B约-0.89%；未证明收益。两组聚合内存均值5966.54/5976.03MiB，差9.49MiB，但轮间波动约300MiB且聚合含共享页重复计费，不能认为精确物理RAM增加9.49MiB。四轮最高warning，未保护中止，无画质/分辨率/效果/缓存预算变化。静态场景不替代探索的低帧结论；保留实验入口供后续CPU受限场景研究，日常QA配置不启用。

复测命令（仅准备好的本机隔离树）：
```
python3 tools/run_launcher_engine_abba.py --order arm64,arm64,arm64,arm64 --seconds 90 --gpu-variant first-upload --cpu-qos-order inherit,user-initiated,user-initiated,inherit
```

## 下一步

优先把生产线程低CPU的长间隔拆清楚：游戏任务依赖/宿主调度/页等待，避免继续把lookup cache hit称为长编译。局部纹理模型已有确定证据，但在现有两轮游戏中未关联实际白模；需要实际命中/白模现场或更接近敌人交互的隔离场景，再改局部数据权威源和合并。

死亡重生、在线、菜单继续按用户要求暂缓。硬件TSO仍未准入，软件排序不变。Cua最小化审批未获新答复，未启动或绕过。正式版/Release不更新。剩余额度约48%，低于15%停止并交接。


## 最新可玩隔离包

应用：`apps/Blood Echoes ARM64 GUI QA 1792876149743662000.app`。归档：工作区`artifacts/arm64-qa-20261025-current/Blood-Echoes-ARM64-Local-QA.zip`，42,809,121字节，SHA256 `d3a1e3e2f7876e2016cfe9108262cbf19e3c025e084fdaef86839e609f530987`。原包保留。仍依赖本机Shared树，不是便携安装器，不含游戏/存档/缓存/凭据。原GUI版本号仍0.2.51，身份由QA时间戳与manifest确定。

CPU输入fbbe99，冻结后因依赖改写/重签为593f1e219e25988d0eec5bd7cb2440e89ee590c10629d44bbd9fc67fd108e10a。frozen-qa-package-1792876282546725000证明解压严格签名、4个真实FEX退出fixture、arm64 Python与手动物理输入配置；偏好中未含QoS实验变量，online=false、cloudSaves=false（离线）。随后launcher-engine-abba-1792876359653644000实际冻结组件进场21.38s，90秒正常结束，末段49.08FPS、GPU19.16ms、聚合5837MiB、最高warning、swap+196.5MiB。单轮只作可启动与功能回归，不归因性能提升，也未验证中文视觉、F3/死亡/在线/菜单。

qos-checkpoint-20261025正式11671文件快照与目标开始基准完全一致；ABBA源QA正常存档摘要亦一致。Release没有更新。
