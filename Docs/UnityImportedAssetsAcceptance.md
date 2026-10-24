# Unity 风格导入资产浏览验收

变更：`unity-style-imported-asset-browser`。2026-10-24，Windows / MSVC 2022 / .NET 10 / Vulkan，Debug。

## 行为与验证入口

| 行为 | 自动验证 |
| --- | --- |
| 完整/可见目录、非零主对象、零 ID 别名、同名补充标签、隐藏节点 | `UrsaAssetImportRulesTests`、`UrsaProjectBrowserModelTests` |
| 名称单独变化更新 Revision、失败不混入新目录、版本 3 重建、重开 | `UrsaAssetImportRulesTests`、数据库回归 |
| FBX/OBJ/glTF/GLB 源名称、后处理节点与新增资源描述、非法目录拒绝 | `UrsaModelImportCandidateTests` |
| 新旧 AddObjectToAsset、注册重排/独立改名、主对象切换、冷 Editor/CLI 一致 | `UrsaManagedAssetImportWorkflowTests` |
| 源及父文件夹移动、对象隐藏/删除/恢复、主对象更换、选择取消 | `UrsaProjectBrowserModelTests`、`UrsaAssetImporterInspectorTests` |
| 真实 Project 展开与点击、取消不改 Hierarchy、精确拖拽 Mesh/Material | `UrsaAssetPreviewPanelTests` |
| C# AssetField 精确拖拽、错误类型拒绝、Scene/Prefab 保存重开 | 同一真实面板测试及 `UrsaEditorBindingsTests` |
| Skeleton/AnimationClip 只读属性、精确动画预览、删片清旧图、节点检查保留草稿 | `UrsaAssetPreviewPanelTests`、`UrsaManagedAssetPreviewTests` |
| 已生成资产由 Loose/Pak Player 加载、不加载 Editor 程序集 | `UrsaGeneratedAssetPlayerWorkflowTests` |
| 示例 CustomEditor 与完整编辑器工作区 | `UrsaScriptedImporterInspectorWorkflowTests`、工作区回归 |

截图由测试实际绘制的 ImGui/GPU 画面生成，保存在 `Build/Temp/AssetPreviewEvidence/Debug/{Loose,Pak}` 与 `Build/Temp/ManagedAssetPreviewEvidence/Debug/{Loose,Pak}`。可从下列条目检查名称、选中状态、属性和预览：

- [Project Mesh 子行与 Inspector](../Build/Temp/AssetPreviewEvidence/Debug/Loose/15-Named-Project-Mesh.png)
- [Skeleton Inspector](../Build/Temp/AssetPreviewEvidence/Debug/Loose/16-Skeleton-Inspector.png)
- [AnimationClip Inspector 与模型上下文预览](../Build/Temp/AssetPreviewEvidence/Debug/Loose/17-Clip-Inspector-Preview.png)
- [模型层级节点与只读变换](../Build/Temp/AssetPreviewEvidence/Debug/Loose/18-Model-Hierarchy-Node.png)
- [脚本导入对象的 Project/Inspector/Preview](../Build/Temp/ManagedAssetPreviewEvidence/Debug/Loose/14b-Project-Subasset-Inspector.png)

## 人工复核步骤

1. 保留 Assets 和 `.meta`，准备隔离项目并重新导入。Library 对象目录版本为 4；旧索引会要求重建。
2. 选择 FBX、OBJ、glTF 或 GLB。源行代表主对象，箭头只展开可见非主对象；ModelNode 不在列表，单个主输出没有空箭头。搜索子资产名称或类型应保留所属源文件路径。
3. 点击具名 Mesh、材质、内嵌纹理、骨骼或动画子行。Inspector 检查具体对象；动画预览使用其所属主模型且只播放所选片段；骨骼没有整模型回退画面。
4. 源模型的 Hierarchy 内检查空节点、网格节点和骨骼节点。节点变换只读；查看不增添 Scene 对象，不改变文档 Dirty 或 importer 草稿。需要作者对象时使用 Add to Scene，之后按现有 Hierarchy 与 Scene/Prefab 流程编辑。
5. 修改 importer 草稿，再从 Project 或预览控件切换对象，选择 Cancel。Project 高亮、Inspector、Preview 和 Hierarchy 都应保留原已接受状态；Apply/Discard 后才切换。
6. 使用 DatasetImporter 示例，展开多类型输出。`identifier` 保持稳定，`displayName` 可以独立变化。将 Mesh/材质拖入原生字段或 C# AssetField；错误类型不能改写字段。保存 Scene/Prefab 后重开，检查具体 GUID/local ID。
7. 移动源或父目录，应跟随 GUID 恢复路径。删除选中输出会保留缺失身份并清除旧预览；同 ID 恢复后可重新显示。导入失败保留上一份完整结果并显示失败状态。

## 执行记录

本轮选择 25 项相关回归和 3 个自动内容 fixture。综合首轮为 24/28，见 [综合回归日志](../Build/Temp/UnityImportedAssets-final-tests.log)。四个旧夹具问题修正后，管线与文档用例在 [fixture 补验](../Build/Temp/UnityImportedAssets-final-fixture-recheck.log) 中通过；该日志还保留当时两个宿主用例的失败，二者最终在 [宿主补验](../Build/Temp/UnityImportedAssets-final-host-recheck.log) 中 2/2 通过。**全部 25 项所选回归均已取得通过证据**，不将首轮日志改写为整轮通过。

| 最终检查 | 结果 | 证据 |
| --- | --- | --- |
| 原生面板 Loose/Pak、最终无预览提示和缺失操作保护 | 通过，退出码 0 | [最终面板日志](../Build/Temp/UnityImportedAssets-final-native-ui.log) |
| 托管 Preview Loose/Pak、精确子资产 Inspector | 通过，退出码 0 | [最终托管面板日志](../Build/Temp/UnityImportedAssets-final-managed-ui.log) |
| 完整预览工作区与布局恢复 | 通过，278.19 秒 | 综合回归 `UrsaAssetPreviewWorkspaceTests` |
| Editor 工作区 | 通过，58.02 秒 | 综合回归 `UrsaEditorWorkspaceWindowTests` |
| Editor/CLI 冷导入、独立改名、注册重排与主对象切换 | 通过，130.41 秒 | 综合回归 `UrsaManagedAssetImportWorkflowTests` |
| 生成资产 Loose/Pak Player | 通过，38.19 秒 | 综合回归 `UrsaGeneratedAssetPlayerWorkflowTests` |
| 示例 importer 的完整 Editor/CustomEditor | 通过，102.06 秒 | 综合回归 `UrsaScriptedImporterInspectorWorkflowTests` |
| 导入管线与文档保存补验 | 通过，80.03 / 1.25 秒 | fixture 补验对应行及 [独立管线日志](../Build/Temp/UnityImportedAssets-final-pipeline.log) |
| ScriptedImporter / Postprocessor 宿主补验 | 通过，2/2 | 宿主补验日志 |


- Debug 受影响原生、绑定、托管和工作区目标构建通过：[构建日志](../Build/Temp/UnityImportedAssets-final-build.log)、[面板最终重链接](../Build/Temp/UnityImportedAssets-final-relink.log)。绑定检查验证 5 个模块、279 个方法、124 个 wire 类型。
- 针对补验中，真实面板 Loose/Pak 为 114.29 秒通过；完整托管 Editor/CLI 工作流为 128.82 秒通过；Loose/Pak Player 工作流为 39.09 秒通过。该结果分别见 [预览补验](../Build/Temp/UnityImportedAssets-preview-recheck.log) 与 [托管/Player 补验](../Build/Temp/UnityImportedAssets-targeted.log) 的对应行；后者包含当时尚未修复的预览失败，不能把整份日志记为通过。
- `openspec validate unity-style-imported-asset-browser --strict` 通过。
- 本次修改的 37 个 C++ 文件按 `Tools/Format.ps1` 的逐字节比较规则检查通过：[修改文件格式日志](../Build/Temp/UnityImportedAssets-changed-format.log)。全库 `Tools/Format.ps1 -Check` 已执行，仍报告两个 **HEAD 中已存在且本次未修改** 的格式问题：`Tests/Editor/Scripting/ScriptBuildTests.cpp`、`ScriptIdentityTests.cpp`，见 [全库格式日志](../Build/Temp/UnityImportedAssets-format-final.log)。

受限环境最初无法访问本机 MSBuild/PDB 服务，构建在获得工具执行许可后使用本机工具链完成。没有更改编译器配置或删改项目资源。最初的失败日志保留，最终回归另行记录。

回归夹具中原有手写的 `test.workflow-reader`、`demo.render-settings` / `demo.other-settings` 与当前自动脚本类型身份不一致。探针和管线用例已改为使用源 `.meta` 对应的自动身份或生成 catalog 返回的身份；没有修改运行时类型发现规则，也没有跳过检查。文档保存用例以及两个托管 importer 宿主用例补齐了已准备内置资源的 fixture；文档用例还配置相应文件系统，使材质重新加载通过实际内容解析路径执行。内存 importer 夹具复制已准备的内置 Shader 及稳定 GUID 元数据，再建立其产物索引，保证依赖验证覆盖真实资源。

本变更沿用已有五类脚本输出与已有模型格式，不修改 `.meta` GUID、稳定输出 key、AssetReference、运行资源或 cooked 目录格式。显示目录只属于可重建的 Editor Library。
