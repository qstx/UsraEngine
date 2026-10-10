# 项目资源导入管线验收记录

日期：2026-10-10。环境：Windows x64、Visual Studio 2022、CMake、Vulkan SDK、.NET 10，Debug/Release，Tracy 启用。

## 构建与格式

- Debug、Release 全部程序与测试目标构建无编译/链接警告。
- 在 `Build/CodeOnly` 中，将 Shader 工具指定为不存在的路径，干净构建 Editor 与 Player 成功；没有创建项目 Library 或 Resources 镜像。
- 临时修改内置 Unlit Shader 后重复普通构建，未触发内容导入；随后恢复源字节及修改时间。
- 直接编译隔离 RHI 测试不生成内容。首次 CTest fixture 执行准备项目资源及测试 GLSL；再次执行通过，测试 Shader 输出修改时间不变（总耗时由 20.21 秒降至 4.19 秒）。
- clang-format 19 检查 328 份引擎 C++ 文件；严格 OpenSpec 校验通过。

## 回归覆盖

Debug、Release 的 107 项测试均已执行。首次全套回归发现托管编辑器工作流中的忙碌状态阻塞；修正后单独复测了工作流、后台刷新和 Play 克隆：

- Release 托管工作流 41.91 秒通过，涵盖编译失败、恢复、源码重载、未保存字段、三轮 Run/Stop 和保存。
- Debug 修正复测 6/6 通过，其中托管工作流 80.32 秒、后台刷新窗口 66.00 秒、Play 会话 1.60 秒。
- 全套其余测试通过，涵盖 RHI GPU、纹理/Shader/离屏、VFS/包校验、资产数据库、FBX/OBJ、蒙皮动画、序列化与 Prefab 引用、C# 生命周期/扩展和场景运行。
- Debug 渲染测试没有 Vulkan 验证错误。测试主动制造的 Shader/C# 错误有明确源路径诊断，并验证修复后的恢复。

CLI 端到端用例通过：首次与重复导入、指定范围/强制导入、错误退出码、同项目写锁、缺失输出恢复、源过时拒绝、Player 不写 Library、确定性 pak、导入失败保留原包。

## 实际运行

- 真实 Demo：Debug Editor 打开 Snake、Debug 贪吃蛇 Player（含缩放/最小化压力步骤）、材质 Player 正常运行退出；Release 贪吃蛇 Player 正常运行。
- 同一个 Demo 先准备 Debug 再准备 Release，ShaderCompileCache 文件集合不增加；两份配置使用各自的框架引导及运行目录。项目源与已有 `.meta` 未修改。
- 删除 Demo Library 中单个 ImGui 产物后启动 Editor，产物自动恢复并正常退出。
- 没有 Assets/Library 的 pak 预览项目成功启动 Editor；仅在项目 Library 准备 EditorBootstrap，pak 校验和不变。
- 将 Player、nethost 和 Content.pak 复制到独立部署目录，清空 Vulkan SDK 环境后运行 20 帧并执行窗口压力步骤成功；部署不包含源、Library 或引擎 Build 资源目录。
- 未准备项目的 Player 拒绝启动，提示 `UrsaAssetImport --project <project>`，没有创建 Library。
- 冷 Library、清除 Library、两个项目隔离、外部源新增、导入期间输入变化、失败重试由隔离项目与窗口用例覆盖。

## 路径与清理

所有资源产物、Shader 缓存和受控导入暂存均归属相应项目 Library。`Engine/cache` 与 `Build/Resources` 没有重新创建。原镜像保存在忽略目录 `Build/LegacyResources`，迁移没有删除旧数据。

本次启动的 Editor、Player 和窗口测试进程均正常退出。构建及测试日志保留在忽略目录 Build，名称以 `AssetPipeline` 开头。本变更尚未提交或归档。
