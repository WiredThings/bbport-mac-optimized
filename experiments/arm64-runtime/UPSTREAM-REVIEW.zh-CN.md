# 隔离 ARM64 原型：上游实现核查（2026-10-23）

经用户授权，由三个 subagent 只读并行搜索，主线程复核 PR 状态与本地代码位置。本轮没有将搜索结果直接合入正式版。目标仍未完成：动态 UI 文字空白，尚无可操作场景、F3、重生、联网或性能对照通过的证据。

## 可复用实现及优先级

1. Darwin 写者唤醒：Zagrios/FEX 的 MIT 文件用 wake_all 处理同地址上的读写混合等待队列，与私有原型复现后的窄修复一致。当前旧路径8秒看门狗退出7，修复100次交接约0.64秒完成；不证明全游戏所有锁均正确。
   - https://github.com/Zagrios/FEX/blob/9d0d7ec0f441e18c58e7d6610d3ad3fb8bcf81a7/FEXCore/include/FEXCore/Utils/WritePriorityMutex.h
2. ESR 分类：voile-lkd 的 fexdarwin 文件（文件级 MIT，仓库顶层 GPLv3）按 ESR EC/DFSC 区分页保护与对齐。与本机真实 DFSC7/0x21 实验一致，不应整体替换 Metal 信号处理器。
   - https://github.com/Zagrios/voile-lkd/blob/db324afd0ff0136f12c8d23f61196c8c1efbed71/fexdarwin/include/DarwinContext.h
3. shadPS4 #5444 已合并；已在私有 Metal 适配条件复制。真实 Metal 小纹理读回复现旧路径两种 CPU-dirty 情况被覆盖（退出7），干净与GPU-only对照通过；修复后四种通过。证据见 evidence/texture-expansion-baseline.json 与 texture-expansion-fixed.json。尚不能将其认定为文字缺失根因：标题时未命中 ExpandImage，后续加载命中的扩张均为有效GPU数据。
   - https://github.com/shadps4-emu/shadPS4/pull/5444
   - merge 699009c94932c64080391cf08d8131fc653ecee8
4. shadPS4 #5410 尚未合并：刷新过程中 CPU 晚写入的 dirty 标记可能被 UploadImage 尾部清除；私有 Metal UploadImage 仍有该清除。作者有硬件对照，但不能当作已证明本次缺字的原因。应先做受控写入、GPU读回和 dirty 代次测试。
   - https://github.com/shadps4-emu/shadPS4/pull/5410
5. ConstantRing 快照：bbport #68 是开放 issue、无确认修复。可只在隔离配置中比较快照开关，对文字 draw 的顶点/UV/颜色及图集上传记录做对照；不能直接禁用后宣称根因已解决。
   - https://github.com/deadinside28/bloodborne_pc/issues/68
6. HyperBridge TSO：MIT 补丁0019/0055提供进程固定策略、每个宿主线程准入及失败保护。不可直接套用Windows/ARM64EC汇编。当前临时签名探针内核支持=1、启用返回5，软件TSO保持开启；未来取得授权后先做硬件+软件并存覆盖验证，再考虑Context级停用软件。没有可证实的免费账户成功流程。
   - https://github.com/t0b1kent/hyperbridge/blob/ac49813a48908d7a4a94f0e5583fedbe8fbc71e0/fex/patches/0055-HW-TSO-R2.patch
   - https://github.com/t0b1kent/hyperbridge/blob/ac49813a48908d7a4a94f0e5583fedbe8fbc71e0/fex/patches/0019-Darwin-no-process-wide-hardware-TSO-from-a-single-th.patch
7. LookupCache 分配失败回传：MIT 修复可排在后面核对，同时检查线程初始化失败时的栈/TLS清理。
   - https://github.com/Zagrios/FEX/commit/7f6e2a3a7709d9842bfe1b7ab9ddc157c1f24e82

未找到可直接命中本次所有动态文字空白的已合并补丁。系统字体挂载 #4028、Linux locale FEX#2696、SVE256 FEX#5555 不匹配；FEX#5708 舍入清位、#3927 AFP 处理已在本地，不应重复移植。

## 当前确证

- 最新43项定向回归：evidence/suite-1792757500499536000/results.json。含锁红/绿、真实Darwin异常、ABI/回调/TLS/VM以及启动补丁的整体拒绝检查。
- 三项启动功能已接入私有加载器：官方中文区域解锁、默认离线、跳过开场，7处94字节。每条校验原字节，全部通过才修改进程镜像。来源为正式分支已有的 Bloodborne.xml，保留 Lance McDonald、Kyo、illusion、bbport 作者字段。设置语言11；不需要外置字体。
- 固定QA种子仅复制到每轮新状态目录，并在副本中将CUSA00207目录映射CUSA03173。源QA的15个文件哈希不变，正式资料不参与。
- evidence/game-1792757530237447000 已进入场景加载，持续编译管线；120秒预算结束，不能当作锁死或加载完成。
- evidence/game-1792757727646174000：复用此前私有QA缓存，完整读入内置zhocn/font.gfx 2849777字节（44次读取），中文menu/item文本均完整读取关闭。文字仍空白，不是资源不存在；此轮中立输入停在标题，170秒后由主线程手动结束等待研究结果。压力始终1，swap净减少32MiB。结果exit0只表示受控退出，不是游戏验证通过。operator-note.json记录手动原因。
- 正式应用、存档、设置和缓存未应用这些开发修改。后续优先用小资源合成图集测试与文字绘制快照定位，不增加缓存预算或常驻诊断。

## 原生宿主页保护跟进（2026-10-23）

- game-1792758448447919000：标题常量环快照关闭后文字仍空白；约49秒严重压力保护停止，未完成场景对照，不能排除所有常量环时序问题。
- game-1792758816662045000：#5444适配后仍缺字，加载时提交8658停滞，CPU持续陷入 Front::HandleFault，GPU处理OnWriteFault；不再是已修复的FEX写锁等待。主动结束后SIGTERM未能退出，核实完整路径后只结束本轮两个隔离进程，exit -9不算通过；240秒压力最高2，swap净减少16MiB。
- 原生ARM64的页大小16KiB，GPU跟踪粒度4KiB。旧runtime_memory_gpu_protect直接使用4KiB范围调用mprotect且忽略错误：对齐的调用影响整个宿主页，未对齐调用可能失败。实际运行库合成测试确认，解除一个子页会错误解除同宿主页其他资源保护（旧退出7）。
- 私有 native_host_protection.h 保留4KiB资源权限，按16KiB宿主页取最严格交集后调用内核；GPU与读写陷阱独立保留，映射销毁清除元数据，拒绝把客体本身的保护异常认作GPU写入。Front::HandleFault按阻塞子页分别调用原有后端失效/读回逻辑。未变更GPU页表或缓存预算；一字节/被触及4KiB页的稀疏元数据，实际驻留量仍待游戏测量。
- 宿主页定向测试包括独立GPU/陷阱所有者、读陷阱、跨宿主页边界、客体只读权限、销毁后清除、阻塞页枚举。新路径通过，旧路径保留红色对照。45项完整CPU/桥回归：evidence/suite-1792759490708493000/results.json。
- game-1792759617326899000：中文菜单目视恢复，“继续/载入游戏/新游戏/系统/登入”正常。单帧R8候选图集的CPU副本1MiB，非零16494字节；抓取仅按明确触发执行一次，临时上限2MiB，不是常驻缓存。已确认“继续”载入固定QA副本，场景、可操作性、F3、重生、联网与性能仍未通过。

构建依然复用只读基线对象；build_heap_bridge.py现在重编译6个私有文件（原4个、texture_cache.cpp、remote_front.cpp）。运行库通过现有CMake低并发重编译。所有修改仍只在Shared隔离目录，没有正式安装或发布。

### 当前可操作场景证据与性能条件

game-1792759617326899000已从固定QA种子进入欧顿小教堂外场景；通过脚本输入确认镜头转动、中文物品栏打开/关闭及短暂前移，用户也确认“进游戏了”。整轮600.3秒，预算正常结束exit0，无严重压力，最高2，swap净减少1551.3MiB。该轮含一次单帧抓取和视觉核对，未作为性能对照。

重要条件：当前原型是原生1920x1080、MetalFX关闭、原游戏特效未关闭、LOD0；启动附加仅区域解锁/默认离线/跳过开场。定点核对boot中的60FPS时步及解锁地址仍为原始字节，未应用60 FPS补丁，不能和正式版已解锁条件直接比较。已在新状态目录默认创建完整明确的bbport.ini并开启FPS，正式配置不变。

无抓取/纹理诊断的复测game-1792760303644781000：中文主菜单和FPS计数器可见（菜单30FPS/33.3ms）；已确认继续加载QA。场景FPS及与Rosetta相同补丁的对照尚未完成。后续还需F3、连续敌人击杀重生、bbnet与内存长测。

纹理扩张红绿用例已可通过tools/run_texture_tests.py复现；8项通过，记录每次实际加载库和哈希：evidence/texture-suite-1792760256015858000/results.json。正式11671个受保护文件哈希与此前快照完全一致，见/private/tmp/blood-echoes-arm64-metal-followup-20261023/native-runtime-host-pages-after.json。

### 21点后：区分30帧上限与FEX调用桥开销

用户确认进入场景后只有约25帧，因此不能把差距归结为漏接解锁。隔离入口已接正式版的 `Uncap FPS++`、`Sprint Fix (High FPS)` 及480Hz vblank，不使用固定步长60帧补丁。XML内部已有顺序覆盖，先按正式顺序求最终字节，再归一为82个无重叠原字节校验记录、780字节。已逐字节核对与正式补丁生成结果相同；计数上限改为512，180条和越界513条分别通过/拒绝，仍全部验证后原子应用。

解锁后的全特效、原生1920×1080、LOD0、MetalFX关闭、固定QA存档站桩基线为27.63 FPS（末60秒去掉末1秒）；平均GPU执行25.21ms、帧间隔36.19ms，不能将两者简单相减作为CPU耗时。菜单可到200帧以上。原型确实存在独立于30帧上限的性能差距。

CPU三秒采样记录 `/private/tmp/arm-cpu-91215-performance.sample.txt` 显示 State::valid 的全表扫描和 shared_mutex 热点。私有改动：生命周期固定的镜像/跳板/宿主堆范围用只读二分前缀索引；本线程临时栈只在映射存活的调用作用域内走TLS校验，退出/嵌套恢复后不留旧权限；空的发布资源表无需取得读锁，非空发布/撤销仍原锁同步。第一版同条件33.38 FPS，较基线约20.8%，GPU25.64ms；这是单顺序初测，非Rosetta对照、非普适收益。两轮压力最高2，swap分别-32/-16MiB。没有更改纹理缓存容量或画质；新增静态索引9696字节，不能以其大小代替真实内存回归。

第二版进一步避免静态索引未命中后重复扫描同一静态前缀；客体VMA仍每次读权威映射表，动态资源和其他线程临时栈保留活跃校验。定向新增可写VMA、改为只读后拒绝写、释放后拒绝写的客体机器码测试，新旧路径都验证。最新55项回归通过：`evidence/suite-1792761603121599000/results.json`。第二版真实性能测试仍在进行。

**比较条件修正**：正式 `bbport.ini` 本来关闭景深、动态模糊、SSAO、游戏AA、动态光源阴影；旧原型默认原版全开，因此不能把先前25帧对比正式版的差距全归FEX。现已只读快照为 `fixtures/launcher-settings.ini`，保持 `fixtures/correctness.ini` 供前后全特效对照。C++主程序改为只复制私有配置模板，不再写另一套默认图形值；`prepare_startup.py` 使用固定正式源码快照的 `launcher_patches.py` /完整 `Bloodborne.xml` 生成FPS、特效、LOD、区域和离线补丁。来源SHA记录于 `evidence/launcher-policy-provenance.json`。当前仅验证原生1080p配置；其他分辨率暂拒绝，不能默默应用不完整viewport补丁。联网仍按QA隔离策略关闭，F3仍待接入，不能宣称已成为正式可切换后端。

第二版真实同条件采样完成：`game-1792761645082434000` 的末60秒38.12 FPS、GPU23.97ms；基线27.63，单顺序约+38.0%。swap -32MiB、最高压力2。无反向重复，暂不能当普适收益。

统一入口使用正式设置只读副本的隔离实测：`game-1792761878451760000`，末60秒45.54 FPS、GPU20.77ms，swap变化-80.0MiB、最高压力2，按预算正常停止。此配置与全特效基线不同，不把两者差异算成代码收益，也未完成Rosetta同条件对照。正式受保护11671文件哈希与本轮前一致：`native-runtime-bridge-perf-after.json`。

### F3真实回归与临时线程栈快照竞态

已复用正式版 `quicksave.py`（原普通存档事务，非RAM快照）以及SaveRequest双提交/自动Continue机制。隔离主程序提供会话身份标记与受控resume，不再从QA种子覆盖当前工作进度。原型重启通过单次随机标记与退出85交给监督器，先等待旧ARM Metal子进程回收，再离线重新生成补丁与启动；会话总时长、swap增长、严重压力和锁屏保护不随F3重置。伪造、重复、软链接请求、错误会话、读写权限和恢复前备份有定向正负例。`runtime_restarting` 从错误的函数占位修正为正式协议要求的数据变量，正常初值0。

首轮真实回归 `game-1792762608095377000`：物理F2请求后角色/配置均新提交、检查点创建成功；物理F3第一次自动Continue返回可操作场景。第二次F2保存了与最初种子不同的角色状态，存档manifest中的userdata0000/0010及备份均变化。第二次F3的恢复事务完成，但新主进程启动以74退出：`host_write_permission ... size=0x100000 host_protection=0`，不能算第二次F3通过。监督器随后清理GPU；`stopped=null`，不是内存保护触发。峰值压力2、swap6892.94→峰值7915.44MiB；原检查点和两份恢复前备份均验证保留。

故障来自本项目新写的ARM入口：创建FEX线程时复制了所有线程的临时栈范围；复制后别的线程可以退出并解除映射，FEX随后正确拒绝这个已过期的权限声明。修复只向FEX启动验证传递永久可执行范围与本调用自己拥有的存活栈；宿主本栈仍必须可写且不可执行，客体/宿主普通数据仍由State::valid与动态VMA实时校验，不是删除权限保护。

定向 `test_thread_bootstrap` 在实际FEX API上强制退役一个无关栈，旧全表请求确定失败，新请求能执行返回42；本栈改只读后仍拒绝。进一步用作者自造客体程序并发创建/退出512个线程：旧快照对照三轮均74退出；修复版三轮均完成全部512线程（共1536），见 `evidence/thread-churn-1792763498552198000/results.json`。已纳入完整62项回归：`evidence/suite-1792763681332270000/results.json`。Mach查询端口数对照11→11，没有发现此处端口累计，不能将其报告为内存泄漏修复。

卖血/治疗卡顿的初步时间对齐使用帧文件birth time：第一动作附近7秒窗20次管线编译，最长1231.36ms；相邻无动作窗无编译，平均GPU约28.5–28.9ms相近。第二动作附近有14次编译及397.35ms长帧。该轮包含截图/远控和刚加载的新动作，未拆出FEX JIT、Metal编译等待与其他CPU同步时间，不能归结为单一原因；动作附近swap没有快速增长。用户看到第二组操作是为比较检查点19瓶/当前18瓶而有意重复，非容量中断后重做。

复测将失败会话只克隆到新的私有工作目录，保留原故障现场；使用现有检查点自动Continue，不再反复卖血，不调用截图。复测结果见下一节。

### F3修复后的真实复测结果

`game-1792763837786045000` 使用故障会话的新私有副本，连续两次物理F3均完成：旧Metal进程回收、离线恢复检查点、重新启动、自动Continue至角色就绪。日志有三次角色就绪（初始加两次重开）、两次受控重启，没有host_write_permission、ARM callback failed或FATAL。240.25秒总预算正常结束，exit0，cleanup_warnings为空；结束后进程核对无残留。期间不卖血/治疗、不截图。该轮验证恢复与启动流程，不是同条件性能比较。

压力最高2；系统swap从7667.38MiB至8214.00MiB，峰值及完整样本见 `evidence/f3-fixed-compatibility-result.json`。单轮系统swap不能证明与Rosetta内存等价。最终完整62项回归通过：`evidence/suite-1792764147765690000/results.json`。正式11671个受保护文件与基线完全相同（`/private/tmp/blood-echoes-arm64-metal-followup-20261023/native-runtime-f3-after.json`）。

卡顿仍待轻量拆分FEX JIT、Metal管线编译和CPU同步等待；不能把F3竞态修复当作卡顿修复。连续死亡重生、bbnet、Rosetta同条件性能/内存仍未通过，不更新正式应用或发布。
