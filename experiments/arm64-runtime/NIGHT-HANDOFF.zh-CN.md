# ARM64/FEX 夜间进展与交接（2026-10-25）

## 范围与边界

目标是替换 Rosetta CPU 组件，复用 Blood Echoes 原启动器、Metal 后端与现有补丁。正式应用、存档、设置和缓存保持不变。仅使用 Shared 隔离配置、已有 QA 进度的新副本和私有 GPU 缓存；不降低分辨率/特效，不缩小缓存。

按用户最新指令，死亡重生、在线和菜单实测本轮暂缓，醒后统一验收；不把历史通过当作本构建通过。所有游戏测试静音，无持续录屏/Cua。严重压力、快速 swap 增长和锁屏保护未放宽。源码只推送 `dev/arm64-runtime-audit`，不更新 Release。用量任一相关可读窗口剩余低于15%即停止开发/测试，整理交接和最新测试包；最近可读窗口剩余52%，其它窗口不可用。

## 已修复并有定向证据

1. **退出协议冲突**：保留原75/76语义，仅显式监督重启路径能发出。缺失导入80、提交前诊断81、保留码误透传guest82/backend83。包括低8位折叠的331/332和负数都已防护。17项自制x86/FEX退出样例、6组原监督脚本，以及子进程回收回归通过。没有重写原监督器。
2. **ABI 生成器漏接原接口**：从 PAIR/DIALOG 宏名字白名单改成 Clang 显式语义 marker；普通、未知宏、嵌套宏可识别，host helper、无关注解、仅声明不会误接。16个原有DIALOG接口复用现有实现。当前343个包装器签名无unsupported；不等于全部导入已支持。
3. **libc完整跨度**：memcpy/memmove/memset/memcmp 在调用现有实现前证明全部访问范围，保留原写入准备/通知。19项真实FEX正反例通过。仅对已明确完整证明的参数去掉重复首字节证明，没有取消范围校验。
4. **真实纹理一致性错误**：GpuModified图像首次GpuDirty上传时没记mip hash，之后邻页写入保守置CPU-dirty时，会将未变的CPU旧数据覆盖GPU结果。真实Metal像素回读证实旧基线红像素255被覆盖成0x11；候选保住GPU结果，真实CPU改变和GpuDirty强制刷新仍生效。三项定向和既有四项扩展回归全通过。采用上游同策略，不增加hash存储或降低缓存容量。[上游实现](https://github.com/deadinside28/bloodborne_pc/blob/8185b88/gpu/textures/texture_cache_upload.cpp)
5. **手动QA入口**：发现新生成入口又带着BB_ARM_QA_PAD_FILE，重新屏蔽物理输入。生成脚本已默认移除，新增拒绝空值/0值等自动化变量的3项回归；自动benchmark仍独立使用文件输入。此处是打包配置修复，不是重新实现手柄。
6. **打包依赖路径**：冻结CPU/宿主库/GPU时，复制后的SDL和旧rpath会同时加载；已统一包内依赖路径，解压后自制FEX样例无重复Objective-C类警告。外链严格签名问题也已改为复制运行依赖。

## 验证与性能

最新开发CPU SHA256：`88da6e9e425efcd51e89a3ef269cc25904b06f249775e92914ffdb1bbfa06344`。

- `suite-1792864806185609000`：117项无窗口回归通过。
- `libc-spans-1792864806283513000`：19项跨度回归通过。
- `exit-contract-1792864806537078000`：17项FEX退出样例、6组原监督器和回收回归通过。
- `first-upload-regression-1792863091096195000`：旧基线像素错误被确定复现。
- `first-upload-regression-1792863287573965000`、`1792863342008165000`：修复3项、扩展4项通过。
- `first-upload-abba-1792864209177601000`：旧46.85FPS，新47.38FPS（约+1.1%），仍在波动内，**不宣称提帧**；相同boot/content/patch/settings/camera。
- `first-upload-memory-1792864532896586000`：内核去重快照5626.93→5639.40MiB，约+12.47MiB；无更高压力/swap增长。先前聚合footprint多算了共享页。单对快照不是长期不增长验收。
- `launcher-engine-abba-1792864949670891000`：最新整合CPU+纹理修复固定场景48.9995FPS、GPU19.19ms、聚合footprint5797MiB、swap+76.6MiB；这是单轮，不能归因给span去重，也不能与早期不同二进制的ABBA混成因果结论。
- `frozen-qa-package-1792865967795256000`：最新zip解压后严格签名通过，4项自制FEX退出样例通过，包内Python运行架构arm64，无实际游戏启动。

## 白模：发现了相关真错误，尚未证明用户症状解决

两次血子弹特效纹理metadata捕获完整，选中图像地址全部覆盖请求；单次静态画面HUD+5证实动作触发，未见整体白角色。但用户的探索/战斗偶发白模尚未复现，不能宣称已修复。

已修正旧观察器自身在memo-hit时解引用被清空slot的错误；捕获只读owned prepared快照，不重新读取guest描述符。性能候选与捕获版严格分开，纹理修复候选只替换一个对象，不带捕获开销。

上游另有mirror trap重装/解锁竞态，但本路径Metal统计0镜像/0陷阱命中，未盲目移植为白模修复。旧PS4固定guest基址假设同样记录为休眠镜像路径风险，不因此启用镜像。

## 长编译与探索长帧：已分开两类证据

原人工探索曾有唯一新shader/PSO编译：VS最长326.6ms、FS209.7ms、PSO188.4ms。MSL源码缓存命中仍要newLibraryWithSource，不能把它叫预编译metallib。弱library复用减少110/538次API调用，但未证明累计时间或进场收益，继续保持实验选项、不默认启用。没有恢复此前已撤销的离线Toolchain功能。

本轮新建空**应用**缓存，再复用它跑相同的有界动作请求。原有QA/正式缓存和系统驱动缓存完全保留；这是空应用缓存+暖系统缓存，不是真正冷驱动缓存。动作按时长请求，不是确定性路线，不作FPS对照。

| 诊断 | 空应用缓存 | 随后复用缓存 |
|---|---:|---:|
| 场景camera首次出现 | 17.51s | 17.31s |
| 首次动作之后新增shader | 37 | 0 |
| 动作后shader总scope | 201.21ms | 0 |
| 动作后library总scope | 125.20ms | 0 |
| 动作后PSO数量/总scope | 40 / 19.06ms | 0 / 0 |

暖缓存启动前预载275个shader（全源码命中），但仍调用275次Metal library编译，总scope约469.8ms。冷应用轮有一个192.1ms的PSO发生在camera出现之后、动作之前，不能把camera出现当作完全可操作/完全加载。shader scope包含library，表内类别不能相加当帧延迟。

证据：`launcher-engine-abba-1792865327869171000`、`1792865415904408000`。后者仍出现130.48ms帧：GPU20.41ms、绘制记录6.83ms、FEX编译重叠0.792ms（CPU仅0.019ms）、没有Metal编译。这说明还存在非编译长尾，不能归咎于长编译本身。普通cond_wait覆盖这段只说明有等待，不证明等待本身是根因。

补开有界producer/guest提交记录后，`1792865593487382000`未重现：完整trace无丢失/覆盖、1970帧最长26.58ms。**原因未定，不能当已解决。** 后续重点记录同一依赖链的生产者CPU、提交、资源页/失效等待，先复现再修改同步。

## 原启动器兼容性还剩什么

最新显式imports元数据审计 `launcher-source-inventory-1792865790099149000`：16个启动/汉化/存档/联网脚本全部逐字节一致；GUI36文件中28相同、8是已审阅隔离/架构准入/文案差异；36项Metal选项均有消费者，但消费者存在不等于收益已测。

实际762个所需导入仍有25项unresolved：16个AvPlayer（原后端实现存在，但guest回调/句柄桥未接）以及9项kernel/libc边缘接口。旧报告41项已过时，工具现在能显式指定新运行的imports元数据并记录摘要。不能拿343个可包装接口称完整兼容。

AvPlayer须保留原实现并正确桥接callback、allocator、opaque handle和线程退出；不能直接将guest地址当host函数，也不能用返回成功空壳绕过。剩余9项须区分原本也未实现、guest模块替代和真正漏接。闲置菜单触发缺失AvPlayer现在安全报80，不再重启循环，但完整播放兼容仍未完成。

## 长期研究结论

已按用户授权完成FEX特化subagent研究，见 `docs/fex-specialization.zh-CN.md`。FEX已经按需翻译；删掉未用ISA处理器主要减少冷代码/磁盘体积，不会自动缩短已经生成的热代码。没有证据证明观察过的指令集合覆盖血源所有场景。

长期目标：**版本绑定、保留完整通用回退的Bloodborne FEX/Metal协同层**。先有界统计热PC/指令/编译与执行成本；优先完整范围和资源代际下的重复通知/快照消除，以及完整shader/PSO键已知后的有界准备队列。不降低浮点/访存语义，不跳绘，不增大archive或缓存。

硬件TSO仍未通过系统签名/profile准入，保留软件TSO；用户同意不能代替Apple provisioning。每个实际执行guest的宿主线程都要成功准入，才能停用软件排序。

## 最新本机测试包

`artifacts/arm64-qa-20261025/Blood-Echoes-ARM64-Local-QA.zip`，约42.81MB，SHA256 `50d4823d60a7c22ef09d6212da52c9436b3835b03e8da51cbfc5671b9079ae8d`。

这是**本机隔离QA包，不是跨机器安装包**。CPU、CPU宿主库、GPU候选、原启动脚本和基础依赖已冻结在包内；游戏/汉化/QA配置路径和当前原型硬编码根仍在本机Shared。无游戏、存档、缓存、账号凭据、截图。默认静音、离线、物理输入，不持续采样。

源码快照同样不是原型的clean-checkout独立构建：候选仍依赖既有对象/依赖stage。已在README明确，不能拿正式构建自包含的结论套在此开发原型上。正式版本构建流程没有修改。


## 后续检查点（数据导入与最终包）

- 发现ARM原型把未知数据导入也当作Missing函数分配可读跳板，而原probe.c为数据导入保留PROT_NONE页。自制BBPROBE5样例证实旧CPU可把跳板字节读成数据，正常返回0。这处差异由ARM导入循环引入，源头原实现已正确区分。
- 现在每个未实现数据导入只保留一页不可访问VA，不预填/提交payload，不加入可读/可写guest范围。启用前发布只读范围表，信号路径仅write+_Exit80，原有显式恢复域的EFAULT语义保留。未使用导入不影响进场；NoExec仍通过FEX原错误路径返回非零，不产生75/76。
- 新7例：未使用、首/尾读取、写入、执行、合法data及普通missing函数；旧冻结CPU的读取错误控制仍可复现。新CPU正常场景进场16.97s、固定尾段48.19FPS，无保护停止。这是加载/功能回归，不是提帧A/B。
- `final-cpu-checks-1792868995161926000`：最终88da构建117无窗口、7项data、19项span、退出17+监督6、同包net路径10例全部通过。
- 冻结包网络模块已是arm64。原加载器只接受P/build，复制进包后本会被拒绝。现在额外允许与真实当前CPU处于同一`Contents/Resources/bbport/bin`组件中的模块；复用原启动器原位置，继续要求原ABI3入口，offline不dlopen。仅路径定向测试通过，真实登录/留言/圣杯/合作未重测。
- 最终包：`Blood Echoes ARM64 GUI QA 1792869097396418000.app`，zip SHA `be2a9dc1d2e0e41094c9906c8218ed473a3075f8b5567c402ac963d418a006b7`。`frozen-qa-package-1792869217393211000`验证解压签名/4项FEX/Python；`missing-data-regression-1792869425373087000`验证冻结CPU七例。冻结后的Mach-O摘要因相对依赖/重签不同于构建输入，两个摘要均在manifest保存。
- 新imports审计 `launcher-source-inventory-1792869426120406000`仍为25个未实现：24函数+1受保护data；“有保护”不是“实现了接口”。16脚本逐字节不变、36选项均有消费者。

## 新抓到的非编译长帧

`launcher-engine-abba-1792866665169598000` 在81s内抓到162.305ms长帧，两个trace完整，无丢弃/覆盖。GPU31.36ms、记录13.03ms；没有新Metal编译；FEX块重叠18.731ms但线程CPU仅0.035ms。IPC MsgWriteFault(4)调用67.456ms（CPU0.044ms），同时GPU IPC线程处理八个MsgSubmitGfx(20)，合计50.239ms、线程CPU0.210ms；不是这50ms都在跑代码。

周围系统出现大量compression/decompression，swap总量在该窗口不变（整个运行+378.9MiB，压力最高warning）。已新增只读VM/CPU全局计数，rev1结构152字节由本机SDK实际编译证明，Mach host端口有对应释放；计数是全系统，不能归因给某个进程。现有采样未给出每次缺页等待时间，不能单凭pageins平稳确认或排除内存活动造成的延迟。SDK新ri_page_wait_time_mach字段的当前内核语义/可用性尚未核实，没有将0当作无等待的证据。

下一步分开记录提交快照复制、DrawPreparation::Build、queue.m_access和submit_mutex。MsgWriteFault可能排在这些提交之后；只有缩短已确认的阻塞源才是优化，不宜单纯减少校验或随意合并fault。固定1MiB trace/64KiB CSV尾部，有>=80ms帧后自动结束并保留上下文，未提高缓存或保护阈值。

另记录上游对部分GPU覆盖的完整image-size条件。本地尚缺，但部分GPU写入的前缀也必须正确更新，不能只验证剩余texel不被覆盖就宣称整体正确；先构造全覆盖/部分覆盖/真实修改与保守标记的定向测试，再决定是否移植。当前未启用该改动。

源码导出worktree突然出现9180个带“ 2”后缀的未跟踪源码副本，约93.83MiB，来源未确认（部分内容与较早检查点相同）。没有删除、覆盖或加入Git；后续只能按SOURCE-EXPORT明确清单暂存，不能广泛git add -A，也不能宣称worktree整体干净。此处是源码副本，不是游戏存档。测试运行与构建仍在Shared。


最终冻结CPU/图形主体的原入口场景验证：`launcher-engine-abba-1792869508968741000`，17.24s出现camera，固定尾段48.80FPS、GPU19.25ms、聚合footprint5814MiB、最高pressure2，无保护中止。仍是单轮，不作严格提帧A/B。该记录及上一冻结包记录的旧gpu_variant字段写成baseline，但frozen_component已经包含真实固定二进制摘要；保留原记录，另有metadata-clarification解释为冻结的first-upload修复版，后续工具已改名frozen-component，不能混作旧基线。


最新测试包入口更新为 `Blood Echoes ARM64 GUI QA 1792869948191017000.app`，zip SHA `50d4823d60a7c22ef09d6212da52c9436b3835b03e8da51cbfc5671b9079ae8d`。其CPU/CPU宿主库/GPU/GPU库四个主体与48.80FPS实测包逐字节相同；仅把arm64联网模块也纳入fmt路径归一化/冻结清单，避免日后联网加载第二份fmt。`frozen-qa-package-1792870055920459000`再次验证严格签名、4项FEX与arm64 Python。没有新增真实联网测试。GUI基础版本仍0.2.51，身份以QA时间戳和manifest摘要为准；不是正式应用替换。
