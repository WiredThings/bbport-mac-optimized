# Experimental Mac online integration — 0.2.40

Blood Echoes 0.2.40 , 2026-10-19. Online is optional and defaults OFF.

## 使用方法

1. 使用 0.2.40 或更新版本，选择 Metal 后端，在“联网”区开启实验性联网。
2. 点击网站登录，在 The Hunter’s Dream 的设备授权页登录 Discord 并批准设备码。
   授权码会过期；过期后重新生成。启动器账号状态只代表设备授权完成。
3. 启动游戏，选择在线模式。游戏内公告和实际搜索结果用于确认游戏服务连接。
4. 在任一空的普通仪式祭坛选择“圣杯地牢搜索”，搜索并加入角色满足条件的地牢。
   普通祭坛的编号不限制迷宫种类。临时祭坛用于快捷匹配，是另一套入口。

正式版联网和离线共用选定的正常存档目录，不再创建隔离副本。旧正式版 `online-user` 若存在，迁移前先备份两边：迁移时联网已开启则沿用联网进度，否则沿用正常进度；旧目录仍保留，备份可在存档管理器中恢复。QA 存档永远不参与正式版选择、迁移或默认恢复。

进入联网及启动新的联网游戏前，先校验并归档正常存档与旧快速存档，再从当前已提交的正常存档建立新检查点。F2 在两种模式均可保存；F3 一律等待旧游戏退出，清除联网令牌、模块和路由，以新的离线进程恢复。快速存档开关与联网不再互斥。

F3 或存档管理器恢复后保持离线，直到主动重新开启联网。恢复前写入所选存档目录内的离线保护标记；即使启动器异常退出，再次打开也不会自动联网。标记只在明确重新联网并成功归档、建立新起点后清除。恢复失败会保留备份与离线保护，不会悄悄改为在线启动。

手动关闭联网时仍会从当前正常存档建立检查点；F3 恢复不会重新生成或覆盖它正在重试的检查点。没有完整正常存档的新角色无法预先生成检查点，可在游戏首次保存完成后用 F2 创建；缺少或校验失败的检查点不能恢复。正常自动存档始终启用。服务器已经提交的留言、统计等不随本地回档撤销。

“存档管理”分正常存档与快速存档：备份、导入 `savedata` 目录、导出、整份恢复、查看目录。恢复前备份当前进度，校验失败不替换；游戏运行时禁止操作。快速存档内部仍为普通游戏存档，外加校验清单和活动指针。备份不会自动清理。开发隔离使用独立 Bundle ID、目录和钥匙串；正式版不扫描 QA 路径。

账号令牌仅保存在当前应用 Bundle ID 对应的 The Hunter’s Dream 钥匙串项，原有标识不变，升级保留登录。设备配对码可点击复制。

## Server

The Hunter’s Dream is the only service exposed by the launcher. There is no custom-server selector or address editor. HTTPS certificate verification remains enabled. The existing port-preserving game routes and the game service credential allowlist on port 20443 are retained. Credential-free routing files live under launcher `out/online-routes`, outside game saves. Only the explicitly permitted THD HTTPS services receive credentials.

## 验证状态

下列游戏内联网结果沿用 0.2.37 隔离测试；存档切换／恢复沿用 0.2.39 自动测试。0.2.40 只新增版本身份和打包验证，合作或入侵仍未验证。

基础 M4 Mac mini 16 GB，CUSA03173 1.09，社区简体中文，隔离 QA 存档：

| 功能 | 结果与边界 |
| --- | --- |
| Discord 网站设备授权、启动器账号读取 | 通过 |
| 游戏内 The Hunter’s Dream 公告、在线场景加载 | 通过 |
| 血迹身影播放 | 已观察到；不等于实时合作 |
| 留言写入、所记笔记列表、删除 | 游戏内流程通过，测试留言已获用户授权删除；未由第二客户端确认送达 |
| 圣杯地牢搜索并加入 | 用户实际操作确认通过 |
| 按铭文直接搜索 | 未通过验证；一个网站目录铭文未搜到，原因未确定 |
| 合作、入侵 | 未测，需要另一名兼容客户端玩家 |
| 官方中文区域解锁路线 | 本次未运行验证；社区中文路线已运行 |
| 离线性能无回退 | 尚无有效前后对照；一次自动 benchmark 因交换增长保护中止，不计作结果 |

The Hunter’s Dream 是第三方社区服务，不是 PSN。不要据此宣称支持 PS4 跨平台联机或所有
shadPS4/bbhost 客户端之间的实时互通。服务状态和配对兼容性由服务端及客户端版本共同决定。

## 实现与兼容性

- 离线时在 `dlopen` 前返回，不初始化网络模块或网络线程；启动入口删除继承的网络凭据和路由覆盖。
  这保证了模块加载隔离，不替代实际帧率/内存对照。
- 联网模块独立编译为 x86-64 `libbbnet.dylib`，游戏宿主仍经 Rosetta 运行。
  Metal 渲染库不重新链接，打包校验渲染库哈希不变；现有两级页表、速度图、剔除和资源回收补丁保留。
- 游戏 HTTP/SSL 和必要的 NP、匹配接口交给模块；奖杯、商店和个人资料对话框保留本地处理。
- 使用 The Hunter’s Dream 后端。TLS 使用系统信任根并校验证书；令牌只附加到明确允许的 THD HTTPS 服务源。
- 联网时显示在线/离线选择，禁止旧“禁用 HTTP”及“跳过联网选择”补丁的地址覆盖，
  包括更名的外部 XML 副本。冲突时在游戏执行前报错，不静默修改已保存的补丁选择。
- 不包含游戏文件、翻译包、存档、账号凭据、World Warper 或开发测试路线。

## 来源与构建

移植自 [Supermedo/bloodborne_pc windows 分支](https://github.com/Supermedo/bloodborne_pc/tree/de2e38af055f09f819946951075c6bb119e62ae6/gpu/bbnet)
的固定提交 `de2e38af055f09f819946951075c6bb119e62ae6`；其中包含 shadPS4/shadnet 和 bbhost 的
The Hunter’s Dream 适配。原文件哈希及许可证见 `gpu/bbnet/UPSTREAM.json`、`COPYING.bbhost`、
`SHADNET-PORT.md`。`vendor-changes.diff` 保留上游的移植记录，Mac 适配差异另见
`MAC-CHANGES.patch`。保留 GPL 和各依赖许可。

在已有 Metal 构建依赖的源码目录中，只构建可选网络模块：

```sh
BB_BUILD_JOBS=2 bash tools/macos/setup_online_deps.sh
BB_BUILD_JOBS=2 bash tools/macos/build_online.sh
```

完整源码构建入口 `tools/macos/build_from_source.sh` 会依次准备联网依赖、编译模块并将模块和许可
打包到暂存应用。它仍要求干净 Git 工作区，不会安装到“应用程序”或修改存档。
网络依赖为固定版本 OpenSSL、Protobuf、MiniUPnP、nlohmann/json 和 cpp-httplib；
依赖清单以 `setup_online_deps.sh` 和 `gpu/bbnet/CMakeLists.txt` 为准。

## English summary

Opt-in Metal-only The Hunter’s Dream integration. Device authentication and in-game announcement
are verified; the user verified chalice search and joining. Message creation/list/deletion was
exercised locally in the game, without independent delivery confirmation. Glyph lookup, paired
co-op/invasion and offline performance equivalence remain unverified. Online/offline share formal game saves. Going online archives old checkpoints and seeds a new baseline. F2 works in either mode; every F3 restore starts offline, strips credentials and stays offline until online is explicitly enabled again. A persistent per-save offline marker protects against launcher crashes. Manual offline transitions seed a checkpoint, while F3 retries preserve their target. Legacy formal copies are backed up before one-time migration; QA is never migrated. Tokens stay in the existing THD Keychain item.

The 0.2.40 release rebuilds only the launcher from the committed public source export. Its game host, online module, Metal renderer and Vulkan compatibility binaries are retained from 0.2.39 and verified unchanged. This is not a newly completed full dependency rebuild.
The updated full-build script includes network dependencies and corresponding-source export.
