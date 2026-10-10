# Tracy 性能分析

UrsaEngine 使用 Tracy 采集 CPU 帧阶段和 Vulkan GPU 区段。Tracy 是可选依赖，默认构建不会链接 Tracy client。Profiler 图形界面由 Tracy 项目单独提供，不随 UrsaEditor 或游戏发布。

## 启用

先初始化 Tracy 子模块：

```powershell
git submodule update --init third_party/tracy
```

然后在 CMake 配置时启用：

```powershell
cmake -S Engine -B Engine/Build -DURSA_ENABLE_TRACY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build Engine/Build --config Debug --parallel
```

Release 配置使用相同选项，将 `Debug` 改为 `Release`。如果使用 Visual Studio 生成器，通过 `--config` 选择构建配置即可。

Ursa 默认启用 Tracy 的 on-demand 采样。应用启动后不会持续记录完整 capture；连接 Profiler 并开始录制后才会采集，停止录制即可结束本次采样。普通构建可省略 `URSA_ENABLE_TRACY` 或显式设为 `OFF`。

## 连接 Profiler

从 Tracy `v0.14.1` 下载或构建独立的 Tracy Profiler，并确保 Profiler 和 Engine 使用同一版本。先启动 `UrsaEditor`、`UrsaDemo` 或 `UrsaScriptPlayer`，再在 Tracy Profiler 中连接该进程并开始 capture。默认连接使用本机网络发现；如需跨机器采样，请按 Tracy Profiler 的连接设置配置网络。

CPU 时间线包含 Engine/Editor 帧、场景更新阶段、Renderer3D 录制阶段以及原生到 C# 的生命周期回调边界。Vulkan GPU 时间线标记渲染和计算命令；如果所选队列不支持 Vulkan 时间戳，GPU 区段会跳过，CPU 采样仍可使用。托管回调按脚本类型和回调阶段命名；用户 C# 方法体内部没有单独插桩。

每个完成的主循环帧发送一个默认 `FrameMark`，并保留 `Engine` 或 `Editor` 命名帧标记。默认帧序列供 Profiler 初始 FPS/帧时间显示及客户端全局帧计数使用，命名序列用于区分运行入口。标记位于帧工作完成后的边界；窗口最小化或帧失败退出时不计作完成帧，Editor 未取得交换链图像的分支也不计帧。Editor 的 FPS 是编辑器整帧频率，包含 UI、游戏预览、呈现及等待时间，不能直接视为 Play 模式的逻辑更新频率。

如果 CPU zones 可见但默认 FPS 没有数据，检查是否只有 `FrameMarkNamed` 而没有默认 `FrameMark`，并确认正在采样的应用仍持续完成帧。修改帧标记后需要重新构建并启动 Engine/Editor 客户端；已有 capture 不会自动增加缺失的默认帧数据。

2026-10-10 回归验证：Debug Editor 的 3 秒 capture 包含 303 个默认帧条目和 300 个 Editor 条目，最近完整帧约 8.47 ms；Release 贪吃蛇 Player 的 3 秒 capture 包含 138 个默认帧条目和 135 个 Engine 条目，最近完整帧约 47.66 ms。按 Profiler 相同的帧时间读取方式，默认与命名序列均能计算有限、正值 FPS。条目数包含 Tracy 初始化数据，以上仅验证帧标记和 FPS 可用性，不是两种运行入口的性能比较。Debug/Release 的 facade、帧循环、Editor 窗口及 Demo 冒烟测试均通过。

采样会增加 CPU/GPU 开销，因此应只在排查性能问题时启用，并在 Debug 或 Release 的目标配置下分别构建后采集。
