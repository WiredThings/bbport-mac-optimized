# 上游纹理差异筛查（2026-10-25）

- 上游bbport 0.6固定提交8185b88的gpu/textures/texture_cache_upload.cpp会在GpuModified资源第一次GpuDirty上传时也记录每级hash。本地此前只在!GpuDirty时记录。人工真实Metal测试已复现：先Storage绑定上传0x11、GPU清红、同页无关CpuDirty后，旧实现像素变回0x11；真实CPU写0x77和GpuDirty强制刷新两项控制通过。尚不能声称这就是用户战斗白模根因。
- 上游同提交buffer_cache_layer.cpp的LayerTrapHandler将解除Mirror保护移入layer_trap_mutex，避免刚重新设立的trap被旧handler清掉。本地缺此变化，但当前Metal日志显示0mirrors/0watched/0trap hits，GPU使用Shared in-place；因此不能把此VRAM镜像竞态认定为当前白模或卡顿原因。本轮不盲移植这条不活跃路径。
- shadPS4 #4569仍为Open，描述受击/染血纹理蓝紫异常并伴Image overlap resolve failed。本地保留日志未见该警告，不能等同。
- bbport #179虽Closed，维护者答复让Windows分支用户找其分支维护者，并没有给出修复；不能把Closed当现成修复。

来源：
https://github.com/deadinside28/bloodborne_pc/blob/8185b88/gpu/textures/texture_cache_upload.cpp
https://github.com/deadinside28/bloodborne_pc/blob/8185b88/gpu/layer/buffer_cache_layer.cpp
https://github.com/shadps4-emu/shadPS4/issues/4569
https://github.com/deadinside28/bloodborne_pc/issues/179

只读下载的上游源码和哈希清单在/private/tmp/bb-upstream-8185b88。运行回归结果在evidence/first-upload-regression-1792863091096195000。
