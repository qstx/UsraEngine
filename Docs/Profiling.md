# Tracy 性能分析

UrsaEngine 使用 Tracy 采集 CPU 帧阶段和 Vulkan GPU 区段。Debug 和 Release 默认包含基础采样，可用 `URSA_ENABLE_TRACY=OFF` 显式关闭；Publish 使用优化代码且不链接 Tracy client。Profiler 图形界面由 Tracy 项目单独提供，不随 UrsaEditor 或游戏发布。

## 启用

先初始化 Tracy 子模块：

```powershell
git submodule update --init ThirdParty/tracy
```

然后在 CMake 配置时启用：

```powershell
cmake -S Engine -B Engine/Build -DURSA_ENABLE_TRACY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build Engine/Build --config Debug --parallel
```

Release 配置使用相同选项，将 `Debug` 改为 `Release`。Publish 是独立的正式发布配置，编译和链接优化继承 Release，强制移除采样。Visual Studio 生成器通过 `--config Debug|Release|Publish` 选择配置。完整说明见 [构建配置](BuildConfigurations.md)。

Ursa 默认启用 Tracy 的 on-demand 采样。应用启动后不会持续记录完整 capture；连接 Profiler 并开始录制后才会采集，停止录制即可结束本次采样。显式关闭基础采样时使用 `URSA_ENABLE_TRACY=OFF`。同一生成目录的 Publish 始终不链接 Tracy，即使 Debug/Release 已开启。

## 连接 Profiler

### 内存和诊断扩展

基础 `URSA_ENABLE_TRACY` 只开启已有 CPU/GPU 时间采样。另有两个默认 OFF 的独立开关：

| 选项 | 数据及成本 |
|---|---|
| `URSA_ENABLE_TRACY_MEMORY` | 每个 Ursa CPU 逻辑块与 VMA 设备块的分配/释放事件；连接采集时增加事件同步与传输 |
| `URSA_ENABLE_TRACY_DIAGNOSTICS` | 内存曲线、应用信息、操作标记和两个关键 mutex 的等待/持有事件 |
| `URSA_TRACY_MEMORY_CALLSTACK_DEPTH` | 原生分配调用栈深度，整数 0–64，默认 0；非零有额外开销并依赖调试符号 |

两个扩展开关都要求 `URSA_ENABLE_TRACY=ON`。只启用 diagnostics 可以观察绝对内存用量而不记录逐块事件。

```powershell
cmake -S Engine -B Engine/Build -DURSA_ENABLE_TRACY=ON -DURSA_ENABLE_TRACY_MEMORY=ON -DURSA_ENABLE_TRACY_DIAGNOSTICS=ON -DURSA_TRACY_MEMORY_CALLSTACK_DEPTH=0
cmake --build Engine/Build --config Debug --parallel
```

需要分配调用点时将深度设为 8 并重新构建。`URSA_ENABLE_TRACY=OFF` 时同时关闭两个扩展，Runtime 不链接 Tracy client；profiling 宏不执行采样参数，锁仍承担原有同步。此功能不追踪 .NET GC 对象或所有第三方 CPU 分配，也不全局替换 `new/delete`。

### 采集

从 Tracy `v0.14.1` 下载或构建独立的 Tracy Profiler，并确保 Profiler 和 Engine 使用同一版本。先用 `UrsaEditor --project Demo` 或 `UrsaScriptPlayer --project Demo --scene Snake/Snake.ursascene` 启动应用，再在 Tracy Profiler 中连接该进程并开始 capture。默认连接使用本机网络发现；如需跨机器采样，请按 Tracy Profiler 的连接设置配置网络。

CPU 时间线包含 Engine/Editor 帧、场景更新阶段、SceneDrawEncoder 录制阶段以及原生到 C# 的生命周期回调边界。Vulkan GPU 时间线标记渲染和计算命令；如果所选队列不支持 Vulkan 时间戳，GPU 区段会跳过，CPU 采样仍可使用。托管回调按脚本类型和回调阶段命名；用户 C# 方法体内部可用下面的显式 CPU 标记。

### C# 关键方法的 CPU 耗时

使用 `Ursa.Profiling.ProfilerMarker` 声明固定标记，在要测量的同步范围中使用 `Auto()`：

```csharp
using Ursa.Profiling;

private static readonly ProfilerMarker findTargetMarker = new("Player.FindTarget");

private void FindTarget()
{
    using var scope = findTargetMarker.Auto();
    // 目标搜索逻辑；return 和异常展开都会结束采样。
}
```

启用 `URSA_ENABLE_TRACY` 并连接 Tracy 后，该范围与 C++ 标记使用同一 client；在 Update 中调用时嵌套于已有的 `Player::Update` zone。每个 marker 有独立的源码位置和统计身份，构造时自动记录声明处的文件、成员名和行号。计时覆盖 scope 内经过的时间，包括内部调用和等待，不等同于线程实际占用 CPU 的时间。

marker 是值类型，可在宿主初始化前作为静态字段构造，首次使用才注册 native 元数据。请复用固定 marker，避免逐帧创建带对象 ID 等变化名称的 marker：native 元数据按内容去重并保留到进程退出，以便脚本卸载后 Tracy 仍可读取。标记不会保留项目程序集。Publish 或框架编译关闭 profiling 时，构造/Auto/Dispose 是没有采样状态的编译期空实现，不查询绑定或 native 能力。开发框架在绑定前使用标记为空操作，初始化后可正常采样。

`AutoScope` 是 ref struct，须在同一线程、同一脚本绑定上下文中按栈顺序结束；不要复制 scope，不要跨 `await`、`yield` 或会话重载持有它。明确支持跨线程的 profiling 绑定随执行上下文传递，因此 Task/await 的后续同步片段可以创建自己的 scope；这不赋予场景和编辑器 API 跨线程访问权限。重复 Dispose 同一个 scope 为空操作；错误的结束顺序或上下文会在提交错误事件前报告异常。Tracy 断开重连时由完整连接身份过滤旧 scope 的结束事件。

CPU marker 与 `Ursa.Rendering.ProfilingScope` 用途不同：后者在 CommandBuffer 中录制 GPU 调试域。新增 Runtime 绑定后需重建引擎及项目脚本内容，使契约身份一致。

### Deep Profiling

Debug/Release 可显式开启项目方法自动插桩，默认关闭。统一使用某个配置的工具和框架：

```powershell
cmake --build Engine/Build/VS2026 --config Release --target UrsaScriptCompile
Engine/Build/VS2026/Release/UrsaScriptCompile.exe Demo Release --deep-profile
```

也可以在 `UrsaAssetImport` 中使用 `--deep-profile` / `--no-deep-profile`。要让 Editor 的项目构建默认开启，可在 CMake 配置中设置 `URSA_ENABLE_DEEP_PROFILING=ON`；它要求 .NET 和 Tracy 支持。开关变化会使脚本缓存失效。Publish 显式请求 Deep Profiling 会失败。

工具对编译后的项目程序集和 portable PDB 后处理：方法进入时开始范围，正常返回和异常展开时结束；覆盖泛型、递归、属性访问器及编译器生成的方法。实例构造从 this/base 初始化调用之后开始记录。async/iterator 按状态机每次执行的同步片段记录，名称带 `[MoveNext]` 等后缀，不把 await/yield 挂起时间计为 CPU 执行。任务切到后台线程时在该线程建立独立范围。

使用 `Ursa.Profiling.IgnoredByDeepProfiler` 可忽略方法、构造函数或整个类型；被忽略 async/iterator 的生成状态机也不插桩。无方法体的 abstract/extern 不产生范围。系统库和引擎框架不改写；强命名程序集及不支持的 IL 会明确报错。重复插桩、损坏符号或后处理失败不会发布新内容。

Deep Profiling 会增加方法调用、元数据和传输开销，并禁止被插桩方法内联；适合定位调用层级。定位后关闭深度采样，用优化的 Release 与少量手动 marker 复测。插桩工具和 Mono.Cecil 只在构建机器使用，不打包进 Player。

2026-11-04 Windows x64 Debug 的真实宿主采集验证：项目 DLL 自动插桩后，四轮加载/运行/卸载得到 `SessionProbe.Awake()` 4 次、`ComputeAsync() [MoveNext]` 8 次；后者分布在 3 个线程，4 个后台工作 marker 均嵌套在对应线程的 MoveNext 范围内。采集与 CSV 位于忽略目录 `Engine/Build/DeepProfiling/HostCapture/`，仅用于功能验证。

构建测试目标后，可运行 `UrsaCpuProfilerTests`、`UrsaManagedProfilerTests` 和 `UrsaManagedProfilerHostTests`；宿主测试执行四轮真实脚本加载、运行和卸载。需要采集时运行：

```powershell
Engine/Build/VS2026/Debug/UrsaDotNetHostTests.exe Engine/Build/VS2026/Managed/Debug Engine/Build/MarkerValidation/HostCapture --profiling-capture
```

该模式等待 Tracy 连接最多 10 秒。2026-11-04 Windows x64 Debug 实测 capture 中，`SessionProbe.Awake.Work` 和 `SessionProbe.Awake.Counter` 各有 4 次采样，源码映射到 `SessionProbe.cs`；四个子范围均处于同线程的外层范围内，宿主正常退出。Tracy 开启与关闭两种构建的 CPU marker、托管 API、真实宿主及源码符号相关测试各 5 项通过。原生 scope 的两次实际 capture 也验证了断开重连后继续采样。记录的是功能正确性，不是性能基准。

每个完成的主循环帧发送一个默认 `FrameMark`，并保留 `Engine` 或 `Editor` 命名帧标记。默认帧序列供 Profiler 初始 FPS/帧时间显示及客户端全局帧计数使用，命名序列用于区分运行入口。标记位于帧工作完成后的边界；窗口最小化或帧失败退出时不计作完成帧，Editor 未取得交换链图像的分支也不计帧。Editor 的 FPS 是编辑器整帧频率，包含 UI、游戏预览、呈现及等待时间，不能直接视为 Play 模式的逻辑更新频率。

如果 CPU zones 可见但默认 FPS 没有数据，检查是否只有 `FrameMarkNamed` 而没有默认 `FrameMark`，并确认正在采样的应用仍持续完成帧。修改帧标记后需要重新构建并启动 Engine/Editor 客户端；已有 capture 不会自动增加缺失的默认帧数据。

2026-10-10 回归验证：Debug Editor 的 3 秒 capture 包含 303 个默认帧条目和 300 个 Editor 条目，最近完整帧约 8.47 ms；Release 贪吃蛇 Player 的 3 秒 capture 包含 138 个默认帧条目和 135 个 Engine 条目，最近完整帧约 47.66 ms。按 Profiler 相同的帧时间读取方式，默认与命名序列均能计算有限、正值 FPS。条目数包含 Tracy 初始化数据，以上仅验证帧标记和 FPS 可用性，不是两种运行入口的性能比较。Debug/Release 的 facade、帧循环、Editor 窗口及 Demo 冒烟测试均通过。

采样会增加 CPU/GPU 开销，因此应只在排查性能问题时启用，并在 Debug 或 Release 的目标配置下分别构建后采集。

### 数据口径

CPU 详细事件只在统一逻辑分配入口上报一次，池名为 `Ursa.Cpu.<Category>`，大小为调用方请求字节。EASTL 默认 allocator、对象工厂与线性分配都走同一入口；临时作用域按实际逐块回收报告释放，不清空其他作用域共享的类别池。底层堆页没有重复上报为 CPU 逻辑分配。

VMA 设备块池使用 `Ursa.Gpu.DeviceMemory.<Device>`，报告实际设备内存块大小。每设备独立命名，块 identity 为该设备内的不透明 handle，不是可解引用的 CPU 指针。多个资源可共用一个设备块，因此不要把 GPU 池地址地图解释为 CPU 地址布局，也不要把设备块大小与资源子分配用量相加。

on-demand capture 只包含连接期间发生的事件。连接前已存在的块、跨断开区间的释放不能据该文件重建；当前活跃事件字节并非进程总占用，文件中未释放的块也不自动等于泄漏。重新连接开启独立 capture。需要绝对占用时启用 diagnostics 的分类快照曲线。

### 诊断曲线与事件

内置统计只在已连接时按 250 ms 周期读取，分配回调不会读取全分片快照。Editor Pause 时仍采样编辑器占用；最小化暂停绘制时不会为了曲线伪造游戏更新帧，恢复后采样最新状态。

| 名称 | 单位与口径 |
|---|---|
| `Ursa.Cpu.<Category>.Current` / `.Peak` | 请求字节当前值/历史峰值，Bytes |
| `.Live` / `.Allocations` / `.Frees` / `.Overflows` | 存活分配数、累计分配/释放次数和临时溢出次数，Number |
| `Ursa.Cpu.Heap.Reserved` / `.UpstreamAllocations` | 默认持久堆预留字节/累计上游申请次数 |
| `Ursa.Gpu.<Device>.Reserved` / `.Used` | VMA 设备块预留字节/资源子分配用量；不是驱动总预算或整个进程 VRAM |

项目可以通过 `URSA_PROFILE_PLOT(name, value, unit)` 提交自定义曲线，名称必须来自同一份固定存储并有进程生命周期，例如 `static constexpr char name[] = "Project.Counter";`。内存池也按名称地址识别，分配和释放应使用同一变量；不要在不同调用点依赖编译器合并同文本的字面量。单位为 `Ursa::Profiling::PlotUnit` 的 Number、Bytes 或 Percentage。诊断关闭时不求值上报参数。

命名锁 `Ursa.Log.Listeners` 与 `Ursa.Vfs.EngineFiles` 分别表示日志监听列表和内置资源挂载的 mutex。它们记录等待/持有，并不通过日志系统再发出日志。分配器 SpinMutex 和采样自己的同步锁不在首批锁追踪范围内。

应用信息标明入口、版本、revision、构建配置与采样开关；`Asset.Import` 标明虚拟路径及 begin、complete、cache-hit，失败/取消通过 `Asset.Import.failed` 保留原结果。`Editor.Play.Start/Stop` 标明运行会话转换。事件按操作发出，不桥接全部日志，正文限制为 1024 UTF-8 字节并在完整字符边界截断。

### 本次接入的验证

2026-10-11 Debug、调用栈深度 0 的 4 秒 capture：`Ursa.Cpu.ProfilerTest` 有 1,033 次分配、1,033 次释放和 0 存活块，覆盖大小为零、对齐、对象工厂、并发复用、临时恢复与回退。两个 VMA 设备池各记录 1 次设备块分配/释放；每设备 Reserved 为 32 MiB、Used 为 4 MiB，对应四个 1 MiB 缓冲区共用设备块。两个关键锁均出现竞争。晚连接后释放连接前分配，以及断开后释放再重连的两个 capture 都没有 instrumentation failure；重连仍包含应用信息。`ProfilerSession` 保留的 321 字节块属于采集结束时尚存活的测试块，在断开后释放，不作为泄漏结论。

capture 与解析结果保存在忽略的 `Engine/Build/ProfilingCaptures/`，相关逻辑测试为 `UrsaProfilerMemoryTests`、`UrsaProfilerGpuMemoryTests`。需要实际采集时运行 `UrsaProfilerMemoryTests --seconds 16 --gpu`，并在同一版本的 Profiler 或 tracy-capture 中先后连接两次；命令结束会报告成功连接次数。

调用栈深度 8 的 capture 已解析到 `AllocateFromFirstCallsite` 和 `AllocateFromSecondCallsite`，深度 0 的对照没有分配调用栈。两个测试 GPU 池使用相同 identity `0x410`，各有 1 次分配、1 次释放与 0 存活块，验证命名池隔离；两个真实 VMA 池也均配对关闭。UTF-8 边界样本分别是 1023 和 1024 字节，解析后仍是有效文本。

仅 diagnostics 开启的对照 capture 没有逐块内存事件，12 秒窗口获得每项 47 个分类统计点，符合 250 ms 周期；ProfilerTest 的 Current/Live 为 0，Peak 为 9000，累计 Allocations/Frees 均为 1033，Overflows 为 1，与已知工作负载吻合。重连会重新上报当前曲线。

Debug 在真实 Demo 上完成三轮 Run/Pause/Resume/Stop，编辑文档 Dirty 状态保持原样。capture 有 7,740 个 CPU zones、14,669 个 GPU zones；Snake 的 Awake、OnEnable、Start、FixedUpdate、Update、LateUpdate、OnDisable 和 OnDestroy 都可按脚本类型检视。默认/Editor 帧条目分别为 684/681，最近完整帧约 12.18 ms，表明 FPS 数据可用，不用于性能比较。另一次真实最小化/主线程计时器恢复检查记录 `frames-while-minimized=0`，恢复后仍完成三轮会话。

`UrsaProfilerEditorWindowTests <项目目录> [--capture]` 可重现导入/缓存/取消和运行模式检查；`--capture` 会等待 Profiler 连接。测试窗口自动关闭，测试不保存运行场景到源资产。

Release 的实际 Demo Snake/MaterialShowcase Player 均正常退出，capture 的 CPU/GPU zones 分别为 2,803/40,194 和 4,983/2,048。最近完整帧时间约 38.04 ms 和 6.43 ms；这些采集启用了详细追踪与调用栈，不能用作无采样性能结论。Debug/Release 相关 CTest 各 26 项通过，Release 最终 Editor 最小化期间的 GUI 帧数也为 0。

### 分配开销测量

2026-10-11 Windows x64/MSVC Release，同一工作负载分别运行 5 次，以下为每次分配/释放对的耗时中位数（ns）。扩展关闭对照保留基础 Tracy client，但不记录详细内存和 diagnostics；其他配置同时开启两个扩展。测量前结束了所有构建和应用验收进程。

| 状态 | 持久小块 ns/pair | 临时小块 ns/pair |
|---|---:|---:|
| 扩展关闭 | 43.04 | 32.61 |
| 扩展开启、未连接、深度 0 | 47.74 | 37.36 |
| 连接采集、深度 0 | 174.36 | 153.62 |
| 扩展开启、未连接、深度 8 | 49.06 | 40.25 |
| 连接采集、深度 8 | 555.00 | 520.54 |

各轮持久堆热身后的上游申请增量、临时上游申请和溢出均为 0。事件名称与诊断元数据没有进入被追踪的堆路径。详细采样会显著增加分配成本，调用栈尤其明显，因此两个扩展开关默认 OFF、调用栈默认 0；测量属于单线程分配工作负载，不能直接换算为整帧降速比例或多线程扩展性。

`UrsaMemoryBenchmarks --repeat 5` 可重现未连接数据，`--repeat 5 --wait-for-profiler` 等待连接后执行采集中数据。原始日志和两种调用栈 capture 位于 `Engine/Build/ProfilingCaptures/Benchmark-*`。

基础 `tracy-profiling` 的联合 CPU/GPU/C# capture 与中文采样流程现已补充实际证据。2026-10-11 在 Windows x64 Debug 下，将 `URSA_ENABLE_TRACY`、`URSA_ENABLE_TRACY_MEMORY` 和 `URSA_ENABLE_TRACY_DIAGNOSTICS` 全部关闭，保留 .NET 与 Editor 支持，重新构建并通过 `UrsaManagedBehaviourRuntimeTests`、`UrsaDotNetHostSmoke`、`UrsaManagedProtocolTests` 和 `UrsaProjectPlaySessionTests` 及其准备步骤。实际 .NET 宿主验证了生命周期顺序、回调异常隔离和销毁行为，项目运行会话验证了 Run/Stop 与编辑状态恢复；对应 Runtime 和测试工程未引入 Tracy client 链接依赖。
# GPU 抓帧调试域

RenderDoc 标记由 RHI 的 GPU 调试域输出，与 Tracy 性能采样和 Vulkan validation 分开控制。可用 Vulkan `VK_EXT_debug_utils` 时，Debug/Release 均可看到相机与阶段的嵌套事件；扩展或函数不可用时后端跳过标签。

默认 Forward 命令缓冲使用 `Ursa Forward` 名称。在抓帧中相机域按 `Game`、`SceneView` 或 `Preview` 类型，加场景对象名与持久 identity 标识；无显示名时回退 identity。相机下可见实际发生的 `Clear`、`Draw Opaque` 与 `Draw Transparent`。Editor UI 使用独立域。CPU 剔除不作为 GPU 域显示。

C# 自定义管线可用 `CommandBuffer.BeginSample(name)` / `EndSample(name)` 或缓存 `ProfilingSampler` 并用 `ProfilingScope`：

```csharp
private static readonly ProfilingSampler opaque = new("Draw Opaque");

using (new ProfilingScope(commands, opaque))
    commands.DrawRendererList(opaqueList, context);
```

每个样本须在同一命令缓冲的单次 Execute 中按栈顺序配对。首版的 `ProfilingSampler` 和 `ProfilingScope` 只生成 GPU 调试域，不提供耗时统计、CPU-only 采样或跨 Execute 配对。命令缓冲 Name 在交付时复制；改名和 Clear 不改变已提交段的名称。
