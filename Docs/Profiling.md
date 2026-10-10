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

采样会增加 CPU/GPU 开销，因此应只在排查性能问题时启用，并在 Debug 或 Release 的目标配置下分别构建后采集。
