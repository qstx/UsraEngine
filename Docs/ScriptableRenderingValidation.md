# Scriptable Rendering 实施验证记录

正式帧时、逐区 CPU/GPU 样本、硬件及基线边界见 [性能对比报告](ScriptableRenderingPerformance.md)。每轮的原始 Trace 与完整 JSON 样本留在忽略目录 `Build/SrpPerformanceEvidence/Formal`；独立 Memory/Diagnostics 样本位于 `Build/SrpPerformanceEvidence/Diagnostics-SRP-Snake`。报告将 MaterialShowcase P95 上升、Vulkan GPU 采样范围和旧图片基线的未提交代码差异列为限制。

## 构建与 Player 系统验收（2026-10-15）

Windows x64 / VS 2022 下，开启 .NET 的 Debug、Release 全量目标均构建通过；`Build/SrpNativeConfiguration` 关闭 .NET 的 `UrsaEngine` 原生库在两个配置下也构建通过。`URSA_WARNINGS_AS_ERRORS=ON`，未新增编译警告。clang-format 19 检查 431 个 first-party C++ 文件通过。

原有 135 项 Release 与 135 项 Debug 测试均已在首轮或修复复跑中逐项通过。完整首轮保留失败记录，不将它改写为全绿：Release 日志为 `Build/Temp/SrpReleaseTests.log`，Debug 为 `Build/Temp/SrpDebugTests.log`；修复复跑在 `SrpReleaseRetest.log`、`SrpReleaseRemainingTests.log`、`SrpCliFinal.log`、`SrpDebugWindowRetest.log`。最后的命令、快照、原生应用和 Shader Debug 核心组 5/5 通过，日志为 `SrpFinalCoreTests.log`。新增 Editor 控件验收另有上文证据。

除了 SRP 迁移错误，完整测试还修复了 Library 重建夹具遗漏 `.compute`、CLI 测试把预期 stderr 当 PowerShell 异常、哈希命令在受限模块环境不可用，以及 VS 源文件检查误把工具包 Python 头文件列为引擎代码的问题。VFS 的符号链接夹具与原子布局保存使用普通 Windows 权限复跑；临时文件统一放在 Build。没有为通过测试而跳过这些校验。

实际 Player 覆盖包括 Snake 的脚本逻辑与自动玩法、默认材质场景、自定义离屏合成和 compute 输出，松散内容与 Pak 加载，窗口 resize/minimize/restore 压力及正常关闭。`UrsaAssetImportCliTests` 覆盖缺失/过期内容初始化拒绝、只读 Player、丢失产物恢复与确定性 Pak；`UrsaApplicationSceneTests` 覆盖缺少托管管线能力时的初始化清理；帧队列、命令执行与 RHI 测试验证 Drain 后资源释放、在途资源保留与完成后回落。Debug Player/窗口退出未报告 Vulkan 验证错误。

本轮项目 Shader/compute 的实际像素证据在 `Build/TestProjects/PipelineSamples/Debug/37f38875-8df6-4923-8df1-1572908f6c6a/`，包含 Default、Composite、Compute、ComputePak 及真实 Editor 图像和 `Execution.log`。原 Demo 不被测试脚本改写。

## Editor 控件验收（2026-10-15）

`UrsaEditorSrpAcceptanceTests` 在独立目录复制 Demo Assets，额外编译测试用 C# 管线，运行真实 `EditorApplication` 窗口循环，通过 ImGui 导航激活实际菜单、字段、保存按钮和 Graphics 选择器。测试不直接写入资产字段或 Graphics 配置来冒充界面操作。创建后断言默认字段为 false，点击复选框后断言源文件仍为 false，点击 `Save ScriptableObject` 后断言源文件为 true；随后选择并应用刚创建的管线。

Release 完整交互证据位于 `Build/TestProjects/SrpAcceptance/Release/ba5e0a5c-d343-414b-833b-9ee4ce54b77d/`：

- `01Created.png`、`02Saved.png`：新资产、独立 GUID、字段 Inspector 与保存后的勾选状态。
- `03Scene.png`、`04Preview.png`、`05Game0.png`：SceneView 为绿色，Preview 为粉色，Game 为蓝色；Game 图片明确选择 Game 标签，并同时保留粉色材质预览。窗口分别绑定自己的相机类型与目标。
- `05Game1.png`、`05Game2.png`：另两轮 Run；每次 Stop 后都断言未保存 Scene 及临时 marker 仍存在。
- `06ReloadedScene.png`：Stop 修改测试脚本并重新编译后，已保存字段和项目选择保留，窗口缩放、最小化与恢复后继续输出。
- `07DefaultForward.png`：通过同一 Graphics 控件恢复 Default Forward。最后仅保存独立测试项目中的 Scene，正常关闭窗口；原 Demo Assets 不被改写。

测试会临时增大 Project 面板以显示实际文件行。截图逐张复核，修正了仅聚焦隐藏 Game 窗口而未选择其标签的测试问题。完整操作过程可通过 `ctest --test-dir Engine/Build -C Release -R '^UrsaEditorSrpAcceptanceTests$' --verbose` 复跑并记录。

Debug 验证还发现并修复新视口目标在首次管线成功前被 GUI 采样的问题：目标创建时使用同一命令服务清除并转为 ShaderRead 后才注册 ImGui 纹理。管线恢复成功后清除对应 Scene/Game 渲染诊断，保留其他 Inspector 错误。

任务 10.5 清理核验：性能脚本启动的 12 个 Player PID 均已退出；捕获进程逐轮在 `finally` 中关闭 Tracy Worker；本轮没有启动独立 Tracy MCP HTTP 服务。结束时 TCP 8086（Tracy Player）和 47380（Tracy MCP）都可本地绑定。Release 与 Debug Build/ctest、捕获辅助脚本和测试证据均位于被忽略的 `Engine/Build/`；Demo 的流水线示例、Shader 与计算源是该变更预期交付内容，系统验收只向 Build 下的项目副本写入。本轮没有写回源 Demo 场景或 `.meta`。对旧生产类型及旧主调用的 first-party 搜索为空，CMake filters 和严格 OpenSpec 校验通过。

Debug 完整套件原有 135 项中，132 项首轮通过；上述目标初始化修复后，`UrsaMeshEditorWindowTests`、`UrsaManagedEditorWorkflowTests`、`UrsaProfilerEditorWindowTests` 全部复跑通过，新增 `UrsaEditorSrpAcceptanceTests` 也通过。窗口复跑组含两个内容 fixture，共 6/6，耗时 260.71 秒，完整日志为 `Build/Temp/SrpDebugWindowRetest.log`，未报告 Vulkan 验证错误。Debug 控件截图在 `Build/TestProjects/SrpAcceptance/Debug/58b38c91-ce44-4857-85db-9ab551b59c1f/`。`UrsaEditorGizmoInteractionTests` 与 `UrsaEditorGizmoTests` 在同轮完整套件通过，覆盖变换、Gizmo 操作和未保存编辑；窗口控件测试另外断言三轮 Run/Stop 均保留 Dirty 文档及原编辑对象。

## 2026-10-15：旧渲染入口清理

删除生产模块 `Engine/Renderer3D`，将唯一的绘制编码实现移入 `Rendering/Commands/SceneDrawEncoder`。它只处理已选择 Pass 的场景记录、GPU 缓存和编码；窗口获取、提交、呈现、目标状态、纹理上传及帧槽生命周期统一归属 `RenderCommandExecutor`。删除旧单帧栅栏等待、隐式 Forward Pass 选择/排序、内建立方体与重复纹理/骨骼上传。内置 Shader 的 `ursa:Shaders/Renderer3D/` 路径保留。

`SceneRenderData.objects` 使用 `RenderObject`，单相机兼容采集复用 `RenderScene`，删除重复场景遍历。原 GPU 回归移至 `Tests/Engine/Rendering/Regression`，测试用 `RenderingFixtures` 构建显式命令并调用 SRP 执行器；不再链接旧渲染入口。无绘制帧也会清理失效 Mesh 缓存。自定义骨骼测试遵循 `_UrsaBones` 系统绑定命名。

本轮 Windows x64 Release 全量构建和关闭 .NET 的 `SrpNativeConfiguration` 原生库构建通过。已通过模型矩阵、深度、材质、透明、子网格/动态 Mesh、Shader 多 Pass/UsePass/fallback/骨骼、Editor 合成等定向回归。首次失败后修复了资源路径、灯光容量诊断、空帧缓存清理和测试骨骼绑定，相关测试复跑通过。`rg` 确认生产及测试代码不存在旧类型、头文件和主入口调用；生成的 VS filters 将编码器置于 `Rendering/Commands`。

复跑命令：

```powershell
cmake --build Engine/Build --config Release --parallel 8
cmake --build Engine/Build/SrpNativeConfiguration --config Release --target UrsaEngine --parallel 8
ctest --test-dir Engine/Build -C Release -R '^Ursa(RendererTests|MaterialRendererTests|MeshRendererTests|MeshComponentTests|RenderSceneTests|LightingTests|EditorCompositeWindowTests|ShaderPackageTests|ShaderExampleImportTests)$' --output-on-failure
```

Debug 全量构建及 Release 全量测试正在继续；Editor 交互、Player 系统验收、固定条件的性能对比与最终进程清理仍单独跟踪。

## 2026-10-14：Demo 离屏与计算样例

`Tests/Integration/ProjectPipelineSamplesTests.ps1` 在独立测试项目中复制 Demo Assets，以公开 C# API 和发布内容验收真实 Player、Editor，不改写正在编辑的 Demo 项目或已有资源 GUID。

已通过的实际流程：默认 Forward → 项目 Showcase 管线 → Editor Play → ComputeShowcase 管线 → Pak Player。每个相机只剔除一次，同一结果生成不透明、透明列表，按顺序写入带深度的离屏颜色，再由项目 Composite Shader 全屏合成。输出保留 Checker、Ceramic、Gold 和地板；合成图像上下方向与默认图像一致。

Release 验收输出位于 `Build/TestProjects/PipelineSamples/Release/13600d82-9b64-4194-94a9-1523df535a12/`：

- `Default.png`、`Composite.png`：1024×720。绿色边框 RGB = `(25, 204, 76)`；三个材质、地板和顶面的取样满足 R×0.9、G×1、B×0.85，容差为 2 个 8 位灰阶。
- `Editor.png`：真实 Editor Play 的 Scene 视口显示三个材质方块、地板和相同绿色边框。
- `Compute.png`、`ComputePak.png`：不透明阶段使用 Ceramic 替换材质，`GeneratePattern` 生成纹理后由合成 Shader 采样；松散与 Pak 的对应像素一致。
- `Execution.log`：各次导入、启动、捕获和内容打包的输出。

类型目录包含 `demo.showcase-pipeline` 和 `demo.multi-camera-showcase`，管线的脚本身份与 `.ursaasset` 元数据 GUID 分别保存。`Pattern.compute` 的内核按反射线程组计算 Dispatch 数量。新增内容只含 C#、Shader、计算源、资产和说明，Demo 无新增 C++。

可复跑集成测试：

```powershell
ctest --test-dir Engine/Build -C Release -R '^UrsaProjectPipelineSamplesTests$' --output-on-failure
```

本轮另外通过 `UrsaScriptBuildTests`、`UrsaScriptableAssetSourceTests`、`UrsaAssetImportPipelineTests`、`UrsaShaderExampleImportTests`、`UrsaManagedSceneApiTests`、`UrsaManagedEditorWorkflowTests`。这些检查覆盖可空材质字段、持久材质/网格绑定依赖、项目 `.compute` 的松散/Pak 发布、Shader 标签迁移、会话 API 与 Run/Stop。

迁移回归组的 10 项（包含两个内容准备 fixture）全部通过：Snake 逻辑、项目脚本编译、Snake Player、Snake 自动玩法、MaterialShowcase Player、Scene/Prefab 往返、Play 会话及 Editor 扩展。现有 Demo 的 Scene、Prefab、Ceramic/Gold/Checker 及其 `.meta` 无新增差异，原 GUID 和材质引用保留。

实际样例纠正了此前仅靠小型测试未覆盖的问题：内置 Shader 的旧 LightMode 导致 Unlit 几何漏绘；临时视口 API 导致跨预览材质和 RenderTexture 句柄失效；Play 管线配置早于 Scene API 绑定；发布过滤器遗漏 `.compute`。现已以真实项目内容补充验证。

## 窗口画面获取

运行中的 Editor/Player 现可由外部调用 `UrsaCapture --pid <pid> --output <output.png> [--timeout-ms N]`，
获取主线程接收请求后完成的下一幅完整客户区画面；成功后返回含实例、请求、尺寸和时间的 JSON。
同一实例可重复调用，无需预先指定帧号，具体状态及控制目录见 [FrameCapture.md](FrameCapture.md)。

原有命令行 `--capture <output.png> --capture-frame N` 配合 `--frames` 继续用于固定时机的基线。
Scene/Game/Material Preview 可用 `--view` 选择；管线资产可用 `--asset <path.ursaasset> --view Inspector` 打开。
按帧 Inspector 基线仍需给后台脚本目录生成留出帧数；此前已验证 `--frames 260 --capture-frame 250`。

2026-10-18 的按需验收在真实 Editor Pause 中从后台进程连续启动两次 UrsaCapture。
`Build/TestProjects/SrpAcceptance/Release/211f5562-2d68-40b8-a817-435b72a59bb5/08OnDemandPause.png`
和 `09OnDemandRepeat.png` 均为 1100×760，回执中的 FrameSerial 分别为 15845 和 15971，
SessionId 相同、RequestId 不同。原生 Update 计数、选择与未保存 Dirty 状态在两次捕获间保持不变。
对应 `.result.json` 与图片同目录保存；固定帧的 01–07 图片和原三轮 Run/Stop 验收继续通过。

本次 GPU 用例复用 `UrsaFrameCaptureTests`，同时覆盖 RGBA/BGRA、改变后的真实像素、一次读回复用、
写入失败后旧 PNG 保留、后续恢复及过期请求无读回。无 GPU 的协议、Busy、超时、关闭、PID 复用和坏消息
集中在一个 `UrsaWindowCaptureTests` 目标，另验证 UTF-8 CLI 与登记文件被读取期间的 ready 发布重试。

实际 Player/Pak/Editor 调用的独立证据位于
`Build/TestProjects/PipelineSamples/Release/14d67f27-d347-4eda-8c52-966f0273c91a/`：
`ProbeAfterFix-*` 验证多实例、写入失败恢复、最小化/恢复和退出；`ProbePak-*`、`ProbeEditor-*` 验证重复调用。
本机高 DPI 环境下，外部调整窗口后 `ProbeAfterFix-Resized.png` 的实际 framebuffer 为 1774×1129，
回执与独立 PNG 解码尺寸一致；没有把 900×600 的窗口调整参数当作图像像素尺寸。

完整最小化检查还暴露了 Player 的既有尺寸竞态：主循环检查之后 framebuffer 仍可能变为零，
导致向托管渲染传入无效 aspect。已在 Player 渲染入口采样并校验尺寸，并只把明确的交换链暂挂起
作为跳过呈现处理，其它渲染错误继续传播。`ProbeRaceFixed-*`（Release）和
`Build/TestProjects/PipelineSamples/Debug/eb74624b-441b-4d65-b8da-954b2f49ae5b/ProbeDebugRaceFixed-*`
分别验证连续三轮最小化超时和恢复后截图成功，随后验证目标退出不复用遗留结果、另一实例仍可截图。

Debug 的完整 Editor 验收通过（含准备共 324.20 秒）。截图验证合入原三轮 Run/Stop 的第一轮，
`Build/TestProjects/SrpAcceptance/Debug/b901ceaf-6114-443e-b41b-2a83304bbda7/08OnDemandPause.png`
和 `09OnDemandRepeat.png` 为 1280×720，FrameSerial 分别为 20415、20513。
两次外部 CLI 完成期间暂停计数、选择和 Dirty 不变，点击 Resume 后原生 Update 再次推进。
CPU 快速回归 60/60 通过；无 Editor/.NET 的独立 UrsaCapture 和聚合协议回归通过；
491 个 C++ 文件 clang-format 19 检查通过。

最终完整重跑：Release 的 Editor SRP 与样例管线集成组（含准备）3/3 通过，
其中 SRP 为 152.53 秒、样例管线为 120.35 秒；Debug 样例管线组（含准备）2/2 通过，为 147.00 秒。
样例脚本真实运行松散 Player、Pak Player、普通 Editor，对每个实例重复请求、核对 PNG 尺寸、
制造写入失败并恢复；松散 Player 另验证两个实例、实际缩放、连续三轮最小化/恢复及目标退出。
最终样例证据分别位于 `Build/TestProjects/PipelineSamples/Release/2b87d4b9-48b7-43fc-9477-68485dcceb3c/`
和 `Build/TestProjects/PipelineSamples/Debug/48640b4d-7926-4661-878a-86f5dcea0313/`。

验证时 Engine 基线为 `a1fee715e8405b66158643787e78a2575dce1959`，实现位于本次工作树；
主仓库基线为 `eb72a147a98a72dde90591f962b18e9576527cff`，Engine 引用尚待后续提交时更新。
`Build/OnDemandEvidence.json` 保存源码及 Debug/Release 二进制 SHA-256、截图回执和验证日志索引。
范围为正常运行的主窗口客户区；调试器挂起时获取历史画面、桌面标题栏/鼠标和持续录像不在本轮范围。

`Build/Temp/SrpCreatedAssetInspectorFocused.png` 是命令行捕获的实际 Editor 界面，显示通过创建菜单生成的资产及其材质/计算引用、浮点和 tint 字段。此前 Inspector 错将资产 GUID 与脚本 GUID 比较，现以 payload 中的脚本身份验证类型。`UrsaScriptableAssetInspectorTests` 用真实 ImGui 控件导航验证字段 Dirty/保存及未知字段保留，避免仅以文件往返代替界面行为。

创建菜单、字段保存、Graphics 选择及三个预览的完整界面交互证据，以及 Snake/MaterialShowcase 松散/Pak Player 系统验收，已在本报告前文记录，OpenSpec 任务 8.6/10.2/10.3 已完成。
