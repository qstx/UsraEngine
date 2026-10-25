# 引用字段与对象选择器验收

对象选择器从序列化引用字段旁的按钮打开，作为字段附近的搜索弹出层呈现（按已确认的 Unity 风格交互，不创建独立平台窗口），提供名称/类型/来源路径搜索、Assets 与 Scene 来源过滤、None、键盘导航和稳定身份信息。候选区域显示对应资源的只读 Preview；预览候选与 Inspector 页脚分别拥有状态、内容、相机和 GPU 目标。弹出层跟随其父 Inspector viewport，可在主窗口或浮动 Inspector 中使用。拖放到字段标签或值上也会经过字段类型兼容性检查。

## 试选、接受和取消

`UrsaReferencePickerTests` 使用真实 ImGui 上下文和逐帧键鼠事件驱动弹窗。单目标回归打开字段、用方向键试选 Beta，再按 Esc；目标先变为 Beta，取消后恢复 `asset:original`。多目标回归从两个不同原引用开始，导航到同一候选并确认两个目标同时实时变为 Beta；Esc 后两个各自恢复原引用。测试还确认取消不清除独立 Dirty、不改 Prefab 覆盖；Enter 与正常关闭接受当前候选；搜索框优先响应 Esc；另一编辑器区域点击、候选移除、字段切换及边界操作会先恢复目标再继续。

`UrsaAssetPreviewPanelTests` 在真实 Editor 面板和 GPU 设备上打开材质候选 Preview。断言页脚 Preview 内容与目标保持原样、选择器创建自己的 GPU 目标；取消后注册纹理回到基线，源材质字节不变，游戏生命周期探针为零。该测试同时覆盖 UI 已提交图像时的安全退休路径。其它 Preview 内容的生命周期及作者源不变检查见 [Inspector 资产预览验收](InspectorAssetPreviewAcceptance.md)。

```powershell
cmake --build Engine/Build/Native --config Release --target UrsaReferencePickerTests UrsaAssetPreviewPanelTests
$env:URSA_CONTENT_PROJECT = (Resolve-Path Engine/Build/TestProjects/Runtime/Release).Path
ctest --test-dir Engine/Build/Native -C Release -R '^Ursa(ReferencePicker|AssetPreviewPanel)Tests$' --output-on-failure
```

上述用例验证弹窗交互和实际 GPU 候选画面，不替代人工检查具体项目扩展的视觉内容。候选没有可用 Preview 时保留身份信息并显示无预览状态；失败或切换不会继续显示前一候选的画面。取消和浏览均不触发源资产保存，也不调用 Awake、Start、Update 等游戏生命周期。
