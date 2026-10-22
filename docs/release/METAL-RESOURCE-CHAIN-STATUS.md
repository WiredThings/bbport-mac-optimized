# 资源访问与生命周期：当前结论

目标仍在进行：追清重复的 192 字节写入、复用地址及保留纹理的实际使用情况，
只省掉有安全证据的工作。已验证改动整理为 0.2.48；正式存档、设置和缓存不参与测试或迁移。历史章节中的“未发布”描述对应当时状态，当前交付见 [0.2.48](0.2.48.md)。

## 已确认的结果

| 项目 | 当前结论 | 验证边界 |
| --- | --- | --- |
| 重复上传的产生路径 | 三帧 105/105 条关联到 SDK 全局表置脏、命令快照、解码和 Metal 写入，每帧 35 条。 | 产生链明确；完整 CPU/GPU 消费与别名覆盖尚未完成。 |
| 能否按 CPU 影子相等跳过上传 | 不能全局这样做。换绑新表、另一上下文改写共享表后仍需上传，已用原 SDK 反例验证。 | 所有原写入、脏标记和同步保留。 |
| 16 张旧纹理 | 共 46.207 MiB，原标签已释放，但每张都与新分配部分重叠；14 张仍标记 GpuModified。 | 无单个完整覆盖者不代表无活跃字节；这组实际物理区间与小表分离。不能按年龄直接释放。 |
| 0071 通道延续 | 省掉每帧两次已消耗清除后的重开，开发源码及私有 QA 默认开启。 | 固定圣杯场景 ABBA：56.41→57.54 FPS，约 +2.01%；1% low 未改善，不是亚楠中心。 |
| 0073 查询写入失效通知 | 查询直接写 GPU 缓冲区时遗漏版本更新，旧逻辑会命中已过期的同批次快照。开发源码及私有 QA 已修复。 | 两种查询链真实 GPU 写入和逐字节快照回归通过；不证明其它写入全部覆盖。 |
| 游戏 mprotect 与 GPU trap 组合 | 旧 VMA 权限会把新不可访问页变可读，或错误阻止新读取。开发源码已改为使用本次新权限。 | 真实运行时 75 例，旧实现失败 13 例、修正后全通过；CLEAN88 当前源码组合的两次隔离受控死亡回归通过；不是所有权限变更的覆盖。 |

0071 的 footprint 组均值约 +1.71 MiB；Metal owned/allocated 约 +79 MiB，存在单轮差异。
0073 四轮对照为 57.84→58.68 FPS，但旧行为自身相差 1.70 FPS，不能确认提帧。
该轮 footprint 均值约少 97 MiB，尚无因果证据，不称为内存优化。两者均未降低画质或缓存容量。

原生填充 0049、释放 storage 背板 0052 仍默认关闭。六项安全但收益不确定的候选已恢复为实验选项、默认关闭；已证实不正确的算法没有恢复。

## 写入追踪原型：只在隔离游戏中观察

旧 GPU Protect 会直接覆盖其他保护位。私有原型改成只原子更新自己拥有的状态，复用已有
稀疏表。648 种组合全部通过；旧接口失败 290 例。并发三轮，旧接口出现 51/36/90 次意外
放开写权限，私有组合实现各至少 5000 次尝试均未放开。这只验证已测保护所有者的组合。

观察器遇到故障时，必须在锁内再次使旧追踪结果失效。定向测试中，保留该处理的 32 次
故障/重新启动追踪没有漏报；删除它的负对照 32 次全部错误地把写后数据当成未变。
这不等于任意线程、重映射和故障分派已全部验证。

CPU77 实际捕获的邻近复制为小表 +368 处 16 字节，以及 +832 处 256 字节，均与小表
处于同一 4 KiB 页且不覆盖小表。重放两种形状、两类别名共 400 次，目标 192 字节始终
未变，但按页证书每次均失效。因此必须保守回退，不能将页保护直接视为字节范围的未变
证明。它也不证明整个方案无收益：这些 CPU 事件的频率和顺序不能直接当成 GPU 执行
顺序或实际可跳过上传的比例，尚未采集游戏内命中率。

CPU 只读保护挡不住实际 GPU 写入，已在本机 arm64/Rosetta 共享 Metal 缓冲区验证。
仍需处理：重映射/追踪启停、主机背板别名、所有 GPU 写入及其顺序、直接 mprotect 的诊断
工具。`BB_VRAM_ACCESS_TRAP` 只要环境变量存在就启用，值 `0` 也必须拒绝混用。
完整读取覆盖仍有缺口，不能把没有记录到读取解释成没有用途。

重映射准入完成 11 项正例：地址换绑、取消追踪后的新权限、晚到别名、释放、跨页写入及
嵌套锁。去掉映射代次检查的 5 个负对照全部错误保留有效状态。随后发现私有观察器在
“持有读锁、另一线程等待写锁”时会因递归读锁死锁，独立程序由 12 秒看门狗退出。
私有修正复用已有锁，13 项映射/锁测试及 15 项写入测试通过；完整 GPU 故障分派仍未验证。
这不是正式游戏已发生的死锁，也不是新性能优化。本轮探针已使用 CPU82 修正版，不能
沿用 CPU80/81 的旧观察器。证据：`cpu82-mapping-and-inherited-lock.json`。


## 首次隔离游戏采样（CPU83）

只记录、不跳过上传的私有探针已在冻结圣杯场景运行。连续三帧（3902–3904）各 35 条
写入，105/105 条 SDK→提交快照→任务→Metal 关联完整。观察器首次建立后，102 个
CPU 产生点区间有效，2 个因预告写入失效，在第 2、72 个调用重新建立追踪；首次调用
另计。只注册到 1 个游戏虚拟别名，另保护了主机背板。CPU 有效不等于 GPU 数据未变，
不能将这 102 个区间当作可省掉上传的数量。最后产生点计数也不是退出完成栅栏。

每帧仍有 19 条未知 GPU 写入（涉及计算着色器 `0xfefebf9f`），都标记逻辑版本 0。
这只是日志顺序，不证明它们实际在第一笔上传前执行完。通用资源使用日志也触及原有
1024 条上限；目标专用链完整不能代替完整通用资源依赖。所有上传及同步均保留。
16 张保留纹理仍共 46.207 MiB，14 张 GpuModified，全部部分重叠活分配且与小表物理分离。

第一次启动在进入场景时被原快速交换保护停止，未取得目标记录；正常压力恢复后只重试
一次，剩余总增长预算收紧为 1070 MiB。重试采样完成，两轮累计最大交换增量约 1150 MiB，
未越过 2 GiB 上限。重试峰值进程 footprint 约 5483 MiB、最高警告级，退出后压力正常。
这是诊断结果，无 FPS 或内存收益结论。测试进程已清理，正式应用/设置/15 个存档未变。
证据：`cpu83-observe-only-game.json`、`validation83/`、`formal83-after.json`。

## 同窗口 GPU 指针复核（GPU84）

同一私有包联合采集三帧（3776–3778）：复制计算着色器共 106 次实际派发，GPU 指针诊断
覆盖全部派发，当前/游戏 BDA 视图均有效，无命中这 192 字节的读写，无计数溢出。每帧
控制列表记录数与 GPU 计数记录数一致，第三帧确有 36 次复制派发，未硬套固定 35 次。
CPU 侧首次建立后 103 个区间有效、1 个预告写入使其失效；全部 105 条上传关联仍保留。

原先每帧的“19 条未知写入”是存储缓冲区访问声明，并非 19 次独立 GPU 存储。
两条通用声明来自 `BindResources -> AccessGlobal` 的依赖记账；源代码没有在那里发出
额外 GPU 写命令。零命中只覆盖该复制着色器在当前/游戏 BDA 视图中的已监测指针表达式，
不能排除任意额外物理别名或其它窗口，也不能直接把 CPU 产生点区间当作 GPU 执行区间。
控制列表精确范围只支持其中 31 次，另 75 次仍需保守回退，未启用范围缩窄。

实际写入还可同时更新 mirror 和 target 两个目的地；只证明其中一份未变不够。
这是 GPU84 当时的后续方向；RACE85 已否决仅增加执行前检查的方案，不再为此采样。
诊断固定计数缓冲 32 KiB、完成后读取计数，包含等待和临时管线，不是性能结果。
两阶段累计交换峰值增量约 1230 MiB，保留 2 GiB 总上限；进程已退出、压力恢复正常。
正式应用、设置、15 个存档和正式缓存保持不变。证据：`gpu84-pointer-and-cpu-joint.json`、
`validation84/`、`formal84-after.json`。

## 直接省掉上传的候选已拒绝（RACE85）

真实页保护观察器与 Metal 事件门控复现了检查到执行的竞态：同一个 Metal 缓冲区、
相同物理背板，先完成上一笔内容 `0xA5` 的 GPU 写入，再建立有效 CPU 追踪。第二笔
写入提交后、GPU 放行前，CPU 写成 `0x6B`；观察器立即正确报失效。保留第二笔写入时
最终为 `0xA5`，省掉则留下 `0x6B`，并逐字节检查了全部 192 字节。

因此“提交前证书有效”不足以授权删除 GPU 命令；事后报失效补不回已省掉的命令。
这不是实际游戏已发生该竞态的证据，也不证明所有省写算法都不可能；仍可通过真正的
GPU 排他所有权证明排除此类写入，但当前追踪没有提供该保证。该候选明确不启用。
原生执行点桥接库虽已私有编译，未接入游戏；仅增加执行前检查不能解决这个问题，
本轮不再为此启动游戏采样。后续继续资源所有权/别名与旧纹理生命周期调查，保留全部
写入，优先处理不依赖“内容未来不会变”的复制和通道整理。

证据：`race85-precheck-elision-rejected.json`、`validation85/`、`formal85-after.json`。

## 当前交付与验证

- 正式 renderer、设置及 15 个正常存档与调查前一致；最新只读核验为工作区 `formal90-after.json`。
- 0071 已完成两次隔离受控死亡重生（SetHp），不等于敌人伤害路径验证。
- 0073 完整原生 Runtime 两种查询链、实际快照失效/字节回归及四轮私有 benchmark 通过。
- 保留补丁 70/70 可重复应用，benchmark 工具 54 项测试通过。
- 无游戏的权限回归可运行：`python3 tools/macos/test_memory_protection.py`，共 75 例；
  执行权限提升被 macOS 拒绝时验证原状态保留，不测试执行指令。
- 写入追踪原型、原游戏函数探针和 QA 应用仅在仓库外，不是公开源码构建依赖。

证据索引：工作区 `cpu77-copy-backing-provenance.json`、`cpu78-write-version-and-query-fix.json`、
`cpu79-protection-ownership.json`、`cpu80-legacy-protection-composition.json`、
`cpu81-lifecycle-and-neighbor-fallback.json`、`cpu82-mapping-and-inherited-lock.json`、
`cpu83-observe-only-game.json`、`gpu84-pointer-and-cpu-joint.json`、
`race85-precheck-elision-rejected.json`。

[完整过程、反例和证据边界](METAL-RESOURCE-CHAIN.md)

## 纹理保留与部分 mip 更新（TEXTURE86）

`GpuModified` 是历史来源标记，不是“全部最新数据只在 GPU”的证明。实际
TextureCache 与 Metal 像素测试表明：建立 CPU mip 哈希后，GPU 改 mip1、CPU 改 mip0，
原路径只刷新 mip0，保留 mip1。若在这次刷新后清除 GpuModified，下一次 CPU 局部更新
会把旧 CPU mip1 重新上传，覆盖有效 GPU 内容。每个有效像素均已检查，Metal API
Validation 开启，合成测试退出 0；不涉及游戏文件。不能据此放宽失效纹理回收条件。

CPU83 三帧各 16 张对象的 flags 分布均为 74×8、65×2、73×5、75×1：全部有 Dirty，
14 张有 GpuModified，按实际 `SafeToDownload = GpuModified && !Dirty` 判定为 0 张。
这不代表可以直接丢弃，因为 Dirty 可能只来自某个 mip 的局部修改。LRU 年龄
1146–1752 的单位是提交 tick，不能解释成秒或帧。全部仍有部分活分配重叠。

同一次运行的四条预算日志中，调整后图形用量约 384–1128 MiB，动态预算约
8547–10726 MiB，明显低于 70% GC 触发线。它们解释了采样时保留缓存的合理原因，
不是这些对象泄漏的证据；预算日志与逐帧纹理日志并非每条同步，不能扩大为全程结论。
没有缩小缓存，也没有修改回收逻辑。

另发现一个已有边界：初始 Dirty 包含 GpuDirty，首次上传会跳过 mip 哈希建立。
没有后续建立哈希的刷新时，同样的局部 CPU 更新会重传未改 mip，合成测试已复现。
当前 engine HEAD 中也有该分支，非本次观察器新增。修复前需分清初次 CPU 上传与
GPU 修改过的缓冲区来源，不能直接给 GPU-dirty 路径增加任意 CPU 读取。
这尚未证明是游戏丢墙或闪退的原因，不作为性能收益。

证据：工作区 `texture86-lifetime-and-mip.json`、`validation86/`、`formal86-after.json`。


## 不能只按首次通知的范围上传（TEXTURE87）

实际 TextureCache/Metal 测试已复现：先建立两级 mip 的 CPU 哈希，再由 GPU 清除 mip1。
只向纹理缓存报告 mip0 的 CPU 写入后，`InvalidateMemory` 调用 `UntrackImage`，
整张图变为未监视；随后 CPU 可以再改 mip1，而不必产生第二次纹理失效通知。
真实 UpdateImage 的完整哈希检查发现两级均改变，GPU 读回分别精确等于 0x33/0x66。
测试使用合成映射，底层保护回调是空实现；`IsTracked` 状态与原缓存代码证明取消整图
监视这一边界，不冒充真实故障处理器测试。Metal API Validation 开启，退出 0。

因此拒绝仅依据第一次失效范围生成 mip 上传掩码；那会遗漏后续 mip1 写入。
不能用它替代完整哈希，也没有因此新增 CPU 读取或改变缓存预算。

首次上传的实际来源分支也已核对：LayerBind 的镜像或原地缓冲、游戏物理块、GPU 修改
过的 arena、预上传 arena、以及可能异步执行的 CPU staging。两级 mip 合成测试的
同步 CPU staging 情形不能代表这些来源；目前没有为修复初始哈希而给全部分支增加
任意 CPU 读取。既有的所有上传、同步及 GpuModified 标记均保留。

证据：工作区 `texture87-untracked-mip-range.json`、`validation87/`。

## CLEAN88：当前源码组合的两次死亡重生回归

重新以当前公开 probe/runtime 源码（包含 CPU79 权限修复）构建私有 QA 主进程，
搭配已验证的 0071/0073 renderer。没有接入私有 CPU 写入观察器或 SDK 产生链探针；
只保留开发用 SetHp 驱动，最多显式触发两次。包及实际执行文件均已签名、严格验证，
正式应用未替换。两次均观测到 HP=0、加载/世界卸载、恢复正 HP 和稳定可操作状态，
控制器与监督进程退出 0，日志没有捕获到断言或 Metal 命令错误。不是敌人攻击测试。

四份原生纹理清单（死亡前、第一次重生后、第二次重生后、闲置 15 秒后）容量为
638.880→708.762→708.777→708.777 MiB，描述符数 623→802→803→803。
第一次约多 69.88 MiB，第二次仅多 16 KiB，最后没有变化。初始 602 个 UID 仍存在，
其最终容量约 566.11 MiB；UID 保留并不保证同一 Metal 背板一直保留，也不是实际驻留量。
有限两次循环没有显示线性累积，不能替代长测或宣称不存在泄漏。

此次可见 57 个 Released-tag 对象约 46.43 MiB，其中 storage-only 历史角色为
51 个、约 30.94 MiB，GPU tick 均完成。它们是后续精确准入核对的候选，CSV 没有
完整的锁内重叠、年龄、记录丢失及背板绑定证明；不能直接当作已安全回收的内存。
0052 仍关闭，缓存预算与回收逻辑未改。

最高内存压力为警告级，批次累计交换峰值相对原始 9626.56 MiB 基线增加约
1493.75 MiB，未越过既有 2 GiB 上限；快速增长、严重压力与锁屏保护保持不变。
测试游戏已正常清理，退出后压力读回正常，正式 renderer、设置及 15 个存档哈希未变。
本轮没有新 FPS 结论；静止场景也没有覆盖运动速度图工作负载。

证据：工作区 `clean88-combined-death-regression.json`、`validation88/`、`formal88-after.json`。

## ADMISSION89：回收准入与物理别名的只读核对工具

已私有编译并签名准入专用 renderer，新增的 CSV 列仅在 BB_TEXTURE_AUDIT_DIR 诊断
请求时输出：标签覆盖、记录丢失/截断、两个分配域的活重叠、2000ms 锁内只读预检、
ReclaimReady、实际 storage 回收谓词、两秒闲置、收集器状态和背板数量。
生产源码、回收逻辑与缓存预算均未修改，0052 仍关闭。

真实 TextureCache 合成测试配合 Metal API Validation 通过三阶段检查：
GPU 未完成时 safe_storage=0；GPU 完成但有 32 字节 Range 活重叠时 unused_2s=0；
释放该重叠后，unused_2s/safe_storage/cold_2s/reclaim_ready 均为 1。
这些是预检值，实际删除必须再次执行 WithReleased 的锁内证明，不能把 CSV 当许可。

私有 QA 驱动通过实际运行时 sceKernelVirtualQuery 获取有界 VMA 清单，在自己的
游戏更新线程上调用，避免持有纹理锁再进入运行时映射锁。前后映射代次必须一致且为偶数，
最多 8192 个 VMA、4 次请求。真实运行时测试建立两个共享物理区间的不同游戏虚拟映射，
快照正确保留两份；查询途中修改保护、导致映射代次变化的负例被拒绝。沙箱内共享池分配
被系统拒绝，主机运行签名助手通过；没有以失败的沙箱结果声称验证成功。

离线分析器有 8 项通过的测试，覆盖实际准入状态、真实别名数据、代次拒绝、跨 VMA
连续覆盖和缺口等情况。它要求同一隔离 run 及相同序列，拒绝缺列、重复非零 UID 和
映射缺失；任何结果均标明“不授权未来释放”。记录中的额外游戏物理别名会拒绝候选；
这不是任意主机指针/未来新别名已排除的证明。

实际 ADMISSION89 游戏采集尚未开始。本轮先新增 1200 MiB 启动余量门槛，再尝试恢复
512 MiB 并启动时被自动审批拒绝：当前交换约 10664 MiB，上轮单次增长约 1192 MiB，
审批认为降低门槛存在卡死风险。被拒命令没有执行，门槛仍为 1200 MiB；原始批次基线
9626.56 MiB 和总增量 2 GiB、严重压力、快速交换增长、锁屏停止条件均未改变。
完成离线工作后的读回仍只有约 990 MiB 可用运行增长额度（含 20 MiB 余量保留），
未用新批次基线、短跑或间接方式绕过拒绝。实际 51 张候选的完整准入记录仍缺失，
没有将 CLEAN88 的 CSV 补猜成许可，也没有启用任何新回收。

正式 renderer、设置、15 个正常存档再次哈希核对未变。证据：工作区
`admission89-readonly-and-start-gate.json`、`validation89/`、`formal89-after.json`。

## GUARD90：未改启动线的实际尝试与重复预算限制

上一轮等待期间交换占用自然回落；新的启动读回为 9463.81 MiB、压力正常，
满足未修改的 1200 MiB 启动准入。此前下调启动线的例外不再需要，也未使用。
原签名 ADMISSION89 包实际启动一次，仍关闭0052；启动43.7秒时被监督器停止，
没有进入准入采集，未产生目标 CSV/VMA 快照。清理成功，无游戏进程残留，
正式 renderer、设置及15个正常存档哈希再次未变。不是游戏崩溃或兼容性结论。

实际触发项是临时脚本保留的1550 MiB单轮增长上限：起点9463.81、最后观测峰值
11183.19，单轮增加1719.38 MiB；相对原批次9626.56基线则仅增加1556.63 MiB，
尚未达到原先2 GiB批次上限。检查按周期采样，因此停止读数可能超过触发线；
不能把这个1719.38 MiB读数冒充2 GiB批次限制被触发。脚本叠加限制导致这次提前中止。

只读源码核对还确认：FreeImage先取消监视、注销并增加registry_generation，再延后销毁
视图和背板；DeleteImage会移除下载队列项。目标memo、图像描述符memo、TextureSet
及helper准入都检查注册代次。storage-only谓词排除绑定、target、rebind、general、
proxy、depth、MSAA和历史RT/depth/VO角色。这是源码证据，不证明任意外部原生指针
或未来虚拟别名都已受控；既有原生缓存测试另有注册失效及持有视图的覆盖。

已准备但未执行的预算修正提案：保留原批次基线和2 GiB总额，单轮上限改为剩余批次
额度（最高2 GiB），并预留256 MiB采样缓冲；保留1200 MiB启动准入、严重压力、
快速增长和锁屏保护。删除额外1550 MiB常量，不重置批次或增加总额。
7项离线测试及数百个余额点的边界检查通过。按实际启动点计算，提案上限为1954 MiB，
故不会在上述1550线提前停止，但不证明后续加载能在预算内完成，也不承诺零瞬时超调。
提案目前仅在仓库外的review-only文件中，不被现有启动器或测试脚本引用，没有自动重试。

证据：工作区 `guard90-overlapping-limits-and-source-audit.json`、`validation90/`、
`formal90-after.json`。51张storage候选的实际完整准入和别名证据仍缺，回收仍未启用。


### 最新进展（2026-10-22，92–94）

已收到继续测试授权，不再等待重复确认。实际 admission92 进入圣杯场景，未触发内存／锁屏保护，取得初始纹理快照；但隔离映射审计误将函数名交给 NID 解析接口，控制器主动结束自有会话，未执行死亡，不接受 FPS 或回收结论。
私有 admission93 已修正；使用实际 runtime.c 导入表与解析函数的夹具证明旧入口失败、新入口成功，并能检测真实别名和映射代数变化。随后预检只剩 797 MiB 可用增长预算，低于保留的 1,200 MiB 启动门槛，没有再启动游戏；原批次基线和 2 GiB 总增长上限未重置。

六项候选已在源码启动器中提供默认关闭的实验开关：原生清除合批、呈现／浮层合并、原生重复值填充、完整范围驻留记录复用、相同物理指针复制写入合并、通道内小参数写入。后三项以 0074–0076 重新接入当前源码，保留历史 M4 偏慢结果和正确性边界；并未宣称普遍无用。0071 清除后通道继续保留默认开启。现有 0038／0039／0040 与 0073 未撤销。

验证：73 个实际补丁重建／重复应用一致；25 组原生回归、32 项前端测试、54 项 benchmark 测试及 3 项补丁工具测试通过。原生清除开／关／父项关闭的 3,584 像素字节一致；驻留差分 30,000 操作／59,952 调用一致。三种语言、依赖、旧开发选择迁移和后端隔离均已测试。UI 隔离包已签名生成，但工具无法解析其应用注册，未声称完成截图验收。

没有正确性证明的回收 0052 仍不进入用户开关；已有写错数据反例的删写／缩小寻址范围方案继续禁用。正式应用、15 个正常存档及设置与 formal90 哈希一致；没有清理正式缓存，也未发布 Release。证据：工作区 `experiments94-restored-and-admission.json` 与 `formal94-after.json`。

末次只读预检余量自然恢复至 1,181 MiB，仍未达到原门槛；没有启动第二份游戏，也没有重复请求授权。


### 95：物理别名边界得到定向证据

可独立复现的真实运行时／Tracker 测试证明：不同虚拟地址 A、B 可以共享物理页；A 分配释放且 B 仍活跃时，现有虚拟区间证明在两秒宽限后仍会放行 A。只执行了测试元数据回调，没有创建或释放纹理，也不能据此声称游戏中存在同类别名。0052 继续关闭；启用前必须补齐物理别名与映射变化在实际回收事务中的保护，死亡测试通过本身不够。

admission92 的初始真实快照有 623 张图像、137 张通过存储状态部分、26 个旧标记已复用，但没有任何 Released 标记或 unused_2s 区间。该快照中没有可回收候选。旧进程的死亡后 51 张图像不能跨进程嫁接到这次映射上。

补充源码核对发现，FreeImage 会经页跟踪回调运行时的写监视／保护：当前实现分别使用原子位图和嵌套读锁，没有证据表明这条链会发生读转写锁升级。新的映射事务仍须先验证重入、锁顺序与非阻塞准入，再决定集成。保护余量仍为 1,197 MiB，预检未启动游戏，未改门槛或原预算基线。小组件仅测得 RSS；完整 footprint 采样无输出，已停止自有采样器，不能将 RSS 当成其全部内存占用，也未重启小组件。


### 96–98：映射事务保护、加载中止与视觉采集条件

新增 0077：仅为默认关闭的 storage 回收扩展增加运行时映射事务。完整 DIRECT 覆盖、当前物理别名排除、非阻塞写锁和嵌套保护查询验证后才进入元数据回调；旧运行时缺失接口时拒绝回收。17 项实际运行时检查、75 项保护回归、真实 runtime＋Tracker 的活跃别名拒绝测试，以及五组 Metal 缓存测试通过。缓存测试中的映射许可由模型提供，不能冒充完整运行时与 Metal 联合证明。74 个补丁重建一致。历史已解除映射但逻辑所有者仍活跃的别名、隐藏原生／GPU 别名尚未覆盖，0052 仍关闭。

ADMISSION97 预检自然满足原 1,200 MiB 门槛，未降低保护或重置批次基线。实际运行约 38.49 秒后因连续快速交换增长停止：10,173.0→11,064.62 MiB，增加 891.62 MiB，压力最高警告级。尚未到可采样场景，没有死亡、映射快照或 FPS 结论。增长对应场景加载／资源上传阶段，不能据此判断泄漏或归因到未执行的回收事务。自有进程清理完成，未紧接着重复启动。

用户指出 Computer Use／录屏可能干扰性能，现将视觉采集纳入对照条件。评分期间不做截图、录屏、远控视觉观察或 GPU 抓帧，只保留同模式的轻量帧数据与低频内存／锁屏读回；需要视觉正确性检查时在评分窗口外执行，并重新等待稳定。游戏窗口保持正常显示，不通过最小化改变工作负载。`benchmark.py run` 和 `benchmark_candidates.py` 支持 `--visual-observation absent`，这是操作者对整个评分窗口的声明，不能自动检测所有外部录屏。已知 present 拒绝评分／比较，不同状态拒绝比较；历史缺字段默认 unverified，比较时明确提示未核实，不能回填成无采集证明。需要比较远控／录屏影响时应独立安排相同场景的控制实验，不能预先认定此前差异由采集造成。73 项 benchmark 相关测试通过。

先前录屏状态为 disabled；本次末次检查 Cua 后台服务未运行，没有为检查启动服务，也没有截图。此读回不证明其他工具没有录屏。正式 renderer、设置和 15 个正常存档与 formal94 哈希一致，未清理正式缓存、未安装或发布新版本。证据：工作区 `mapping96-admission97-visual98.json`、`formal97-after.json`。


### 99：历史已解除映射的物理别名也必须拒绝

实际运行时定向反例：物理页先映射 B，解除 B 后再映射 C，从未同时存在 B/C，旧 current-VMA 事务仍会放行 C。真实 runtime＋Tracker 也证明，解除虚拟映射不会把逻辑所有者自动标为释放。只做了测试回调，没有在反例中释放纹理；这不证明真实游戏已出现同类错误。

为默认关闭的 storage 扩展增加从第一次 DIRECT 映射开始的有界物理页见证：每个 16 KiB 物理页保存首次客体虚拟页号；若曾映射到不同虚拟页则永久保守拒绝。解除映射、释放／重新分配物理页均不抹除历史。相同虚拟地址重映射、保护变化／VMA 拆分和不相关物理页不被连带拒绝。关闭时不分配表；晚启用、分配失败和缺失完整覆盖均拒绝。表载荷在 5056 MiB 池为 1,294,336 字节，在 16 GiB 上限为 4,194,304 字节；这不是进程 footprint，未含低堆分配粒度。当前 storage 仍关闭，因此未给正式离线游玩增加这张表。

接口升级为 `runtime_memory_reclaim_if_unaliased_v2`；0077 使用弱 v2 导入。只有旧 v1 的运行时也不能放行，定向链接测试确认旧 v1 回调未被调用。未新增或重排现有缓存容量／GC 时限、页表、速度图或遮挡剔除逻辑，35 次 192 字节写入及同步仍保留。

验证：5 种实际映射历史配置（5056／16384 MiB 池、关闭、晚启用、分配失败）、17 项映射／锁检查、真实 runtime＋Tracker 的同时别名与历史已解除映射所有者拒绝、75 项保护回归、5 组实际 Metal 缓存测试通过；74 个补丁重建一致，73 项 benchmark 回归通过。Metal 缓存组仍由映射模型给许可，不能冒充实际运行时映射与 Metal 同一条链已验证。原生／GPU 隐藏指针的所有权、真实游戏准入、死亡重生和收益仍未完成，所以不启用新回收。本轮未启动游戏、未使用截图或录屏；正式应用、设置、15 个正常存档哈希未变，也未清缓存。证据：工作区 `history99-historical-physical-alias.json`、`formal99-after.json`。


### 100：实际运行时与 Metal 缓存已在同一进程联合验证

新增可复现工具 `tools/macos/test_runtime_metal_retirement.py`，使用现有 Metal 测试构建和两份小型源码桥接。新动态库包含实际 runtime_memory.c／low_heap.c；缓存测试把映射准入、物理背板读取、映射代数和页保护转交该库。相关方法不再由许可模型代替，其余不相关运行时调用仍为失败即停的测试桩，Rasterizer 的 IsMapped 也仍是最小接口。没有运行游戏程序、访问游戏文件或截图。

Apple M4、x86_64 测试进程、Metal API Validation 下五组通过：候选关闭时不回收且历史表为零；唯一物理映射时实际进入 v2 事务，调用正常缓存注销／延后销毁路径，最后一个视图释放后 weak backing 变空，同地址新纹理像素正确；活跃物理别名、已解除映射但逻辑所有者仍活跃的历史别名、缺失起始历史均拒绝回调，原 GPU 红色像素逐字节保持。实际 FreeImage 链触发一次映射写锁内的嵌套页保护调用，无死锁。

另对唯一映射用例测得，最后一个视图释放时 Metal currentAllocatedSize 从 160,940,032 降至 160,923,648 字节，下降 16 KiB；缓存报告的资源 allocatedSize 容量为 48 KiB。两者口径不同，不能相加，也不能把容量直接当成物理内存回收量；这仍不证明驱动已向 OS 归还等量驻留页。

复现（先从当前源码构建原有测试目标，禁止与游戏并行）：

```sh
cmake --build /private/tmp/bbport-metal-build --target texture-cache-views-test --parallel 1
python3 tools/macos/test_runtime_metal_retirement.py \
  --build /private/tmp/bbport-metal-build \
  --output /private/tmp/blood-echoes-runtime-metal-new
```

输出目录必须为新目录，工具签名并验证自建 helper／可执行文件，将配置与日志限制在该目录；它不属于正式包的启动路径。`--mode released-storage` 可只跑唯一回收用例，报告明确列出实际运行的用例，不能冒充全套。

这些是 64×64 合成纹理的资源生命周期正确性证据，不覆盖任意原生／GPU 指针、全部格式／mip 或真实场景。实际游戏的候选准入、死亡和收益仍未验证，0052 继续关闭，35 次写入保留。正式应用、设置和 15 个正常存档哈希未变，未清缓存、未安装／发布新版本。证据：`joint100-runtime-metal-retirement.json`、`validation100/`、`formal100-after.json`。


### 102：用户关闭前台应用后，授权放宽 swap 容量预算

只读读回：swap 从 10,848.62 降到 9,807.62 MiB（下降 1,041 MiB），压力正常级 1；随后 10.02 秒 swap 未增长，换出 0、换入约 0.019 MiB/s，压缩器实际占用约 1.26 GiB。磁盘约 393 GiB 可用。这个短窗口显示当前没有持续换页，不保证加载期间仍如此；memory_pressure 的百分比不能直接换算成空闲 RAM。

用户明确要求放宽 10 GiB 以上。测试默认新增增长限额改为 12 GiB，另设系统 swap 总量 24 GiB 上限；不是修改 OS 的 swap 设置。原批次起点仍为 9,626.56 MiB，未重置历史，当前剩余测试预算 11,850 MiB（保留 256 MiB 缓冲）。严重压力、连续快速增长（至少 6 秒／512 MiB／32 MiB/s）、锁屏／睡眠和游戏 footprint 9 GiB 停止条件保留；启动余量门槛仍 1,200 MiB。警告级本身不停止。此前 ADMISSION97 因快速增长停止，不能说提高容量就能保证通过。

benchmark policy v5 增加 `--max-swap-total-mib` 和 `--swap-budget-baseline-mib`。自动候选 A/B 四轮共用同一原始基线，不能每轮重新领 12 GiB；新旧保护策略或总量／共享基线不同的结果拒绝直接比较。CLI 确认新额度可实际传入，79 项 benchmark 回归通过。高开销 gpucapture 自身原有的独立 1 GiB 诊断增长限制未放宽，本轮没有启动 GPU 抓帧。

admission101 已准备私有强制起始历史记录、只读非阻塞 snapshot API 和 metadata 字段；probe 不修改 VMA、映射代数或资源。解析器新增拒绝历史缺失／锁竞争等状态，且纠正原先把“采样＋storage”也排除的过窄角色过滤，使其符合实际状态谓词；10 项解析测试通过。原生 probe 夹具及完整包仍待构建，不能声称游戏采集成功。后续控制器已移除显示快照，远控状态未核实，所以这轮诊断不会当作 FPS。

本轮未启动游戏，正式应用、设置和 15 个正常存档哈希未变，未清缓存。证据：`guard102-expanded-capacity-and-memory.json`、`formal102-after.json`；私有新 runner 是 guard102/run_death_authorized103.py，尚未执行。旧97等脚本作为历史证据保留，不再用其2 GiB配置进行下一轮。


### 101／103／104：只读采集链验证完成，真实加载仍被短期增速保护中止

私有 snapshot 的 14 项实际运行时检查通过：所有调用均不改变 table_generation，不调用释放；当前／历史别名、缺失历史、镜像进程或锁竞争不会给出许可。另用实际 runtime＋Metal＋CSV＋解析器验证同一 UID 的三个阶段：GPU 未完成、另域活跃区间重叠、已满足快照条件，结果依次 false／false／true。构造的 VMA 输入是该夹具的已知映射布局，不是游戏映射清单；全部结果保留 authorizes_future_free=false。collectors 仍关闭，不进行显示读取。

Admission101 完成编译、重定位及深度签名验证；实际嵌套游戏执行文件导出 bb_private_mapping_probe_v2 与 v2 回收接口，渲染库导入前者。执行文件 SHA6df22c68…，renderer056f4e17…，详细哈希在 package101.json。私有内存源码仅强制从首个 DIRECT 映射记录历史并增加只读快照方法，公共源码未为审计新增开关。

用户已授权的新容量预算用于实际 GAME103：起点 swap9,775.62 MiB、压力1、未锁屏；44.57秒后清理，自有监督器exit1、控制器exit0。末次／峰值 swap10,393.38 MiB，新增617.76 MiB，压力最高2。37.38到43.72秒的两段连续增长约94MiB/s，仍触发旧6秒快速增长判据；未达到12GiB新增、24GiB总量、严重压力或9GiB游戏footprint上限。尚未取得CSV或死亡，无FPS结论。所有显示快照调用已从控制器移除，远控状态未核实，诊断本来就不计分。

离线REVIEW104准备一轮例外提案：连续≥32MiB/s达≥30秒且增长≥1GiB才触发常规增速停止；另保留连续≥128MiB/s、≥6秒且≥1GiB增长的紧急停止；严重压力、容量、锁屏、footprint和时间保护保留。10项合成／实测前缀重放检查通过，已记录的103前44秒不会触发提案；其后的真实行为未知，不能据此证明能安全加载。提案没有接入任何运行脚本；此前30秒授权仅限当次，本次仅容量被明确放宽，所以已请求下一轮例外确认，不自动沿用。

正式renderer、设置及15个正常存档仍与formal102哈希一致，缓存未清、应用未替换。证据：`admission101-game103-review104.json`、`validation104/`、`formal104-after.json`。完整目标尚未完成，0052仍关闭，35次192字节写入及其同步继续保留。

### 105：收到当日17:00前测试授权，接入持续增速规则

用户明确授权至2026-10-22 17:00（UTC+8）。已接入REVIEW104的30秒持续增长规则，保留6秒紧急增长、严重压力、12GiB批次新增/24GiB系统总量、9GiB footprint、锁屏/睡眠及监测中断保护。普通模式仍默认旧6秒；本轮显式选择30秒，A/B必须匹配模式并共用原始swap基线。新增带时区的硬截止，启动前及每次guard调用检查，超时按既有finally清理自有游戏。83项benchmark回归通过。

GAME105使用已签名Admission101、同一隔离圣杯存档，采集资源/VMA元数据并最多两次SetHp死亡；不作帧率评分，不请求显示读取，不启用0052。启动swap10,059MiB，压力1。采集结果待读回，不能提前宣称完成。证据：guard105-integration.json。

### GAME105/106：真实双重生与映射记录已取得

同一签名包、隔离圣杯、无显示快照。0052关闭和开启各完成两次SetHp=0→加载/世界切换→正HP稳定重生，监督器与控制器均exit0、清理成功。105关闭时四次快照701/877/888/888个有native容量图像；重生后65/64/58个满足只读条件，约38.5/38.1/33.9MiB。所有当前图像mapping_v2_snapshot=1，但快照本身仍不授权未来释放。

106开启实际受锁事务回收，日志54个图像、66,428,800B容量被回收；最终仍满足条件但未回收为0。最终Metal allocated约5.85GiB，比105约少74.2MiB；进程footprint约5.47GiB基本持平，不能把容量差当成等量物理内存归还。106系统swap未增长，压力最高2；105新增约774MiB，压力最高2。两个先后短轮不能独立归因系统swap变化，也没有像素/帧率结论。已有小纹理实际GPU像素负例/正确回收测试不替代全游戏验证。

正式renderer/设置/15个正常存档在105后哈希仍未变。107正在进行关闭0052的三次梦境/亚楠中心往返，各次要求loading+world teardown；随后同配置开启对照。尚未改正式默认值。证据guard105-integration.json、admission106-storage-on-death.json、validation105/106。

107长测在第5次VMA请求超时后由控制器主动finish：私有原生采集器硬上限4次，脚本误安排8个检查点都采完整VMA。没有游戏崩溃或内存保护触发，已有4次实际地图卸载/加载不能当完整三往返。108已改为8个检查点中仅初始、首次梦境、首次亚楠中心、最终亚楠中心采4次完整记录，其余记录原生状态和总量；不增加上限，不需重新编译。对应测试保护回归增加套件向子进程传递同一模式/截止/原始基线的检查，共84项通过。

### TRAVEL108/109：关闭/开启两组长往返均完整通过

同一签名包、同一隔离种子、相同画质/缓存和v6显式30s保护。每组约356秒、3次梦境/亚楠中心往返，合计12次原生传送均有loading+world teardown+稳定目的地证明。四次完整VMA/纹理快照按限额取得，其他检查点记录总量；两组均正常退出、自有进程清理成功。

开启组累计回收168个图像、258,079,872B（246.1MiB）native容量；关闭组最终93个/55,414,016B满足只读条件仍保留，开启组为0。最终Metal allocated约少70.8MiB，owned约少70.8MiB，footprint约少54.4MiB；均非容量计数与物理内存的一一对应。关闭组亚楠中心1/2/3轮Metal allocated约6277.7/6341.1/6327.5MiB，开启组约6203.8/6239.9/6247.6MiB；当前六分钟测试不呈线性失控增长，不能推广成无限时长无泄漏。

这两轮是元数据诊断，没有实际游戏像素一致性或FPS证明。既有合成纹理逐字节验证与实际受控死亡是独立证据。110..113已开始额外的预先固定ABBA站桩窗口：每轮相同隔离传送到亚楠中心、充分闲置后45秒，窗口内不采元数据、不显示读取、不启GPU分项计时。外部视觉观察状态仍unverified，不追认capture-free。正式默认值与应用仍未修改。证据travel108-109-retirement-comparison.json、validation108/109。

### 110/111测量边界修正；114..117关闭整进程内容审计

110固定窗口46.407FPS、零编译、镜头稳定；但源码复核确认MemoryStats::ContentEnabled会因BB_TEXTURE_AUDIT_DIR从启动起持续开启逐绘制内容计数和ledger锁，不能仅以窗口内没有快照请求声称没有审计开销。110保留为诊断数字，不用于无审计性能收益结论；111中途主动对准确自有suite发SIGTERM，信号逐层清理自有游戏，没有杀其他应用。

114..117改成整进程不传--texture-audit，冻结环境也没有任何AUDIT/CAPTURE变量，因此NativeMode=false、ContentEnabled=false。同一私有包仍强制首映射历史，开关两组相同。控制器不请求纹理/VMA快照，只做原生传送、idle和固定45秒窗口；每轮计分结束后单独进行一帧native显示读回，且该阶段不纳入计分。外部远控/录屏状态仍unverified，不追认capture-free。守护和原始9626.56MiB批次基线未重置。

### 114..117：无纹理审计的0052 ABBA完成

四轮均成功退出、相同二进制/INI/cohort、私有地图序列、固定45秒，窗口零编译、镜头稳定；无整进程纹理内容审计或GPU分项计时。一帧native显示读回均在各自窗口之后，不用于FPS样本。外部观察状态仍unverified。OFF两轮46.025/45.487，ON46.354/46.270，均值45.756→46.312（+1.214%）。OFF自身波动0.538FPS，与平均差0.556FPS接近，不能确认提帧因果；1%low也不做稳定改善结论。

窗口平均Metal allocated只少约8.7MiB，而footprint约少321MiB，二者口径/系统压缩不同，不能把后者归因于回收。分配器和native资源计数、长往返实际释放和同场景约70.8MiB分配差更直接。114/115画面抽查墙面/地面/材质无新缺失，静态墙区域平均通道差小于0.4/255；地面受动画/粒子影响更大。这是有限场景观察，不是逐像素全游戏正确性。正式游戏INI字节与冻结INI一致，5项显式性能选择（归档/同批次快照/宽松数学/片元特化/小参数写入）也一致。

0052继续开发验证且默认OFF。随后将恢复的6项实验统一开/关：native clear batch、overlay merge、native fill、conservative residency memo、identical copy pair、in-pass parameter updates，保持0052关闭。117作为首个OFF、118/119全ON、120最后OFF；初始方案文件及3个子runner出生时间早于117窗口约20秒，后续补环境元数据的更新时间在该窗口中，已单独记录，不把更新时间冒充预登记时间。118/119约45.71/45.36FPS，120运行中。没有删去任何实验开关，也未把已证实错误的算法恢复成选项。

证据storage114-117-clean-abba.json、validation114..117、bundle118-baseline-preselection.json。

### 117..120：六项恢复实验的无审计组合ABBA完成

OFF45.487/45.996FPS，ON45.713/45.357FPS，均值45.742→45.535（-0.452%），1%low约30.60→30.72。差异在本轮波动范围内，没有确定整体收益；组合结果也不能判定每项独立作用。全部六个实验开关继续保留、默认关闭，未把已证实错误的算法重新暴露。开启组确认copy paired-store merge日志命中；没有render-update Info日志不等于证明该路径零命中，未报告其使用比例。

窗口Metal allocated约少24.3MiB，footprint约少98.8MiB，不作逐项归因，也不声称负收益适用于其它硬件。各轮原生加载、固定镜头/零编译、守护与清理均通过，所有显示读回位于窗口之后。118/120单独画面抽查主要墙地/人物/HUD没有新增缺失，仍不是全游戏逐像素证明。

至此本批无游戏残留进程，结束内存压力1、swap11,092.06MiB。正式renderer、偏好和15个正常存档与formal105哈希相同，正式游戏INI仍与冻结INI字节一致；未清正式缓存或替换应用。下一步只读核对RangeFreeHook：未配对释放可能来自找不到Tag、当前节点边界与原Tag不符（如合并）、或其它分配路径。先用有界私有观察区分原因，不能以unknown直接授权释放。证据restored117-120-clean-bundle.json、validation118..120、formal120-after.json。

### 121–124：Range 缺失统计来自另一条分配入口

123 私有只读观察补上 0x26a9db0 的返回点。实际 1.09 指令签名与原生 ABI 测试通过；原分配/释放参数、Tracker 和 GPU fresh-range 回调均未改变。124 两次 SetHp 死亡重生通过（不是敌人战斗或帧率测量），最后稳定快照为：旧入口 44,084 次分配、44,079 次配对释放；缺少 Tag 的释放 564,139 次；新入口观测到 564,320 次分配；无长度不符或精确 Tag 拒绝。

这些事件均位于同一约 160 MiB DIRECT 范围。四次纹理快照没有纹理区间与之重叠，因此该入口解释了 unknown-free 的规模，不能直接解释大批纹理的保留，也没有因此接入 Range 回收或 GPU 失效回调。新观察器有界状态约 295 KiB、报告临时区约 320 KiB；仅在私有 QA 中使用。正式 renderer、偏好及 15 个正常存档与 formal120 完全相同。证据：range123-124-observation.json、validation123-124、formal124-after.json。

### 125：同容量释放历史优先级候选（尚未游戏验收）

124 的末两次纹理快照中，已存在 Heap Tag 但状态变 Unknown 的对象由 10 个增至 16 个；该段 Heap 分配/释放数均未增长，Range 仍持续复用。原始 Tracker 定向测试确认：共用的 32,768 条历史会被高频 Range 复用挤占，失去 Heap 释放证据。不能把所有 Unknown 都当成已释放资源。

开发源码新增 BB_LIFETIME_HEAP_HISTORY_PRIORITY=1 候选，默认关闭：历史总条数不变，优先保留可用于纹理回收的 Heap 证据；只剩 Heap 记录时，新 Range 历史不再挤掉它。关闭时仍使用原释放插入顺序 FIFO。释放、重用、活跃重叠、映射事务、GPU 完成、冷却和 backing 角色条件均保留。额外固定空队列约 KiB 量级，未增加 GPU 缓存容量。待完成原生回归及隔离场景对照后才决定启用。

### 126–129：联合回归与两组12次切换通过，暂无省内存结论

126 实际运行时/TextureCache/Metal 五例全部通过：可回收 backing 被销毁，地址复用像素正确；当前别名、历史别名和缺少历史仍拒绝删除。普通生命周期、实际 hook ABI 和 85 项 benchmark 测试通过，75 个有序补丁（编号至0078）可从基线精确重建。

127b 关闭、128 开启同容量 Heap 历史优先策略；同一签名私有程序、相同冻结存档/INI/固定环境，0052均开启。各完成12次有 loading 与 world-down 证明的梦境/亚楠中心切换，约584/578秒，最高压力均为警告级；所有自有进程正常清理。最终已标记 Heap 但历史 Unknown 的对象均为7个；开启后 footprint 约少4.25MiB，Metal allocated约多15.19MiB，累计销毁271/269张、容量约399.08/385.25MiB。累计容量不是实际RAM节省；这对长测没有证明新策略带来净内存收益，也不能把差异归为单一因素。

129 开启策略后的两次 SetHp 死亡重生同样通过（不是敌人战斗覆盖）。正式renderer、偏好、15个正常存档与formal124相同，未清缓存或替换应用。新策略在源码前端补成三语实验项，默认OFF；现为33项、21默认ON/12默认OFF，Swift回归须等游戏退出后再编译。

130–133 已开始无纹理审计、无阶段GPU计时的同版本ABBA；固定45秒窗口之后才读回单帧。当前尚无最终帧率结论，外部录屏状态仍为unverified，未调用CUA/桌面录屏。所有子运行保留严重压力、持续/紧急交换增长、原批次容量、9GiB footprint、锁屏/睡眠和当日17:00硬截止。

### 131／135–145：发现并修复静态顶点预取的断言缺口

原四轮短对照在131切换梦境时退出23，因 DrawPrep4 在静态顶点格式键构造中调用严格 SurfaceFormat，遇到 data6/num5；不是交换/压力保护。该组ABBA不成立，130单轮不用于确认收益。相同原始分支在关闭 Heap 历史优先时无GPU定向复现同一退出23，证明旧分支存在这个断言缺口，但不能仅凭此证明真实异常描述符由谁改写。

0079只让推测线程在未知静态顶点格式时拒绝准备结果，交回正常有序路径重读；真实有序路径仍严格检查，不用默认格式，也不省略绘制。静态格式键与后续顶点预取共用有作用域的原始描述符读取恢复，保留外层恢复点。128种V#硬件字段组合中98种结果完全不变、30种推测拒绝；typed覆盖、故障/成功恢复、信号解阻塞、动态顶点键不变、原纹理/缓冲特化及未污染缓存检查通过。真正不支持的有序6/5仍退出23。76个补丁可从基线精确重建。前端33控制的13项单测在不带扩展属性的私有源码副本通过；未开正式启动器。

新136隔离包的137两次受控重生通过。138真实记录到顶点格式15/7被拒绝后流程继续，随后12次地图切换分别完成，但收尾等待在约563秒触发严重压力保护，故整轮长测记中止，不能记完整通过；该拒绝尚未与某一GPU绘制唯一关联，也未证明异常字节的原始写入者。游戏退出后压力恢复正常；另按既有授权重启占605MiB的小组件chronod。139–142是新的同136二进制短ABBA，仍保留全部守护及17:00截止，不把138中止结果洗成通过。

上游master在2026-10-19提交8185b88d中，重命名后的gpu/render/pipeline_cache.cpp静态顶点分支仍严格SurfaceFormat，而邻近vk_draw_prep.cpp已有TrySurfaceFormat回退。这与本地缺口一致；不代表已审阅全部分支/PR。当前Metal明确不使用动态顶点输入，因此会进入静态格式键分支。已保存三个完整源文件并核对Git blob SHA。

证据：vertex135-crash-root.json、vertex138-partial-and-critical-stop.json、validation135、upstream144源快照。正式应用未替换，正式存档/设置及游戏缓存未作写入或清理。

### 139–147：短对照完成；省去创建管线时无用的描述符重读

139/140/141/142 是同一136包、无整进程纹理审计的完整ABBA，Heap历史优先OFF两轮46.087/46.228FPS，ON46.303/45.676FPS；均值46.158→45.990（-0.364%）。没有确认性能或净内存收益，实验项继续默认关闭；三语说明更新为本轮结论。两次地图切换、稳定镜头和零编译固定窗口均通过，自有进程清理及冻结存档校验通过。显示读回在窗口后，139/141在全部测试结束后抽查，不能视为全游戏像素证明；外部录屏状态仍unverified。

0079之后复核Metal新建管线，发现GetVertexInputs读取V#地址、格式和步幅，而调用者仅使用来自fetch shader和runtime step rates的位置/实例频率/步进值。0080从这些已有元数据直接构造完全相同的Metal列表，预加载序列化、实际绘制packet_vsharps、格式键和严格检查不变，不新增GPU资源或缓存。重复语义沿用原Metal循环的最后匹配实例divisor；早期私有试验误按首次匹配，已在接入前改正测试oracle并重新测试，不用旧结果充当现路径证明。

5378组与实际旧GetVertexInputs转换逐项一致，包含全部256种步进字段、重复语义、0/最大divisor及32属性。旧元数据构建访问PROT_NONE退出SIGBUS，新接口无Info/描述符指针且通过；推测描述符及生命周期原生回归通过。77个补丁从引擎基线精确重建。游戏隔离验证尚待执行，不预先宣称提帧或新场景收益；正式应用、存档、设置和缓存未更新。

### 148–153：新建管线覆盖与稳态回归分开验证

148–151旧136/新147包完整ABBA：45.701→45.910FPS（+0.457%），旧路径自身跨度0.915FPS，无确定提帧结论。四个进程的帧统计编译数均为0，说明这是暖缓存回归，不能据此证明0080新建管线分支实际执行或改善进入新场景卡顿。窗口平均native分配约少19.4MiB，进程footprint约多101.4MiB，口径/宿主状态不同且0080并非逐帧路径，不将这两个差异归因给修改。所有固定窗口零编译、稳定镜头及自有清理检查通过。

152另建完整隔离存档/配置/缓存副本，复制时省略1861个历史图形key（原缓存无删除），保留已有着色器/原生库。运行新147包生成160个当前格式图形key、帧统计378次编译，覆盖非预加载图形管线构造；完成两次SetHp=0的HP归零→卸载/加载→正HP稳定重生。最高压力2，swap起点及峰值均6973.94MiB，监督器/控制器退出0。重生后单帧读回在游戏退出后抽查，地面/墙体/角色/HUD及中文可见；不是敌人战斗或逐像素全游戏证明。

0081候选进一步跳过静态顶点后端永远不消费的PreparedVertexInputs：Metal能力与运行时均非动态输入，原消费者已经拒绝其结果。仅省去第二次原始V#读取、属性/范围排序、哈希和临时arena复制；静态格式键、stage/resource准备、packet_vsharps和有序缓冲解析保留。Vulkan动态输入能力来自同一个Instance查询，与消费者一致，仍执行原准备。它与0080同属删除无用内部计算，没有画质/容量策略变化或新增设置。当前源码接入、78个有序补丁精确重建；隔离原生/游戏对照正在验证，尚不宣称收益或正式部署。

证据：metadata148152-validation.json、validation147、私有vertex153。正式renderer/偏好/15个存档在formal147-after.json与formal129保持一致，缓存未清理。

### 153–164：无效顶点准备的实际命中、长往返与重生已验证

153四组原生回归通过：5378组Metal元数据差分、推测格式/恢复、资源生命周期、实际Metal管线能力/序列化/归档保留。归档测试内的故意失败与无效文件警告属于夹具，最终all checks passed；不代表正式缓存写入失败。78个有序补丁（编号到0081）精确重建。

154–157无内容审计短ABBA，旧147/新153平均45.801→46.445FPS（+1.405%），旧组跨度0.106、新组0.011FPS；这是窄场景积极信号。两边0052均为开发验证开启，不能将结果直接冒充正式默认配置。窗口renderer footprint均值差约+18.5MiB、native allocated差约-27.6MiB，口径与系统变化不作单一因果解释。

161/162两个私有诊断库只在四个现有纹理审计请求点输出累计计数，公共源码没有添加这些计数器。158/159分别用旧/新工作线程代码完成各12次有加载与world-down证明的传送，约584.47/580.88秒，无FPS评分。旧路径最后快照13,461,379份动态顶点数组，复制4,800,566,693B（约4.47GiB）；新路径15,117,583次准备成功、0份顶点数组、0对应复制字节。这是累计CPU复制流量，不是活跃分配或物理RAM节省；Metal消费端原本因静态输入能力拒绝这些数组，有序顶点快照/格式键/其他资源准备保留。

两轮最高压力2，旧swap6858→峰值7781.38MiB，新7598.62→峰值7688.88MiB；起点不同，不能归因交换改善。最后30秒renderer footprint新比旧约+45.7MiB、native allocated约+21.4MiB，未确认总内存下降。不同场景阶段数据保留在比较JSON，不能将起点圣杯与终点亚楠中心相减当泄漏。160新路径两次SetHp死亡重生完整通过，非敌人战斗或联网覆盖。自有进程/冻结原始存档清理校验通过。

164–167第二组反向BAAB已预先固定0052关闭（正式默认），保持其他配置/INI不变，用无计数器147/153包独立复测，不与第一组混算。脚本沿用Starting storage字样是旧日志标签，数值实际为本次顶点准备候选；真正0052以control-result中的BB_TEXTURE_RELEASED_STORAGE=0为准。所有守护及2026-10-22 17:00硬截止保留。

正式应用未替换、未提交/发布。根目标的完整原始CPU/GPU别名与读取覆盖仍未建立；35次192B写入及同步保留。0080/81不绕过真实格式检查、不引入GPU资源/缓存容量策略，也未把已证实不安全的写入省略做成选项。

### 164–168：正式默认状态复测完成，结束本轮测试

反向BAAB164–167，两边0052关闭、Heap历史优先关闭，其他配置和冻结INI相同，无逐绘制审计/CPU复制计数/GPU阶段计时或计分窗口显示读回。旧路径46.101/45.812，新46.156/46.657，平均45.956→46.406FPS（+0.980%）；旧组跨度0.289、新组0.501FPS。与0052开启的第一组分别记录，不混合样本。两组方向均正，但只支持当前固定镜头的小幅改善，不能保证全游戏收益，60FPS目标未达到。第二组native allocated均值差约+0.35MiB、renderer footprint约-9.18MiB；配合第一组和长测的反向差异，未确认物理RAM净节省，也未增加缓存容量。

164–167四幅1920×1080显示读回均在计分窗口后，在全部游戏退出后统一抽查；同一视角墙面/地面/角色/HUD无明显新增缺失，不是全游戏逐像素等价证明。5378组元数据差分则精确验证位置/实例频率/divisor与旧转换一致。速度图使用的有序顶点缓冲/哈希路径、遮挡查询和两级页表未改，不能把当前MetalFX关闭的场景说成这些功能的完整活跃覆盖。

168将167的测试进度另冻成新隔离种子，正常继续就进入相同镜头，无WorldWarper/控制会话；仅启用现有有界GPU阶段时间戳采样。45秒诊断完成、原始源存档及偏好校验通过。末8个完整报告约40.07秒估算guest-render17.88ms/帧、compute2.95、blit1.03、host-render0.91、occlusion0.13。各跨度包含依赖/tile工作、可以重叠，不可相加当临界路径，也不能直接命名为纯顶点ALU/片元/纹理带宽耗时。该诊断的45.55FPS不与无诊断对照混算。剩余热点仍在主绘制和通道依赖，下一步需具体资源/硬件访问证据。

所有自有游戏/渲染进程已结束，退出后压力1、swap7639.88MiB。formal168-after.json核对renderer、偏好、15个正常存档及游戏INI与formal147一致；未清理/替换正式缓存，未安装应用、提交/推送或发布Release。测试授权截止2026-10-22 17:00，本轮不再启动游戏。已证明CPU无效工作减少，完整CPU/GPU别名/读取覆盖及原始35次192B写入的可省略性仍未成立，不将整个根目标标记完成。

最终证据：validation153/clean164-167-comparison.json、long158159-comparison.json、static-long-suite158.json、profile168-result.json、stage-summary.json；状态见root-followup-status.json。


## 2026-10-22: depth/stencil-only pass continuation (0.2.49)

0082 separates capture boundary drains from requested-state observations. 0083 adds a default-OFF
experiment for identical attachments across read-only-to-write plane promotions only. Previous
and current texture feedback, unknown indirect access, barriers/fences/hoists/tails and pending
clears keep their original fallback. Write-to-read-only always retains its boundary; reduced-density
visibility queries do too because per-pass scaled counts round independently.

355 CPU policy checks passed; actual M4 Metal API Validation passed exact pixel/query tests in
held/direct encoding, candidate OFF/ON, unknown access, feedback later unbound, parent views,
barriers, tails, snapshots and query-policy fallbacks. Existing consumed-clear tests pass OFF/ON.
One diagnostic frame confirmed 3 actual continuations; its image spotcheck was separate from FPS
scoring. General buffer-use recording still reaches its 1024-entry bound: it does not prove absent
reads or full alias coverage.

Same-binary ABBA+BAAB, 8 runs, 30s warmup/45s score, fixed camera, unchanged 1080p configuration:
OFF 45.8093 FPS, ON 45.8136 FPS (+0.0095%); mean run-median footprints 5745.04/5772.42 MiB.
The two order-specific results disagree; no FPS gain established. Warning pressure stayed allowed,
score-window swap did not grow, and all eight finished with cleanup. No screenshot/readback/native
capture/GPU stage profiling ran during scoring; external recording state remains unverified.
Only the separate 173 diagnostic consumed the newly authorized one-run 30s swap-rate exception;
all eight comparisons and the following lifecycle regression used the default 6s rate window.

The final library with the candidate ON also completed two isolated native SetHp death/respawn
cycles with validated HP-zero/loading/world-down/stable-positive-HP and before/after mapping
inventories. No new texture-format/Metal failure appeared. These are not enemy-combat/online tests;
the fixed view had MetalFX/object motion inactive. Motion-query unit compatibility is not full
active gameplay coverage. Formal saves, preferences, INI and caches remain untouched.

The launcher exposes this as its 34th control (21 ON/13 OFF total), not a proven improvement. All
35 small uploads and synchronization remain: the complete original read/alias proof is unresolved.
See [policy, tests and measurement limits](METAL-DEPTH-LAYOUT.md). Private evidence: depth-layout173
suite-ABBA/suite-BAAB/eight-run reports and depth-layout174/run174 lifecycle result; not public payloads.
