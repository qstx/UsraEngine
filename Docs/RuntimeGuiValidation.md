# OnGUI 与 DemoSuite 验收记录

本记录对应 `runtime-ongui` 和 `demo-feature-test-suite`。验收日期为 2026-11-06，环境为 Windows、Vulkan、NVIDIA GeForce RTX 4060 Laptop GPU、Visual Studio 2026、.NET 10、CMake 4.4.5。测试中注入 Window 订阅事件，不等于人工鼠标、输入法或系统 DPI 验收。

## 已取得的证据

| 验收入口 | 实际证据与边界 |
| --- | --- |
| Debug 核心回归 | `GuiDebugFinalCore.log` 发现并执行 17 项，17 项通过，覆盖 Window、FrameScheduler、SceneBuildSettings、RuntimeSceneManager、GUI、句柄/Behaviour、PlaySession、Viewport、ImGuiBridge、执行器和窗口。包含 Editor 引用规范化的直接引用、数组和空元素断言。 |
| Debug 集成回归 | `GuiFinalSessionRetest.log` 的 DotNetHostSmoke 和 DemoSuiteEditorTests 共 2 项通过；`GuiDebugFinalSuite.log` 的准备单测、ManagedProtocol、DemoSuitePlayer 和 BuildSettings 共 4 项通过。 |
| Release 构建与验收 | 受影响 Engine/Editor/Player、托管测试及旧项目回归目标构建成功；`GuiReleaseAcceptance.log` 实际执行 31 项，31 项通过，包含 Runtime/Sample/DemoSuite 内容准备、核心回归、托管协议/宿主、新 Demo 及旧项目 Scene/Prefab、PlaySession、Editor 扩展。耗时 178.22 秒。 |
| 关闭托管支持的独立构建 | `Build/VS2026-Native` 以 `URSA_ENABLE_DOTNET=OFF`、空 `URSA_PROJECT_DIR` 重建 Editor 和原生测试；`GuiNativeFinal.log` 实际执行 5 项（内容准备及 4 项原生测试），全部通过。原生 Editor 对独立空项目执行 `--hidden --frames 3`，退出码 0；无需托管宿主或绑定生成目标。 |
| 正式 DemoSuite 完整 Player 录制 | Debug/Release 的松散和独立 Pak 各执行 60 帧，直接启动执行 4 帧；Menu → OnGUI → 重置 → Menu → OnGUI → Menu，动作、中文/辅助平面字符、焦点、裁剪、6 次场景代数和纹理像素断言通过。故障 Pak 的两次真实菜单点击均保留原 Menu 且可重试；5 项无效配置保留发布快照原字节。 |
| Editor 与 Player 对照 | Debug/Release 各运行两轮 Editor Run，验证 Pause/Step、暂停时导航、截图和 Stop 后完整作者图、Dirty、选择。每种配置的 `Editor/HostParity.json` 均记录与松散/Pak 共 120 组帧状态比较通过，GUI 回调/动作次数、对象数、焦点和资产身份一致。只排除显式 Pause/Step 导致不同的模拟 Update 次数；滑块坐标映射浮点比较绝对容差为 0.00001，整数、字符串和资源身份保持精确比较。 |
| 正式 Demo 导入与运行 | 旧 Library 冷导入已通过；最终 Debug Both 刷新退出码 0，19 项导入输入命中缓存，Player Ready 包含 32 项发布条目且不含 Editor 模块。正式 Demo 再执行完整 60 帧菜单录制通过，证据在 `Build/Temp/ActualDemoEvidence`。IDE 项目由正式工作区生成器重新生成。 |
| 旧项目回归 | SnakeLogic、项目脚本构建、两个 Snake Player、Scene/Prefab 往返、项目 PlaySession、Editor 扩展及 Profiler 已执行通过。Profiler 完成三轮 Run/Pause/Resume/Stop 与最小化恢复。旧资源来自 Engine 自包含夹具。 |
| 绑定与格式 | Debug/Release `UrsaCheckBindingsSnapshot` 通过，Engine/Demo `git diff --check` 通过。本次变更的 70 个 C++ 文件通过 clang-format 19；全仓库格式检查退出码 1，报告 19 项已存在差异。`GuiFormatBaseline.json` 逐项以 HEAD 源码重新执行格式检查，确认 19 项均在本次变更前存在；未批量改写无关文件。 |
| OpenSpec | 两项变更的 strict 校验均通过。`runtime-ongui` 为 41/42，`demo-feature-test-suite` 为 30/31；分别保留 9.4、7.5 的系统交互验收未勾选。2026-11-07 按用户“提交并归档”指令同步主规范并归档，保留未完成任务及验收边界。 |

2026-11-06 启用测试时 Debug CTest 实际发现 199 项：保留原 186 项注册并新增 13 项；没有删除旧测试注册。发现数不代表全量执行，本记录只对上述实际运行的测试报告通过。日志位于 `Engine/Build/Temp`；新套件的状态、截图和故障日志位于 `Build/VS2026/TestProjects/DemoSuiteEvidence/Debug` 与 `Release`。自动验证已完成，下面列出的系统交互验收仍未完成；2026-11-07 已按用户指令归档，归档不代表这些人工检查已经通过。

## 规范场景与测试映射

| 规范场景 | 实施/自动验收入口 |
| --- | --- |
| 两个 ImGui Context、纹理命名空间、异常恢复、fence 保留 | `UrsaImGuiRendererTests`、`UrsaImGuiBridgeTests` |
| 中文字体/Shader 发布、最小 Pak、损坏资源诊断 | `UrsaGuiCanvasTests`；`GuiCanvasTests.cpp` 仅挂载两项运行时资源 |
| Current 作用域、Use/Used、ID/焦点、ExitGUI、组恢复 | `UrsaGuiSessionTests`、`UrsaManagedGuiTests` |
| 按事件编辑、辅助平面字符、选区、Validate/Execute 一次执行 | `GuiSessionTests.cpp` 的文本/命令断言；DemoSuite 完整录制覆盖 Unicode 与 Tab |
| 生命周期、每输入前 Layout、多次固定步/一次 Update、Pause | `UrsaGuiFrameTests`、`UrsaManagedGuiTests`、`UrsaDemoSuiteEditorTests` |
| 回调中新建/禁用/移除/销毁行为，不重放旧事件 | `GuiFrameTests.cpp` 的 MutationBoundary |
| 原始 Window 顺序、物理键与字符分离、消费账本 | `UrsaGuiFrameTests`、`UrsaWindowInputTests`；不把原始 Window 轮询当作过滤后的 Input |
| 正常 End 最终合成、Abort、空 GUI、嵌套裁剪/alpha | `UrsaGuiCanvasTests`、`UrsaRenderCommandExecutorTests` |
| Forward、Universal、多相机、自定义 Pipeline、曝光/Bloom 后的 GUI | `DotNetHost/Rendering.cpp` 的最终 GUI 像素断言与 `RenderCommandExecutorTests.cpp` 的曝光/Bloom 断言 |
| Game 像素与坐标、Fit/缩放/平移/留边/DPI、窗口隔离 | `UrsaViewportLayoutTests`、`UrsaImGuiBridgeTests`、`UrsaDemoSuiteEditorTests`；几何或注入证据不能替代下面的系统交互检查 |
| 场景列表/稳定身份、无效配置不替换已发布产物 | `UrsaSceneBuildSettingsTests`、`UrsaDemoSuiteBuildSettingsTests` |
| Single 排队、准备失败保留旧图、旧句柄失效/静态保留 | `UrsaRuntimeSceneManagerTests`、`UrsaManagedGuiTests` |
| SceneLoaded 重入、清理异常、激活后的 native 致命错误 | `RuntimeSceneManagerTests.cpp`、`DotNetHost/Gui.cpp` |
| Demo 目录、通用场景序列化、安全清理与隔离 | `UrsaDemoSuitePreparationTests`、`PrepareDemoSuite.py`、`LegacyDemoMigration.json`、`LegacyDemoDeletion.json` |
| 真实菜单按钮、重置、直接启动返回、Pak 错误恢复 | `UrsaDemoSuitePlayerTests`；故障包保留资源目录并损坏目标 Scene 内容 |
| 两轮 Editor Run、暂停导航、Step、Stop 恢复作者 Dirty/选择 | `UrsaDemoSuiteEditorTests`；检查整个 Run 最初的作者快照 |

测试映射表示断言入口，只有实际执行通过才计完成。增加测试后必须检查发现数；不存在的测试名或零匹配不计通过。OpenSpec 的任务勾选仍以完整任务为单位。

## 复验命令

在仓库根目录执行；构建配置和测试配置须一致。内容准备包含脚本和 Shader 编译，避免与同一项目的 Editor、打包或其他导入并行运行。

```powershell
cmake --build Engine/Build/VS2026 --config Debug --target UrsaScriptPlayer UrsaEditor UrsaAssetImport UrsaPak UrsaDemoSuiteEditorTests UrsaDotNetHostTests
ctest --test-dir Engine/Build/VS2026 -C Debug -R '^UrsaDemoSuite' --output-on-failure
ctest --test-dir Engine/Build/VS2026 -C Debug -R '^Ursa(Gui.*|ManagedGui|ManagedProtocol|ManagedHandle|ManagedBehaviourRuntime|SceneBuildSettings|RuntimeSceneManager|ViewportLayout|ImGuiBridge|ImGuiRenderer|PlaySession|RenderCommandExecutor|RenderCommandProtocol|RendererWindow|FrameScheduler|WindowInput)Tests$' --output-on-failure
# Release 同样构建这些目标，并以 -C Release 执行上述测试。
ctest --test-dir Engine/Build/VS2026 -C Release -R '^Ursa(DotNetHostSmoke|ProjectPlaySessionTests|ProjectEditorExtensionsTests|ProjectScenePrefabRoundTripTests)$' --output-on-failure
cmake --build Engine/Build/VS2026 --config Debug --target UrsaCheckBindingsSnapshot
python Engine/Tools/Format.py --check --clang-format-path Engine/Build/Tools/ClangFormat19/clang_format/data/bin/clang-format.exe
openspec validate runtime-ongui --strict
openspec validate demo-feature-test-suite --strict
```

`--fixture-exclude-any` 只可在对应配置的有效夹具已准备且托管契约未变化时使用。更改托管框架后应重新导入，不复用旧 Ready/Pak。Player/Editor 的 replay JSON 与最终截图位于 `Build/VS2026/TestProjects/DemoSuiteEvidence/<配置>`。

## VS 调试回归（2026-11-07）

人工反馈：在 VS 调试 Editor Play，点击菜单按钮后，`GUIUtility.ExitGUI()` 被显示为“用户未处理”的 `Ursa.ExitGUIException`。运行时已有正常捕获；缺少调试器对该框架控制流的识别。

将 OnGUI 调用和专属 `catch (ExitGUIException)` 提取到禁止内联的方法，并使用 .NET 10 的 `DebuggerDisableUserUnhandledExceptions` 标记该捕获边界。该方法不捕获其他异常；真实脚本错误仍向原有宿主错误隔离边界传播。没有修改 VS 全局设置或项目菜单的 ExitGUI 用法。

Debug/Release 的正式托管框架均已重建。当前主构建的 `URSA_BUILD_TESTS=OFF` 保持不变；本次在独立输出目录构建并直接运行协议测试，同时执行既有集成程序，共 5 个回归入口通过：Debug/Release 协议测试、Debug/Release 真实托管 GUI 测试，以及 Debug Editor 的两轮菜单导航、Pause/Step、像素和 Stop 作者状态恢复。新增协议断言检查标记与禁止内联元数据、重复正常退出和原始真实异常传播；构建无警告/错误，变更文件 whitespace 检查通过。证据位于 `Build/Temp/GuiDebugger`。

这些测试未附加 VS 调试器，尚不能作为 VS 异常助手不再暂停的人工证据。需要停止旧调试进程后重新启动复测；显式配置“引发时中断”仍可在正常退出抛出时暂停，应仅调整该异常类型。参考 [Microsoft 调试器属性文档](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerdisableuserunhandledexceptionsattribute?view=net-10.0)。原有 DPI/输入法/剪贴板的待验收状态不变。

## 待系统交互的项目

曾有一次 DemoSuite 冷导入与 Release 构建争用资源而超过 600 秒，该次失败未计通过；后续独立准备和 Debug/Release 完整流程均已通过。Editor 与 Player 比较使用 Window 事件注入，不能替代以下系统交互检查。

| 系统交互项目 | 状态 |
| --- | --- |
| Windows Vulkan，100%/150%/200% 系统 DPI，停靠与独立 Game | 未取得全部系统配置下的实测证据 |
| 真实鼠标按下/拖出/释放、独立 Game 与主窗最小化恢复 | 自动注入/窗口回归有覆盖；人工操作尚未记录 |
| 物理键盘、中文输入法组合/候选窗、提交中文与辅助平面字符 | 已验证窗口已提交字符的注入；真实输入法尚未记录 |
| 系统剪贴板 Ctrl+A/C/X/V、Validate 无副作用 | 命令和文本单测有覆盖；真实系统剪贴板操作尚未记录 |
| Linux/macOS、其他图形后端 | 未验证 |

不能将代码路径、单测或截图外观解释为以上人工检查已经通过。内置 Noto Sans SC 不覆盖所有 Unicode 字形，emoji 可显示缺字字形；值编辑与字体覆盖应分别判断。

## Unity 参考与 Ursa 差异

2026-11-06 核对 Unity 6000.0 官方文档：[OnGUI](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/monobehaviour/ongui) 说明按事件多次调用；[ExitGUI](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/guiutility/exitgui) 说明退出当前 GUI 循环并保持控件 ID 顺序；[LoadScene](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/scenemanagement/scenemanager/loadscene) 说明延后加载；[sceneLoaded](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/scenemanagement/scenemanager/sceneloaded) 位于 OnEnable 与 Start 之间。

Ursa 使用 PascalCase 的 `Event.Current`、`GUIUtility.GetControlId`、`SceneManager.SceneLoaded`；Rect/鼠标坐标采用 Game 目标像素。每个输入前重新 Layout，模拟后再 Layout/Repaint；暂停 GUI 仍运行，但 GUI DeltaTime 为零，模拟时钟冻结。组件具有独立 ID 命名空间，重叠控件按调用顺序消费。LoadScene 在下一可绘制宿主帧准备和激活，准备失败保留旧场景；Single 保留运行脚本域和静态字段，名称/路径查找区分大小写。首版不提供 GUILayout、Additive、LoadSceneAsync、DontDestroyOnLoad 或 Unity 所有 IMGUI 皮肤/控件 API。
