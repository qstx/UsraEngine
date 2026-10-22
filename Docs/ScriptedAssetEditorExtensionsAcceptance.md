# 脚本资源编辑扩展验收记录

记录日期：2026-10-22。早期实施记录保留如下，后续各节记录当前 Preview、格式和最终集成回归的真实结果。

环境：Windows、MSVC 17.13、.NET SDK 10.0.100、CMake 3.28.0-rc4。真实 Player 使用 NVIDIA GeForce RTX 4060 Laptop GPU / Vulkan。当前生成绑定为 5 个模块、266 个方法、112 个 wire 类型。用户于 2026-10-22 明确取消后续关闭 .NET 的配置验证；本次最终验收仅覆盖启用 .NET 的配置，早期 OFF 记录仅作为历史结果保留。

## 已执行

| 检查 | 结果 | 日志（Engine/Build/Temp 下） |
| --- | --- | --- |
| 空 Library：CLI 与 Editor 的自定义格式、PNG、带骨骼动画 FBX 身份/内容/指纹及处理顺序 | 通过 | ScriptedAssetExtensions-workflow-expanded-tests.log |
| 修改代码、版本、顺序、增加/移除处理器、配置依赖及无关原生缓存 | 通过 | ScriptedAssetExtensions-worker-regression.log |
| 回调运行中修改源、修改脚本、取消；编译/注册失败；并发 CLI 租约 | 通过 | ScriptedAssetExtensions-worker-regression.log |
| 原生管线、托管处理器和协议回归 | 5/5 通过 | ScriptedAssetExtensions-worker-regression.log |
| 同轮生成 C# 依赖的编译/导入循环诊断、完整冷启动回归 | 通过 | ScriptedAssetExtensions-player-adapter-tests.log（其中 Player 子项曾因测试场景缺相机失败，后续单独修复复测如下） |
| 真实 Player：Loose/Pak 加载生成 SO/纹理/Mesh/材质及 Prefab；检查未装载 Ursa.Editor / Ursa.EditorScripts | 通过 | ScriptedAssetExtensions-player-camera-tests.log |
| importer 草稿接入统一属性事务：字段/集合、引用失败原子性、外部修改、只读及过期 | 通过 | ScriptedAssetExtensions-player-adapter-tests.log，UrsaSerializedObjectTests |
| URSA_ENABLE_DOTNET=OFF，dotnet 路径不存在：Release 原生工具及规则测试 | 构建、测试通过 | ScriptedAssetExtensions-native-final6-build.log / ScriptedAssetExtensions-native-final6-tests.log |
| 无 .NET CLI 导入普通原生项目；加入未发现的 Editor 规则后拒绝导入 | 通过 | ScriptedAssetExtensions-native-cli.log / ScriptedAssetExtensions-native-required.log |

| 默认/托管 importer 控件、属性事务及绑定协议 | 5/5 通过 | ScriptedAssetExtensions-importer-controls-tests.log / ScriptedAssetExtensions-inspector-matching-tests.log |
| 从空 Library 编译文档 ScriptedImporterEditor，真实 Apply 后生成数据反映 scale | 通过 | ScriptedAssetExtensions-scripted-gui-tests.log |
| 源 SO 统一属性事务：集合、枚举、引用回滚、冲突、只读、失效；真实 GPU 控件保存；C# 类型化 SO Target 与保存隔离；原生/托管 importer 及协议 | 5/5 通过 | ScriptedAssetExtensions-typed-source-tests.log |
| 实际 Apply/Discard/Cancel 模态控件、忙碌禁用、模型嵌入默认字段及自定义覆盖 | 通过 | ScriptedAssetExtensions-typed-source-tests.log，UrsaAssetImporterInspectorTests |
| Mesh 编辑器真实窗口、材质槽、Scene/Prefab 保存重开及两次 Play/Stop | 通过（386 秒） | ScriptedAssetExtensions-mesh-complete-tests.log |

| 组件/原生 Camera/SO/Material/独立 Prefab 类型化 Target、GetComponent 脚本字段、旧包装拒绝、对象缓存隔离和协议 | 2/2 通过 | ScriptedAssetExtensions-child-proxies-tests.log |
| 材质源完整保存链路、共享已有文档与实际 GPU 预览（含内容 fixture） | 2/2 通过 | ScriptedAssetExtensions-material-source-tests.log |
| 独立源文档/共享文档、真实字段与 Save/Discard/Cancel、外部元数据冲突、只读与对象快照 | 3/3 通过 | ScriptedAssetExtensions-snapshot-tests.log |
| 类型化持久资源依赖、只读/克隆、SO 修改/销毁/重新发布拒绝，以及 PNG/FBX 处理器和 Inspector 回归 | 5/5 通过 | ScriptedAssetExtensions-persistent-current-tests.log |

| 最终统一属性服务：源资产、importer、Scene/Prefab 属性与绑定；含内容 fixture | 8/8 通过 | ScriptedAssetExtensions-properties-final7-tests.log |

## 2026-10-22 Preview 与故障回归

| 检查 | 实测结果 | 日志（`Engine/Build/Temp/`） |
| --- | --- | --- |
| Preview 生命周期、importer 工具栏、共享 Inspector 实例、二维像素/Alpha/颜色空间及默认回退 | Debug 通过 | `ScriptedAssetExtensions-resume-baseline.log` |
| 修复材质纹理子资产加载时丢失 local file ID，复测 importer 和处理器 | 2/2 通过，退出 0 | `ScriptedAssetExtensions-resume-material-tests.log` |
| GPU Preview：相机拖动、灯光、DPI、窄区域、无有效 Game 相机、同目标失败/跨目标清空、无有效画面区域、反复切换、跨项目、提交期间选择改变及关闭 | Debug Loose/Pak 通过；该次模型测试夹具仍使用旧 CBOR 解码而失败，不能将整组记为通过 | `ScriptedAssetExtensions-resume-format-unify-tests.log` |
| 真实 Editor：活动 SRP 的自定义 Preview、Scene/Game/Inspector、3 次 Play/Stop、脚本重载、最小化/恢复、外部画面捕获 | Debug 通过，含 fixture 2/2，退出 0 | `ScriptedAssetExtensions-resume-minimized-tests.log` |
| Release 的导入管线、空 Library Editor/CLI 等价、Loose/Pak Player 及 importer Apply | 正常本地权限复测 4/4 通过，退出 0 | `ScriptedAssetExtensions-resume-Release-workflows.log` |

SRP 应用证据位于 `Engine/Build/TestProjects/SrpAcceptance/Debug/dc1e8506-870d-4161-ac2a-fb85954fa204/`，包含 `04Preview.png`、`05Game0.png`～`05Game2.png`、`06ReloadedScene.png`、`07DefaultForward.png`、外部捕获及其结果 JSON。Preview 的测试项目没有有效 Game 相机，Scene 作者文档也不同于选中资产；测试同时断言作者对象数、Dirty/Revision 和游戏回调计数保持不变。

故障记录与处理：

- 最初的组合回归暴露材质纹理子资产仅按 GUID 加载的问题；统一资源会话保留 GUID/local file ID 后 importer/处理器通过。原先的 JSON→对象→Archive 重编码和裸 CBOR 材质分支已移除，模型 importer 8 与脚本材质统一使用当前 `.ursamat`。
- Gold/Ceramic 测试夹具虽然标记格式 2，却缺少 Shader 身份。修正夹具的 `shaderId` 和原有参数，保留 GUID；没有添加旧格式兼容分支，也未改写格式正确的 Engine/Demo 源资产。
- Windows 最小化后 framebuffer 可能仍有尺寸；Editor 现在同时检查窗口 iconified 状态，并丢弃本帧 GUI 回调后最小化的待提交 Preview。真实窗口计数/时钟断言通过。
- 受限运行的 Release 导入测试曾因无法原子替换 Build 隔离 Shader 暂存文件返回 Windows error 5（`ScriptedAssetExtensions-resume-Release-tests.log`，退出 8）；使用正常本地权限执行相同测试后 4/4 通过。失败运行未记作通过。

主要早期命令：

```powershell
$env:_CL_='/Z7'
cmake --build Engine/Build --config Debug --target UrsaRuntime UrsaEngine UrsaEditorCore UrsaScriptPlayer UrsaAssetImport UrsaManagedAssetImportWorkflowTests -- /p:BuildProjectReferences=false /p:MultiProcessorCompilation=false
ctest --test-dir Engine/Build -C Debug -R '^Ursa(ManagedAssetImportWorkflowTests|AssetImportRulesTests|AssetImportPipelineTests|AssetPostprocessorTests|ManagedProtocolTests)$' --output-on-failure
ctest --test-dir Engine/Build -C Debug -R '^UrsaGeneratedAssetPlayerWorkflowTests$' --output-on-failure
ctest --test-dir Engine/Build/BindingsFinalOff -C Release -R '^UrsaAssetImportRulesTests$' --output-on-failure
```

部分测试需要原子替换 Build 隔离目录中的 Shader 暂存文件；受限环境首次运行返回 Windows error 5 后，在同一授权工作区复测通过。测试不使用 Demo 的 Library 或未提交资产。

## 本次收尾范围：完成至 10.1

按用户 2026-10-22 的最新指示，本次实施截止任务 10.1；整体为 35/37 项。10.2 和 10.3 保持未完成，不把下列已执行的分项结果宣称为完整集成验收或最终矩阵交付。

| 检查 | 结果/退出码 | `Engine/Build/Temp/` 日志 |
| --- | --- | --- |
| 启用 .NET 的 Debug Editor、CLI、Player 及受影响测试最终构建 | 通过，0 | `ScriptedAssetExtensions-10.1-Debug-final-build.log` |
| 启用 .NET 的 Release 同组目标最终构建 | 通过，0 | `ScriptedAssetExtensions-10.1-Release-final-build.log` |
| clang-format 19 全部 547 个第一方 C++ 文件 | 通过，0 | `ScriptedAssetExtensions-final-format-bindings.log` |
| 生成绑定快照漂移检查：5 模块、266 方法、112 wire 类型 | 通过，0 | `ScriptedAssetExtensions-final-format-bindings.log` |
| 普通 Snake 键盘模式：Input/Time、P 暂停/释放/再按恢复、作者数据保持 | Debug、Release 均通过；此前错误的测试输入已改为向 FrameRunner 提供按键状态，由其产生边沿 | `ScriptedAssetExtensions-final-Debug-recheck.log`、`ScriptedAssetExtensions-final-Release-recheck.log`、`ScriptedAssetExtensions-snake-keyboard-final-tests.log` |
| 托管 OnDisable/OnDestroy 在原生清理前访问自身所属对象 | Debug、Release 均通过 | `ScriptedAssetExtensions-final-Debug-recheck.log`、`ScriptedAssetExtensions-snake-input-Release-tests.log` |
| README 示例：等待真实自定义 Inspector 回调，再按子进程 PID 捕获并正常关闭 Editor | Debug、Release 通过，0 | `ScriptedAssetExtensions-example-ready-Debug-tests.log`、`ScriptedAssetExtensions-example-ready-Release-tests.log` |
| 共享 Inspector/Preview 实例将 Apply/Revert 已绘制状态传回宿主，避免重复按钮 | Debug 2/2 通过，0 | `ScriptedAssetExtensions-shared-controls-Debug-tests.log` |

最终构建命令（从根仓库运行，两种配置执行同一组目标）：

```powershell
$env:_CL_='/Z7'
$targets = @(
  'UrsaEditor', 'UrsaAssetImport', 'UrsaScriptPlayer', 'UrsaDotNetHostTests',
  'UrsaAssetPreviewPanelTests', 'UrsaAssetPreviewWorkspaceTests', 'UrsaEditorWorkspaceWindowTests',
  'UrsaManagedAssetImportWorkflowTests', 'UrsaManagedSceneApiTests', 'UrsaProjectPlaySessionTests',
  'UrsaEditorSrpAcceptanceTests', 'UrsaAssetImporterInspectorTests', 'UrsaNativeAssetInspectorTests'
)
cmake --build Engine/Build --config Debug --target $targets -- /p:MultiProcessorCompilation=false
cmake --build Engine/Build --config Release --target $targets -- /p:MultiProcessorCompilation=false
cmake --build Engine/Build --config Debug --target UrsaEngineFormatCheck UrsaCheckBindingsSnapshot
```

`/Z7` 是此工作区原有的调试信息配置覆盖；MSVC 会报告覆盖 `/Zi` 的命令行提示，不能将其当作源代码警告或隐藏编译错误。构建未修改第三方或手工编辑生成绑定。

本次还修复了正常 Demo Play 的帧上下文访问：将跨模块的 `SetFrameContext` / `SetBehaviourRuntime` 实现集中到 `ManagedSceneApi.cpp` 并完整重建消费它们的模块。新增普通键盘模式回归，避免原 smoke 模式跳过 Input 读取而漏检。已打开的旧 Editor 进程须重启后使用新原生代码。

故障路径补充：完整类型信息下的源资产外部修改曾暴露未平衡的 ImGui disabled 栈；原生资源和 ScriptableObject Inspector 已使用作用域清理，Preview 全类型复测通过。工作区第二次冷启动现在从同步 `OpenProject` 完成后单独计时，Debug/Pak 的项目切换和完整重启测试均通过。固定少量帧的示例截图不能证明自定义界面已加载，因此示例测试现在等待真实回调并使用 PID 捕获。

示例 Debug 共享控件修正后的实际界面证据：`Engine/Build/Temp/ManagedAssetWorkflow/dc5302f0-5f04-49f3-a3b0-b1d91e7197c0/EditorImporter.png`，同目录的 `EditorCallback.txt` 证明真实自定义 Inspector 已执行。Preview 的分项 DPI 证据继续位于 `Engine/Build/Temp/ManagedAssetPreviewEvidence/{Debug,Release}/{Loose,Pak}/`；最终矩阵汇总留给 10.3。
