# Bloodborne Optimized：macOS 血源运行器

[English](README.md) · [下载 0.2.28](https://github.com/WiredThings/bbport-mac-optimized/releases/tag/v0.2.28) · [使用指南](docs/USER_GUIDE.zh-CN.md) · [分支改动与第三方来源](docs/THIRD_PARTY_CHANGES.md)

这是 [DiAvisoo/bbport-mac-optimized](https://github.com/DiAvisoo/bbport-mac-optimized) 的
**WiredThings 分支**，提供独立 macOS 启动器、**直接 Metal 渲染**、**MetalFX 时域升频**，
以及英文、简体中文、繁体中文启动器界面。当前公开版本是 **0.2.28（0228）预览版**。

启动器本身是原生 Apple Silicon 程序；Metal 路径中的游戏与渲染器宿主代码仍是 x86-64，
通过 **Rosetta 2** 运行。项目基于 bbport、shadPS4 和社区渲染器工作，不是 FS／Sony 官方 PC 版。

## 安装与启动

需要 **Apple Silicon Mac、macOS 26 或更新版本、Rosetta 2**，以及自行准备的、
**已合并 1.09 更新的解密游戏文件**。安装包自带 Python 和运行库，游玩无需安装 Xcode、Homebrew 或自行编译。

1. 从 [Releases](https://github.com/WiredThings/bbport-mac-optimized/releases/tag/v0.2.28)
   下载 **Bloodborne-Optimized-0.2.28-macOS.zip**。
2. 解压并把 **Bloodborne Optimized.app** 放进“应用程序”；更新前先退出旧启动器和游戏。
3. 选择同时包含 `eboot.bin`、`sce_sys`、`dvdroot_ps4` 的完整合并目录，
   不要只选择单独的 `CUSA…-patch` 更新目录。
4. 选择界面／游戏语言、输出分辨率和全屏状态，点击“启动游戏”（⌘R）。
   MetalFX、游戏特效、帧率和优化开关位于“高级设置”。

应用使用临时签名，未经过 Apple 公证。若被 macOS 拦截，可在“系统设置 → 隐私与安全性”中
对这个应用选择“仍要打开”。更新合并、按键、存档位置和故障排查详见[使用指南](docs/USER_GUIDE.zh-CN.md)。

## 本分支做了哪些工作

| 方面 | 相对优化版上游新增／集成的内容 |
| --- | --- |
| 多语言 | 三种启动器语言；独立的游戏语言选择；中文文本／字体检查和区域解锁联动 |
| Metal 后端 | 将 Huax-1839 的 Metal 引擎接入现有启动器、运行时、语言／分辨率补丁和快速存读档；保留 Vulkan 兼容路径 |
| 时域输入 | 将 IFreemz 的游戏速度图镜像路线适配到 MetalFX；合批速度图绘制，关闭动态模糊时仍保留速度图生成 |
| 内存 | 按图形内存账本计预算，避免重复计算 no-copy 游戏内存；完善 GPU 完成后的回收、闲置上传后备回收、分配代次跟踪和失效采样副本回收 |
| 地址表 | 把固定 1 GiB GPU 平铺地址表改为两级页表，同步修改着色器寻址 |
| 渲染效率 | 采样器快速缓存；安全省略无用附件加载；直接清除场景副本；alpha 合并；兼容清除合批、复制／平铺、资源访问与同步细化 |
| 前端 | 25 项 Metal 优化开关、依赖和证据标识；按后端显示适用选项；修正呈现／帧率标签；按 macOS 规格处理并署名图标 |
| Vulkan 兼容 | 保留此前移植的着色器和命令内存修复，使用独立缓存；这些 Vulkan 专用改动不算作 Metal 加速 |

独立启动器、超宽屏界面管理、补丁系统和快速存读档的基础来自上游。
Metal 引擎、原始游戏补丁、移植算法均保留原作者归属。
[来源表](docs/THIRD_PARTY_CHANGES.md)进一步区分上游已有功能、第三方移植和本分支的集成工作。

## 中文支持与第三方补丁

在“**游戏语言**”选择“**简体中文／繁體中文**”，退出并重新启动游戏。
游戏目录必须已有相应中文文本和字体；此设置不改变配音。

内置 **Unlock Game Region** 区域解锁补丁的作者是 **Lance McDonald（manfightdragon）、Kyo**。
这个补丁在上游 XML 补丁库中已经存在；本分支将其接入中文语言选择，自动作用于启动镜像，
不会因切换语言而改写原始游戏文件。**安装包不提供汉化文本包、中文游戏数据或字体。**

其他帧率、分辨率、特效和调试补丁保留 [`patches/Bloodborne.xml`](patches/Bloodborne.xml)
中的作者信息。超宽屏 UI 包和调试字体需要用户另行取得。
详见[中文设置](docs/USER_GUIDE.zh-CN.md#启动器与游戏语言)和[补丁来源](docs/THIRD_PARTY_CHANGES.md#game-language-and-community-patches)。

## 画质、性能与优化开关

- “分辨率”是输出尺寸；“高级设置 → 超分辨率 → 预设”决定 MetalFX 输入尺寸，
  启动器会显示实际场景渲染分辨率。MetalFX 为时域升频，“原生抗锯齿”以 1:1 渲染。
- Metal 优化区默认 **20 项开启、5 项可选实验关闭**；已有的明确选择会保留。
  游戏物体速度图和实验性遮挡剔除分别有独立开关。
- 五项可选实验是跨块快照复用、片元参数专用化、计算内核小参数写入、宽松浮点运算、二进制管线归档。
  收益未确定，部分有实测内存成本，见[开关证据](docs/release/METAL-OPTIMIZATION-CONTROLS.md)。
- 本分支主要在 **基础款 M4 Mac mini：10 核 GPU、16 GB 内存**上验证。
  场景、渲染尺寸、特效和缓存状态都会影响帧率，**不保证全程 60 帧**。
  历史对照和内存取舍见[测量说明](docs/THIRD_PARTY_CHANGES.md#measurements-and-limits)。

优化保留已选分辨率、特效和有效缓存容量；速度图等功能仍有资源成本，内存预算也不是进程 RAM 硬上限。
首次进入区域仍可能因资源加载或着色器／管线编译短暂卡顿。此前放弃的离线编译库实验不在正式发行中。

## 按键、存档与更新

默认 **F1** 设置、**F2** 快速存档、**F3** 快速读档，可点击启动器按键标签重映射。
快速存读档需先开启；读档会恢复磁盘存档检查点并重启游戏，不是完整内存即时存档。
[更多默认键位与存档位置](docs/USER_GUIDE.zh-CN.md#按键与快速存读档)。

设置和存档在应用外，替换完整 App 会保留它们。默认存档根目录是
`~/Library/Application Support/bbport/user`，与已有 bbport 安装共享；隔离试用时可先复制存档目录，再选择副本。

## 源码与致谢

- [源码、固定依赖与构建方法](docs/release/SOURCE.md)
- [完整分支工作与第三方署名](docs/THIRD_PARTY_CHANGES.md)
- [0.2.28 变更](docs/release/0.2.28.md) · [反馈问题](https://github.com/WiredThings/bbport-mac-optimized/issues)

感谢 **DiAvisoo、bmy/bbport-mac、deadinside28、shadPS4、Huax-1839、IFreemz、Awakenchan、
Wooloomooloo2／Mark Hall** 及各依赖组件贡献者。
许可证以 [LICENSE](LICENSE)、各源码文件声明和包内第三方声明为准；
猎人印记图标[单独署名](launcher-macos/Sources/BBLauncher/Resources/Artwork/ATTRIBUTION.txt)。

公开包**不含游戏程序／数据、存档、着色器缓存、下载模组、World Warper、QA App、私人路线或本机采样产物**。
启动器图标是明确署名的例外艺术资源；独立游戏进程的 Dock 封面在本机从玩家自己的游戏目录读取。
上游历史技术介绍保留在 [UPSTREAM-README](docs/UPSTREAM-README.md) 和 Git 历史中。
