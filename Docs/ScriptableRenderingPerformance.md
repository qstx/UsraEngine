# Scriptable Render Pipeline 性能对比

执行于 2026-10-15，原始数据在 `Engine/Build/SrpPerformanceEvidence/`。正式时序矩阵包含 12 个独立 Player 进程：Snake 与 MaterialShowcase 各运行旧 `Renderer3D` 和新 SRP，每种组合各 3 轮。每轮最多 1320 个完整 `Engine.Frame`，排除前 120 个预热帧，精确保留之后连续 900 个；每个 `Samples.json` 都记录实际完整帧数、窗口、逐帧区间、可执行文件 SHA-256、Tracy 文件 SHA-256、GPU 设备、运行参数及 CPU/GPU 区域。原始 `.tracy` 与逐事件纳秒时间戳随样本 JSON 保存。正式轮次没有截图、Pak 重建或并发工作负载。

| 场景 | 旧管线帧间隔中位数 / P95 | SRP 帧间隔中位数 / P95 | 中位数变化 | P95 变化 |
| --- | ---: | ---: | ---: | ---: |
| Snake | 19.967 / 26.268 ms | 5.889 / 9.502 ms | −70.5% | −63.8% |
| MaterialShowcase | 6.061 / 7.974 ms | 6.066 / 12.249 ms | +0.08% | +53.6% |

数值合并 3 轮、每配置 2700 个完整帧，P95 用线性插值计算。Snake 的 `Renderer3D.Record.BuildPasses` 中位数/P95 为 12.770/17.568 ms，`SceneDrawEncoder.Record.BuildPasses` 为 2.334/3.728 ms；旧 `WaitPrevious.Fence` 为 4.875/8.366 ms，新 `Rendering.FrameSlotWait` 为 1.663/5.267 ms。两者 `BuildBindGroups` 中位数均约 1.73 ms，所以 Snake 的改善主要来自消除每帧重建绘制 Pass 和全局前帧等待。

MaterialShowcase 的两个管线帧间隔中位数约 6.06 ms。旧等待栅栏区域为 5.312/7.053 ms，新帧槽等待为 5.528/11.659 ms；SRP 的 P95 回归与较长帧槽等待同时出现。这个小场景没有证明端到端帧时改善，结果保留为明确的尾延迟风险。即使新管线将 `SceneDrawEncoder.Record.BuildPasses` 从 0.476/0.999 ms 降到 0.079/0.179 ms，窗口提交与同步仍主导总帧间隔。

GPU 数据只覆盖 Tracy 标记的 `Vulkan.DrawIndexed`。旧与新管线绘制事件中位数都是 1.024 μs，时间戳分辨率为 1024 ns；MaterialShowcase SRP 的事件 P95 为 107.52 μs。GPU 队列异步于 CPU 帧，窗口区间内事件数也不等于 CPU 绘制数。图形区域未覆盖屏障、复制、呈现和整个 GPU 帧，不能用来声称 GPU 整帧变快；汇总和每个事件时间戳保留在 `Samples.json`。

**条件与范围：** 两组程序均使用 Windows x64、VS 2022 / MSVC 19.38、Release、Vulkan 1.2、.NET 开启、Tracy 开启，Memory/Diagnostics 关闭、调用栈深度为 0；1024×720 隐藏窗口、交换链 VSync/FIFO 开启，材料场景使用同一 Camera、源场景与导入缓存。驱动启动后和采样运行后检查为同一台 RTX 4060 Laptop GPU / 591.86；测试机当时使用 Windows“安静模式”，GPU 在采样后检查为 P3、48°C、显示活动为 Disabled。捕获程序记录固定 Build 配置和各二进制 SHA-256。隐藏窗口不等同于显示器前景交互，它不保证显卡持续运行于最高性能状态；这些运行条件下的帧间隔反映本次机器和配置。

旧端从 Engine commit `9b2baf25dc0e5ecc52a9a1ea05e00b367e1158e8` 和 Demo 资源 commit `d0a382277d218af45f0da4a8725cedf94f351930` 重建，新端来自当前 Engine 工作树和 Demo 资源。任务 1.1 的截图基线记录了同一 Engine HEAD 下尚未提交的基础 RHI/场景快照改动，因此旧 commit 重建是可追溯的旧 `Renderer3D` 实现，不能声称与 1.1 图片逐位对应。相关实现可能使数值基线存在偏差。旧端与新端的编辑器、脚本运行时及 Player framework 二进制也各自重建；正式样本明确记录其文件摘要。

Snake 是实时脚本场景，物体数随实际帧时间和游戏逻辑变化。本次 CPU `BuildBindGroups` 样本计数在两版本间一致（每场景 3 轮各 234900 次），而 Tracy GPU 事件计数分别为旧端 691418、新端 637561；异步 GPU 区间和运行时场景演进不提供按逻辑绘制一一配对的证据。MaterialShowcase GPU 事件数为旧端 10385、新端 10212。比较应结合 CPU 区域、事件计数和逐帧分布阅读，事件数不能替代逐对象图片对比。

独立内存/Diagnostics 轮使用相同 Release 配置，另外开启 Tracy Memory 与 Diagnostics、调用栈深度为 0，运行 Snake 360 帧，其中 120 帧预热、180 帧用于该独立诊断。结果保存于 `Engine/Build/SrpPerformanceEvidence/Diagnostics-SRP-Snake/`，没有并入时序矩阵。180 帧区间内没有观察到帧槽等待，360 帧运行期间两帧槽共建立 2 个上传缓冲，总容量最高 2 MiB，最终已上传字节累计最高约 14.3 MiB。Pass 解析、Shader 反射、图形 PSO 和 Mesh GPU 上传在此场景各记录 1 次，增长没有按逐物体绘制扩张；新建材质绑定组没有被此系统 Shader 场景触发。VMA 的 GPU Reserved 为 70.25 MB，Used 峰值约 5.25 MB。退出后所有采样到的 Tracy CPU/VMA Memory Pool 当前 usage 都为 0；CPU 容器类别记录的运行峰值约 17.82 MB，CPU Engine Heap Reserved 峰值约 4.00 MB。这些值是该 Snake 场景和这次进程周期的上界及退出读数，不代表整个编辑器、托管堆或所有资产的内存预算。

完整的基线与 SRP 原始证据，以及独立诊断样本，保留于 `Engine/Build/SrpPerformanceEvidence/`。试采 `Pilot1` 和 `Pilot2` 因帧时间戳解码/Windows 主机文本编码检查失败而没有有效报告，不纳入以上数据；修复后通过的 `Pilot3`、旧提交 `PilotLegacy1` 只用于验证导出链路，同样排除正式汇总。可用以下命令串行重跑 12 轮，并用生成的输入路径、文件摘要和原始 capture 复核汇总：

```powershell
Engine/Tools/RunSrpPerformanceMatrix.ps1
```

内存诊断单独运行；将 `--player`、`--project` 改为旧端程序和项目即可采集旧端，保持 Diagnostics/Memory 开关一致：

```powershell
Engine/Tools/Python/3.11.17/python.exe Engine/Tools/CaptureRenderingPerformance.py --player Engine/Build/Release/UrsaScriptPlayer.exe --project Engine/Build/TestProjects/Sample/Release --scene Snake/Snake.ursascene --frames 360 --warmup 120 --samples 180 --output Engine/Build/SrpPerformanceEvidence/Diagnostics-SRP-Snake
```
