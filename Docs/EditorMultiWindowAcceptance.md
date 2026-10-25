# Editor 多窗口基础验收

## 2026-10-25 手动验收问题回归

`UrsaEditorPanelInteractionTests` 使用实际 Hierarchy、Inspector 和 Project 绘制入口，注入鼠标事件验证 Scene 与 Prefab 从空选择开始点击节点后会绘制该对象的组件 Inspector；同时拖动 Project 目录树右边界，验证两侧宽度改变、窄窗口下保留可见区域、选择不变，以及子窗口宽度进入工作区布局设置。`UrsaEditorSelectionContextTests` 另覆盖空选择和全部缺失选择的重复刷新不递增版本，避免按下与松开之间误取消首次点击。

平台浮窗创建和显示不再主动抢焦点；`UrsaImGuiBridgeTests` 实际显示原生窗口并检查源窗口的按住状态保留、浮窗没有获得焦点，同时验证跨窗鼠标事件按来源窗口及事件坐标转换到桌面坐标。浮窗仍可通过后续点击或平台焦点回调获得焦点，普通 Window 的默认焦点行为保留。额外 viewport 关闭独立 vsync，帧节奏由主窗口控制；窗口移动中的可见区域校验复用本帧显示器列表。GPU 资源仍按原有提交完成点释放。

Release 的面板交互、选择上下文、Window 输入和 ImGuiBridge 回归通过，完整 `UrsaEditorWorkspaceWindowTests` 通过（173.93 秒），覆盖标签拖出/重停靠、独立窗口关闭、活动 Play 的跨 viewport 资产交付及布局恢复。Debug 的面板交互、Window 输入和 ImGuiBridge 回归也通过；窗口测试中补齐了两处遗漏的帧结束调用，使 Debug 断言检查可完整执行。Debug 与 Release Editor 均已重新构建。上述输入为测试注入；实际场景中的物理鼠标流畅度仍可人工补充检查。

2026-10-25 在 Windows、Vulkan GPU 和单个已连接显示器环境完成 Release 自动验收。多窗口验收不要求第二台显示器；跨显示器 DPI 兼容性单独作为可选扩展验证。

`Window` 保持原 `Create(width, height, title, visible)` 入口，并提供可配置窗口描述、桌面位置/鼠标、焦点、逻辑与 framebuffer 尺寸、内容缩放、显示器工作区和逐窗口事件订阅。事件属于产生它的窗口；窗口失焦会发布焦点变化并释放窗口输入状态。

一个 RHI Device 可从 `DeviceDesc.window` 取得默认窗口，也可在每个 `SwapchainDesc.window` 指定从属窗口。Swapchain 自己持有窗口 surface、格式、尺寸、获取/呈现状态和重建统计；从属 surface 会检查所选 Vulkan 队列的呈现能力。省略 Swapchain 窗口仍使用原 Device 默认窗口；不带窗口的 Device 继续支持计算和离屏绘制。Swapchain image 引用其状态，surface/state 退休前等待设备空闲，避免从属窗口关闭时提前销毁正在使用的呈现资源。

ImGuiBridge 为一个 Context 安装平台窗口回调，并为每个从属窗口创建 Window 与 swapchain。`BeginFrame` 统一更新 ImGui Context，`RenderPlatformWindows` 逐 viewport 获取、录制显式 `ImDrawData`、提交和呈现；上传缓冲与被绘制纹理保持到对应提交完成。窗口创建或 renderer 创建失败会关闭平台窗口功能并请求 ImGui 回收从属 viewport。Scene、Game 与资产预览按当前面板所在 viewport 的 framebuffer scale 计算目标尺寸；Play 中聚焦的 Game viewport 会把所属 Window 作为输入源和帧上下文窗口。ImGui 捕获键盘/文本、弹出菜单/对话框或处于拖拽期间时，Play 输入为空，避免搜索、命令菜单和跨面板拖拽触发运行场景。主窗口最小化或没有可获取图像时，应用仍构建共享 UI 帧并提交可见从属 viewport，以限时事件等待控制空转。

从布局文件恢复浮动窗口时，平台回调会按所有连接显示器的工作区裁定是否至少有标题区域可见；若窗口已完全落在屏幕之外，会移至最近的可用工作区。Reset Layout 仍删除当前项目布局并使用默认停靠状态。离屏恢复有 ImGuiBridge 自动测试，停靠持久化有 EditorWorkspaceWindow 测试。

Release 检查命令与结果：

```powershell
cmake --build Engine/Build --config Release --target UrsaWindowInputTests UrsaRhiTests UrsaImGuiBridgeTests UrsaAssetPreviewPanelTests UrsaReferencePickerTests UrsaSceneDraftTests UrsaEditorHierarchyTests UrsaEditorWorkspaceWindowTests UrsaPlaySessionTests UrsaEditorDocumentsTests UrsaViewportLayoutTests UrsaEditorGizmoInteractionTests UrsaManagedTestAssemblies
ctest --test-dir Engine/Build -C Release --output-on-failure -j 1 -R 'Ursa(ImGuiBridge|Rhi|WindowInput|AssetPreviewPanel|SceneDraft|EditorHierarchy|EditorWorkspaceWindow|PlaySession|ReferencePicker|EditorDocuments|ViewportLayout|EditorGizmoInteraction)Tests|^UrsaManagedProtocolTests$'
```

此前相关串行 CTest 11/11 通过（含 fixture 准备）；本轮修改后追加的窗口/拖拽/文档回归 5/5 通过（含 fixture 准备）。本轮新增的 `UrsaEditorGizmoInteractionTests` 与 `UrsaViewportLayoutTests` 连同 fixture 为 3/3 通过。ViewportLayoutTests 覆盖负桌面原点、1.5× viewport 缩放、留边拒绝和目标像素映射；GizmoInteractionTests 将隐藏窗口移到负桌面坐标后，仍通过真实多帧 ImGui 输入拖动 Transform Gizmo。该负坐标测试验证桌面坐标换算，不代表物理第二显示器验收。WindowInputTests 覆盖两个窗口的尺寸/失焦事件隔离、负桌面坐标和内容缩放；RhiTests 覆盖默认及第二窗口 swapchain、独立 resize，并在关闭从属 swapchain/window 后保留其 image 引用，同时继续呈现主窗口；ImGuiBridgeTests 覆盖平台窗口创建/销毁、viewport 到输入 Window 映射、窗口销毁取消活动 DragSession、平台创建失败回退、离屏窗口位置恢复、第二 viewport 的 DisplayPos/FramebufferScale 与字体图集共享。它还将一次从浮动窗口路由到共享 ImGui Context 的按键交给 Play 输入门控，验证非 Game 焦点/文本输入会拦截、Game 焦点允许，并在失焦后释放状态；PlaySessionTests 覆盖同一门控。AssetPreviewPanel、ReferencePicker、SceneDraft、EditorHierarchy、EditorDocuments 与 WorkspaceWindow 测试分别覆盖视图交互、字段试选、Scene/Prefab 往返、跨层级选择、保存/取消和布局恢复。

`UrsaEditorWorkspaceWindowTests` 在 Editor 进程中通过多帧 ImGui 鼠标输入驱动 Game 标签脱离 dock，最小化主窗口后确认共享帧继续执行且浮动 viewport 保留平台窗口和渲染后端。随后以隔离用户设置重新启动 Editor，验证 Inspector dock 与浮动 Game viewport 恢复，并把 Game 标题栏拖到 Project 标签条，确认两者重新加入同一 DockNode。另两轮真实窗口运行分别在浮窗创建后打开脏文档关闭提示并取消，以及关闭浮动 viewport 的原生窗口；测试确认前者保留主窗口、脏文档和浮动 viewport，后者销毁从属 viewport 但不关闭主窗口或丢弃脏文档。为保证布局绑定，Editor 在绘制停靠面板前先创建 Dockspace。标签拖放自动化调用真实 ImGui Docking 状态机，但坐标和按键来自测试事件注入，不是物理鼠标操作。

2026-10-25 本轮更新标签拖出/拖回自动交互后串行运行上列 9 项 Release CTest（含内容 fixture），9/9 通过，总耗时 197.57 秒；窗口集成测试耗时 122.16 秒。随后扩展 Play 输入门控并串行追加 `UrsaTestContentPrepare`、`UrsaPlaySessionTests`、`UrsaImGuiBridgeTests` 和 `UrsaEditorWorkspaceWindowTests`，4/4 通过，总耗时 163.25 秒；窗口集成测试耗时 126.77 秒。增加双 viewport 资产拖放后，完整 `UrsaEditorWorkspaceWindowTests` 再次通过（168.18 秒）；`UrsaImGuiBridgeTests` 与 fixture 2/2 通过。自动测试覆盖 DockNode 变化、独立 viewport 建立、浮动布局持久化、拖回目标 DockNode，以及键盘捕获、文本输入、弹窗和拖拽期间的 Play 输入隔离。`UrsaEditorDragSessionTests` 验证拖动已选 Project 项目传递整个有序选择集、拖动未选项只传单项且不改变 Inspector 选择目标；WorkspaceWindow 测试另覆盖主 viewport 向浮动 Game viewport 的资产引用 DragSession 交付，验证 GUID 不变、viewport 不同且只交付一次。ImGuiBridge 测试分别关闭拖拽源和目标平台窗口，确认两个端点销毁都会取消会话。Clang-format 19 检查全部 561 个 C++ 文件通过，OpenSpec strict validation 通过。自动化使用事件注入，物理鼠标可作补充检查；跨显示器验收为可选扩展。

`UrsaManagedProtocolTests` 另验证同一 C# `EditorWindow` 扩展委托连续执行时保留实例字段状态；`UrsaManagedEditorWorkflowTests` 则通过 `Window` 菜单打开 Editor 脚本中的 C# 窗口，将其并入 Inspector 标签组、拖出为平台 viewport 并通过真实 ImGui 拖动重停靠，验证一个窗口身份连续绘制。该测试使用注入的输入事件。

## 验收状态与待执行操作

| 范围 | 自动证据 | 当前状态 |
|---|---|---|
| 内置面板拖出、跨 viewport 拖资产、浮窗关闭、主窗最小化时呈现、保存后恢复并重停靠 | `UrsaEditorWorkspaceWindowTests`；事件由测试注入到真实 Editor/ImGui Docking 状态机 | 通过；不代表物理鼠标操作 |
| 关闭浮窗时保留脏文档、退出取消时保留窗口和文档 | `UrsaEditorWorkspaceWindowTests` | 通过 |
| C# `EditorWindow` 菜单打开、组合、拖出、重停靠、关闭视图及实例状态 | `UrsaManagedEditorWorkflowTests` 与 `UrsaManagedProtocolTests`；关闭标签后确认脏文档和主窗口仍在 | 自动通过；事件由测试注入，物理鼠标操作仍待人工验收 |
| 搜索/文本输入、点击打开弹出菜单、拖动期间拦截 Play 输入，Game 焦点恢复及失焦释放 | `UrsaPlaySessionTests`、`UrsaImGuiBridgeTests` 的浮动 viewport 搜索/按钮点击，以及 `UrsaEditorWorkspaceWindowTests` 活动 Play 会话中的跨 viewport DragSession | 自动覆盖通过：活动 Play 中拖动期间输入门控清空 Space；均为注入事件，物理鼠标操作仍待验收 |
| 同一显示器下多个原生编辑器窗口、焦点隔离与布局恢复 | `UrsaEditorWorkspaceWindowTests` 创建真实平台 viewport，操作浮动/重停靠、关闭和布局重启恢复；输入通过测试事件注入 | 自动通过；不要求第二台显示器，物理鼠标操作可作为补充人工检查 |
| 跨显示器 DPI、热插拔与跨屏恢复 | 当前自动环境只有一台连接显示器；负坐标和缩放测试只验证坐标换算 | 可选扩展，未运行；不阻塞多窗口功能验收 |
| 完整“拖出→源资产跨窗拖放→模型放入 Scene→保存重开→布局恢复”工作流 | `UrsaEditorWorkspaceWindowTests --cross-viewport-drag-only`；关闭浮窗由同一测试的独立用例覆盖 | 自动集成通过；输入为注入事件，源/目标控件与 Model 文档使用测试夹具，不代表物理鼠标或 Project/Hierarchy 面板点击验收 |

如需补充物理鼠标人工验收，可在同一显示器上拖出 Game 与 C# 工具窗口、移动和调整大小、组合后重停靠并重启恢复；再在浮动 Game 窗口检查对象选择器、菜单、Project 资产拖放和 Play 输入隔离。此项不要求第二台显示器。跨显示器 DPI、热插拔及跨屏恢复属于可选兼容性检查，只有在具备相应硬件时才记录和执行。

2026-10-25 聚焦回归串行执行 `UrsaTestContentPrepare`、`UrsaPlaySessionTests`、`UrsaManagedEditorWorkflowTests`、`UrsaImGuiBridgeTests`、`UrsaEditorWorkspaceWindowTests` 和 `UrsaManagedProtocolTests`，6/6 通过，总耗时 314.53 秒；完整窗口用例 169.65 秒，托管编辑器工作流 106.90 秒。此次结果验证自动测试路径，不改变上表中的人工验收状态。

随后扩展托管编辑器工作流，在实际 `Window` 菜单打开 Editor 程序集中的 C# `EditorWindow`，将它并入 Inspector 标签组后通过 ImGui 标签拖出平台 viewport，再拖回 Inspector，并关闭它的标签；测试验证同一窗口身份连续绘制、没有重复实例，且关闭视图不会关闭脏文档或主窗口。`UrsaManagedEditorWorkflowTests` Release 串行通过（108.51 秒）。该自动测试使用注入的鼠标事件，不代表物理鼠标操作。

真实跨显示器 DPI 验收尚未执行；该项是可选扩展，不影响同一显示器下多窗口功能的验收结论。

2026-10-25 新增跨 viewport 工作流集成：自动化先在真实 Editor 窗口中拖出 Game 面板，再用项目 AssetDatabase 中 OBJ 源的 GUID 建立 DragSession，从主 viewport 投递到浮动 viewport；接收后通过 `SceneDraft::PlaceImportedAssets` 放入 Model 文档，保存并重开 Scene 校验对象身份/名称和干净状态，随后重启 Editor 恢复布局并重停靠 Game。跨窗阶段、场景往返及布局恢复均通过。源按钮/目标落点由测试窗口提供，Model 数据使用 SceneDraft 验证过的有效夹具；普通导入管线另由 AssetImportPipeline 测试覆盖。窗口关闭保留脏文档已有独立自动用例。该流程使用输入事件注入，不等价于真实 Project/Hierarchy 面板点击或物理鼠标验收。恢复运行的项目会重新导入 OBJ，当前这条窗口测试日志会记录一次临时 VFS 产物暂存失败，但测试进程退出码为 0，Scene 与布局断言通过；跨显示器 DPI 和真实鼠标操作为可选补充验收。

随后扩展 `UrsaImGuiBridgeTests`：在从属 viewport 中打开 Editor command popup，并验证该浮动窗口的键盘状态被 Play 输入门控拦截；测试通过。搜索框文本捕获与焦点条件仍由 `UrsaPlaySessionTests` 的门控用例覆盖。该阶段尚未把 Project 面板资产拖放与正在运行的 Play 会话合并到同一窗口序列，因此任务 11.2 与人工双窗口检查仍保持未完成。

继续扩展 `UrsaImGuiBridgeTests` 后，测试通过从属窗口事件将字符送入聚焦的搜索字段，确认字段捕获键盘并阻断 Play；鼠标坐标和从属窗口的按下/释放事件点击命令按钮，打开 popup 并验证菜单状态也会阻断 Play。失焦用例覆盖浮动窗口键鼠释放，以及另一窗口仍持有同一按键时不被错误释放。为此 ImGuiBridge 现在按来源窗口聚合按键/鼠标状态与焦点，窗口失焦或销毁时只释放该窗口独占的输入。`UrsaImGuiBridgeTests` 与 `UrsaPlaySessionTests` Release 串行通过（含内容 fixture）。

随后扩展 `UrsaEditorWorkspaceWindowTests --cross-viewport-drag-only`：工作流先打开有效 Scene 并启动真实 Editor Play 会话，再将项目 AssetDatabase 中 OBJ 的稳定身份经活动 DragSession 拖入浮动 Game viewport。接收端用 `EditorInputBlocksPlay` 与 `SelectPlayInput` 检查拖拽期间的候选 Space 输入被清空；随后停止 Play，再执行 Model 放置、保存重开和布局恢复。该跨 viewport 工作流 Release 直接运行通过，相关断言覆盖运行状态和拖拽输入门控。连同浮动 viewport 搜索/菜单点击、Play 门控与失焦按键释放测试，任务 11.2 自动验收通过；事件由测试注入，未代替真实鼠标操作。该测试源/目标仍是工作流 fixture 控件，不是 Project 面板的实际图标拖拽；跨显示器与物理输入属于可选补充验收。

自动测试使用当前连接的单个显示器（`\\.\DISPLAY1`，1600×1000）和本机 Vulkan 适配器；Release 与 Debug 验证层配置的 RhiTests 均通过。本次同一显示器下的多窗口自动验收通过。未覆盖显示器热插拔、异构 GPU 多 surface 呈现队列及真实不同 DPI 双显示器拖动；这些属于可选扩展兼容性检查。

本轮曾尝试完整 166 项 CTest 的并行运行，但并行 Editor/GPU 应用争用桌面焦点，AssetPreviewPanel 出现视口不可见，导入与 Managed 编译用例在 sandbox 系统临时目录的原子替换返回 Windows error 5。AssetPreviewPanel 的视口尺寸失败已修正；随后变更相关窗口/Scene/引用/预览/文档/Play 测试串行 11/11 通过。将 TEMP、TMP、LOCALAPPDATA 指到仓库可写的 `Build/Temp` 后，之前受系统临时路径影响的 Managed Asset Preview、AssetImportPipeline、Editor Shader Import、Managed Editor Workflow、Scripted Importer Workflow 串行 5/5 通过；Generated Asset Player、Project Script Compile、Project Import Prepare 串行 3/3 通过（各组含 fixture）。完整 166 项 CTest 未串行重跑；物理鼠标操作可作补充人工检查，跨显示器 DPI 是可选扩展。
