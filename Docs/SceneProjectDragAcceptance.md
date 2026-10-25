# Scene 与 Project 拖放验收

2026-10-25 Release 验收覆盖 Scene 画面落点和 Scene 子树导出 Prefab。

`UrsaViewportLayoutTests` 用相机和合成可见边界验证屏幕像素到射线的映射、最近几何落点、目标平面回退以及画面留边拒绝。`UrsaAssetPreviewPanelTests` 通过真实 ImGui 多帧拖动发送 Project DragSession 到 Scene 图像，确认回调收到有限世界坐标和两个已选来源；拖到 Scene 工具栏或 Game 画面时回调不执行。该测试还从真实 ImGui 拖放源向 Project 内容窗口投递两个 Scene 根，确认 Project 收到一次有序的双根导出请求。测试中的生命周期探针在全部交互后仍为零。

`UrsaSceneDraftTests` 捕获 Scene 子树为新 Prefab，重新实例化后保存并重开，验证 Prefab/object 身份变化、子树结构和本地 Material 引用保留，同时源 Scene 根数及对象身份不变。`UrsaEditorHierarchyTests` 使用带跨根 GameObject 引用的选择验证独立根捕获失败；整批导出在应用层先准备全部根，捕获失败时删除新建占位文件与 sidecar。

`UrsaSceneDraftTests` 还在两个真实 Prefab 实例的公共 MeshRenderer 上创建 `SerializedObjectGroup`，先确认 `castShadows` 混合值，再一次提交到全部目标；同一测试随后打开多帧引用选择器，对两实例试选并按 Esc 分别恢复原引用，再以多帧 ImGui 鼠标事件从 GameObject 拖动源释放到引用字段落点。落点通过统一 `AcceptEditorDragDropPayload` 领取一次，解析文档/对象身份并写入两个序列化引用字段。记录 Prefab 覆盖、保存重开后，测试检查两个已编辑实例、未选中的第三实例、Prefab 源资源和拖入引用的稳定身份。`UrsaAssetPreviewPanelTests` 另通过 Project 行和 ImGui 帧验证 Mesh/Material 字段接受正确资产并拒绝错误类型。组合回归覆盖完整的模型级工作流；真实原生双窗口和物理鼠标操作仍留给 11.3/12.2 的窗口验收。

DragSession 另有一次性投递领取：Hierarchy、Project 移动/导出、Scene 放置和统一引用字段在提交操作前，以会话 token 和选择 revision 原子领取。`UrsaEditorDragSessionTests` 验证过期 revision 不能领取，且同一有效 token 的第二次领取失败。此为自动会话级回归，不替代真实跨窗口释放操作验收。

验证命令：

```powershell
cmake --build Engine/Build --config Release --target UrsaEditorCore UrsaViewportLayoutTests UrsaSceneDraftTests UrsaEditorHierarchyTests UrsaAssetPreviewPanelTests
ctest --test-dir Engine/Build -C Release --output-on-failure -R 'Ursa(AssetPreviewPanel|EditorHierarchy|SceneDraft|ViewportLayout)Tests'
```

当前测试验证 Project 面板的实际目标交付和 Prefab 捕获/保存逻辑，但还没有对真实 EditorApplication 的多文件导出执行中途 VFS 故障注入；该恢复路径通过批次占位清理实现，后续集成验收应覆盖故障注入。
