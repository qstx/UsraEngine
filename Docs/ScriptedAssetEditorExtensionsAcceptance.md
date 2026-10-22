# Scripted Asset Editor Extensions 验收记录

最后更新：2026-10-22。平台：Windows x64、Visual Studio 17.13、CMake 3.28.0-rc4、.NET SDK 10.0.100；配置：启用 .NET 的 Debug 与 Release。验收针对 `scripted-asset-editor-extensions`，源代码已包含本计划实现；CTest 汇总日志保存在 `Engine/Build/Temp/`。依据用户明确要求，不执行关闭 .NET 的构建/测试。

## 10.2 CTest 发现与选择集

运行 `ctest --test-dir Engine/Build -C Debug -N` 和 Release 对应命令，均发现 160 项已注册测试。以下 Debug/Release 同名选择集实际启动全部 30 项，含 `UrsaTestContentPrepare`、`UrsaTestSamplePrepare` 两个 fixture 准备项；两边均 `100% tests passed, 0 tests failed out of 30`，无跳过/零匹配。测试覆盖绑定生成和签名、ManagedScene API、Importer Inspector、Preview 面板/回调/状态、导入管线和数据库、Editor 工作区、真实 GUI 工作流、CLI/Player 发布闭包、托管协议、项目编译/Play/模型导入以及活动 SRP。

| 配置 | CTest 选择集结果 | 汇总日志 |
| --- | --- | --- |
| Debug | 30/30 通过，0 失败/跳过；854.06 秒 | `Engine/Build/Temp/ScriptedAssetExtensions-10.2-Debug-ctest.log` |
| Release | 30/30 通过，0 失败/跳过；591.83 秒 | `Engine/Build/Temp/ScriptedAssetExtensions-10.2-Release-ctest.log` |

完整选择正则包含：

```text
^(Ursa(TestContentPrepare|TestSamplePrepare|BindingsTests|BindingsGeneratorTests|BindingsSignatureTests|BindingSnapshotTests|RenderingBindingsTests|ManagedSceneApiTests|NativeAssetInspectorTests|AssetImporterInspectorTests|AssetPreviewPanelTests|ManagedAssetPreviewTests|AssetPreviewStateTests|AssetImportPipelineTests|AssetDatabaseTests|EditorBindingsTests|AssetPreviewWorkspaceTests|EditorWorkspaceWindowTests|ProjectWorkspaceTests|ManagedAssetImportWorkflowTests|GeneratedAssetPlayerWorkflowTests|ScriptedImporterInspectorWorkflowTests|EditorSrpAcceptanceTests|DotNetHostSmoke|ManagedAssetPreviewCallbacksTests|ScriptedAssetImporterTests|AssetPostprocessorTests|ProjectScriptCompile|ProjectPlaySessionTests|ProjectModelImportTests))$
```

CTest 在 `Engine/Build` 注册清单执行；两次运行使用独立的 `Engine/Build/Temp/UserData-10.2-{Debug,Release}` 和 `Tmp-10.2-{Debug,Release}`，命令以 `--output-on-failure -j1` 顺序运行，进程退出码均为 0。此前 10.1 的 Debug/Release 全目标构建、绑定快照与 `UrsaEngineFormatCheck` 记录见本文件下文。

## 10.3 工作流和图像矩阵

| 配置 / 内容模式 | 自定义 Mesh Preview 100/150/200% | 自定义图像 Preview 100/150/200% | Editor/CLI/Player、失败/重载/关闭 |
| --- | --- | --- | --- |
| Debug / Loose | 通过；`Engine/Build/Temp/ManagedAssetPreviewEvidence/Debug/Loose/12-Custom-Mesh-DPI-{100,150,200}.png` | 通过；同目录 `13-Custom-Image-DPI-{100,150,200}.png` | 通过；本节工作流/窗口回归及 30 项 CTest |
| Debug / Pak | 通过；`Engine/Build/Temp/ManagedAssetPreviewEvidence/Debug/Pak/12-Custom-Mesh-DPI-{100,150,200}.png` | 通过；同目录 `13-Custom-Image-DPI-{100,150,200}.png` | 通过；本节工作流/窗口回归及 30 项 CTest |
| Release / Loose | 通过；`Engine/Build/Temp/ManagedAssetPreviewEvidence/Release/Loose/12-Custom-Mesh-DPI-{100,150,200}.png` | 通过；同目录 `13-Custom-Image-DPI-{100,150,200}.png` | 通过；本节工作流/窗口回归及 30 项 CTest |
| Release / Pak | 通过；`Engine/Build/Temp/ManagedAssetPreviewEvidence/Release/Pak/12-Custom-Mesh-DPI-{100,150,200}.png` | 通过；同目录 `13-Custom-Image-DPI-{100,150,200}.png` | 通过；本节工作流/窗口回归及 30 项 CTest |

每个配置/模式目录还包含 `14-Custom-Imported-Subasset.png` 和 `14b-Project-Subasset-Inspector.png`。DPI 图由实际 GPU/ImGui Preview 测试采集：测试在每个比例以 240 px Inspector 宽度绘制；验证 Mesh/PBR 相机拖动和灯光图像差异、二维纹理的透明/半透明像素及 DPI 坐标映射。宽度 70 px 时断言没有可用图像区域后停止渲染和预览时间推进，恢复后时间不追赶。随后同目标注入失败保留最后成功图像、reload 后新实例恢复；隐藏/折叠时停止渲染；跨目标和提交期间切换/关闭覆盖资源释放。详见 `Engine/Tests/Editor/Assets/AssetPreviewPanelTests.cpp` 与对应 Debug/Release 测试结果。截图属于真实 GPU 测试面板而非完整 Editor DPI 窗口截图，这里按该证据边界记录。

真实 Editor GUI 的 Debug/Release SRP 验收截图：

- Debug：`Engine/Build/TestProjects/SrpAcceptance/Debug/6b1b0835-8e7b-4f00-874c-4bcf0cd51d24/`，含 `03Scene.png`、`04Preview.png`、`05Game0.png`～`05Game2.png`、`06ReloadedScene.png`、`07DefaultForward.png`、`08OnDemandPause.png`、`09OnDemandRepeat.png` 与 `Progress.json`。
- Release：`Engine/Build/TestProjects/SrpAcceptance/Release/68fb14d9-fff3-4b29-a46c-76de486cf92d/`，含同名阶段画面与完成状态。
- 两边真实窗口均在 Demo 之外的隔离验收工程运行；回归覆盖活动 SRP Preview、Scene/Game/Inspector 并存、脚本重载、Play/Stop、关闭/最小化与恢复，并断言没有有效 Game 相机时 Preview 仍可查看，作者对象数/Dirty/Revision 与游戏回调计数不因 Preview 改变。

Editor/CLI 等价和 Player 无 Editor 依赖由 `UrsaManagedAssetImportWorkflowTests`、`UrsaGeneratedAssetPlayerWorkflowTests` 两个真实工作流测试覆盖，Debug/Release 均通过。前者从空隔离 Library 执行 Editor 启动源资产、等待实际自定义 Inspector 回调标记、PID 捕获窗口并检查导入结果；CLI 比较同一内容身份/指纹。后者按 Loose/Pak 运行内容闭包并检查运行期不装载 importer/处理器或 `Ursa.Editor`。真实工作流产生的 `EditorImporter.png`、`EditorCallback.txt`、Player 报告保存在 `Engine/Build/Temp/ManagedAssetWorkflow/<run-id>/`。

用户遇到的 Demo Snake Play 输入失败也已在 Debug/Release `UrsaProjectPlaySessionTests` 中覆盖：FrameRunner 按住 P 产生帧输入边沿，验证暂停/恢复和 Time 上下文，无作者场景更改；修复前曾在真实 Editor Demo 运行重现缺失输入帧上下文，修复后测试与 Editor SRP/Play 验收无该错误。详细日志见 `ScriptedAssetExtensions-final-Debug-recheck.log`、`ScriptedAssetExtensions-final-Release-recheck.log` 与 `ScriptedAssetExtensions-snake-keyboard-final-tests.log`。

## 已执行的分项回归记录

| 检查 | 实测结果 | 日志/证据 |
| --- | --- | --- |
| Debug/Release 绑定生成快照、clang-format 19、547 个第一方 C++ 文件 | 通过；5 模块、266 方法、112 wire 类型；格式检查退出 0 | `Engine/Build/Temp/ScriptedAssetExtensions-final-format-bindings.log` |
| SRP Preview、Scene/Game、脚本 reload、三次 Play/Stop、真实窗口截图 | Debug/Release 通过；各自 `Progress.json` phase 20 | 上述 Debug/Release SRP 截图目录 |
| 空 Library Editor/CLI 导入、Inspector Apply 后实际回调、Loose/Pak Player 发布闭包 | Debug/Release 全部通过 | `ScriptedAssetExtensions-example-ready-Debug-tests.log`、`ScriptedAssetExtensions-example-ready-Release-tests.log`；10.2 两份 CTest 总日志 |
| 真实 ScriptedImporter Inspector Apply/Revert、写入/验证错误、待定 Apply/Discard/Cancel | Debug/Release 通过 | 10.2 两份 CTest 总日志中的 `UrsaScriptedImporterInspectorWorkflowTests`、`UrsaAssetImporterInspectorTests` |
| 自定义 Preview DPI、窄区域、同目标失败/最后成功画面、脚本 reload、隐藏/折叠、资源释放 | Debug/Release，Loose/Pak 均通过 | `Engine/Build/Temp/ManagedAssetPreviewEvidence/{Debug,Release}/{Loose,Pak}/`；10.2 两份 CTest 总日志 |
| 普通 Snake 键盘输入帧上下文，暂停/恢复与作者数据不变 | Debug/Release 通过 | `ScriptedAssetExtensions-final-Debug-recheck.log`、`ScriptedAssetExtensions-final-Release-recheck.log` |

## 已知限制/未执行项

- 按用户 2026-10-22 的明确要求，没有测试关闭 .NET 的构建或运行配置；本次任务的其余验收目标均已完成。
- 100%/150%/200% 的逐格 PNG 是 GPU 面板测试捕获；真实 Editor SRP 的 Debug/Release 截图是实际运行窗口阶段图，没有声称它在三种系统 DPI 下分别被捕获。
- Build/TestProjects、Build/Temp 是忽略的本机临时证据，不随源代码提交；本文件和 CTest 汇总日志名提供复现/定位信息。
