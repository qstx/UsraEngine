# 运行时 GUI

运行时 OnGUI 已接入 Player 与 Editor Game，Demo 提供菜单和独立 OnGUI 测试场景。本文记录当前接口；自动与系统交互验收的覆盖边界见 [验收记录](RuntimeGuiValidation.md)，未取得证据的项目不计完成。

## 共享绘制器

`Rendering/Gui/ImGuiRenderer` 是 Engine 的通用 Dear ImGui/RHI 绘制器。它不创建窗口或 ImGui Context，不订阅输入，也不依赖 Editor、ImGuizmo、图标或停靠。创建时显式传入当前 VFS 和编译 Shader 路径；运行时资源必须来自当前内容会话。

调用方负责在适当的 Context 完成 ImGui 帧，再把显式 DrawData 传给 UpdateAtlas/Record。绘制器不会切换全局 Context；临时调用可用 ImGuiContextScope，作用域在异常路径也恢复原 Context。

Record 生成独立上传缓冲及资源保留令牌。调用方必须把 LastRecording() 返回的令牌交给 RenderFrameQueue::Retain，或自行持有到该次 GPU 提交的 fence 完成。令牌持有绘制所用的缓冲、管线、bind group、纹理与采样器，因此注销纹理或销毁绘制器不会使已经保留的提交资源提前释放。原有 Editor 仍在每次提交等待完成后继续下一帧。

编辑器 ImGuiBridge 保留平台浮窗、输入、布局、字体选择及图标管理，并委托共享绘制器处理 Shader、字体上传与绘制。ImGui 核心依赖位于公共 CMake 模块，ImGuizmo 只属于 Editor。

验证入口：UrsaImGuiRendererTests 覆盖两个独立 Context/纹理命名空间、异常恢复、字体与裁剪像素，以及先销毁绘制器再提交的资源有效期；UrsaImGuiBridgeTests 验证原有 Editor 图像、字体、DPI 和多 viewport 行为。

## 运行资源

运行时使用 `ursa:Shaders/Runtime/GUI.ursashaderasset` 与 `ursa:Fonts/Runtime/NotoSansSC-VF.ttf`。两者源资源具有新的 GUID，既有 Editor Shader/字体的 GUID 保持不变。字体许可位于同目录的 OFL.txt。Player 与 EditorBootstrap 的导入闭包包含 Runtime 路径；Player 仍排除 Editor 路径。

Canvas 从调用方提供的当前 VFS 读取已导入的资源。缺失字体、截断字体或无效 Shader 返回含资源路径的诊断，不回退源码目录，也不调用运行时 Shader 编译器。修改引擎 GUI Shader、字体或托管契约后，必须重新导入项目，再重新生成发布内容/Pak；旧的 Library/Pak 不会自动获得新契约。

UrsaGuiCanvasTests 使用只含编译 Shader 和字体的最小 Pak，验证中文、图片 alpha、分组裁剪、最终输出合成、Abort 和空 GUI pass。

## 事件与控件

`Behaviour.OnGUI()` 默认空实现，生命周期编号 0–7 保持原值，OnGUI 追加为 8。`Event.Current` 只在当前组件的 OnGUI 回调中有效；可以保存属性值作为日志，但保存的 Event 包装在下一次回调即过期，也不能用于另一运行会话或后台线程。

每个输入前先派发新的 Layout，再派发输入；模拟的 FixedUpdate/Update/LateUpdate 完成后，派发新的 Layout/Repaint。多个 GUI 事件不增加模拟或呈现次数。暂停期间仍运行 GUI，传给 GUI 的 DeltaTime 为零，模拟时间和帧号冻结。无输入时仍有 Layout/Repaint；没有有效 Game 相机或目标不可绘制时跳过 GUI。

`Event.Use()` 令当前事件变为 Used，后续组件观察到同一消费结果。RawType 保留原始事件类型。Layout/Repaint 不能消费，调用 Use 会保留类型并记录诊断。重叠控件按调用顺序消费输入；需要特殊遮挡顺序的项目应显式组织调用与 Use。

控件身份由会话、组件、ID 作用域、hint 和调用位置确定。Layout 与其关联输入/Repaint 必须保持同一控件结构。动态列表用 PushId/PopId 为项目条目建立稳定作用域，结构变化后调用 GUIUtility.ExitGUI；不要在 Layout/Repaint 无条件修改列表。

HotControl 是唯一指针捕获，KeyboardControl 是键盘焦点；Passive 控件不能取得键盘焦点。Tab/Shift+Tab 按 Layout 中 Keyboard 控件的顺序切换。GetTypeForControl 在事件属于其他捕获/焦点控件时返回 Ignore。删除控件或取消输入会清理对应捕获与焦点。

ExitGUI 是正常控制流：当前 GUI 事件终止，恢复组件组/ID 栈，不停用 Behaviour。输入中已完成的业务修改保留，事件不重放。Layout/Repaint 中退出会丢弃未完成绘制，最早下次调度周期恢复，不同步重试。不要用 catch(Exception) 吞掉 ExitGUIException；服务仍会拒绝继续当前 pass。真正的脚本异常、身份不一致和未配对作用域只隔离出错组件，其未完成绘制段被丢弃。

VS 调试中的 ExitGUI：它通过抛出 `Ursa.ExitGUIException` 展开项目回调栈，在引擎边界正常捕获。该边界使用 .NET 的 `DebuggerDisableUserUnhandledExceptions` 和禁止内联标记，避免“仅我的代码”将此正常退出报告为用户未处理异常；其他脚本异常继续向上传递。如果自行启用了此类型的“引发时中断”，VS 仍可在抛出时暂停，应只调整 `Ursa.ExitGUIException` 的异常设置。不要删除 ExitGUI，也不要在项目 OnGUI 中吞掉它。机制参见 [Microsoft 调试器属性文档](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerdisableuserunhandledexceptionsattribute?view=net-10.0)。

首版提供 Label、Button、Toggle、HorizontalSlider、TextField、DrawTexture、ViewportSize、BeginGroup/EndGroup、PushId/PopId 和 GUI.Changed。只在 Repaint 生成绘制，按钮和编辑操作只在对应输入中返回变化。坐标以目标像素为单位，左上为原点；分组内为局部坐标，Event.MousePosition 随组原点转换，绘制与命中同时遵守嵌套裁剪。未提供 GUILayout 自动布局。

TextField 接收完整已提交的 Unicode 字符串；物理 KeyDown 不再重复插入字符。Shift 加方向/Home/End 支持选区，Backspace/Delete 不拆分 UTF-8 标量。Copy/Cut/Paste/SelectAll 经 ValidateCommand/ExecuteCommand：Validate 不触碰文本或剪贴板，Execute 执行一次并消费事件。系统剪贴板属于承载 Game 的 Window。

Unicode 编辑与字体覆盖是两个独立条件。内置 Noto Sans SC 覆盖示例中文，未包含的字符使用字体缺字字形；例如辅助平面 emoji 的值可完整编辑和保存，但不保证显示彩色或对应字形。GUI 接收窗口已提交字符，输入法候选窗及组合文本仍由系统处理。

## 自定义控件示例

```csharp
public override void OnGUI()
{
    Rect area = new(20, 20, 160, 32);
    int id = GUIUtility.GetControlId(42, FocusType.Passive, area);
    Event e = Event.Current;
    if (e.GetTypeForControl(id) == EventType.MouseDown &&
        e.Button == 0 && area.Contains(e.MousePosition))
    {
        GUIUtility.HotControl = id;
        e.Use();
    }
    else if (e.GetTypeForControl(id) == EventType.MouseUp &&
             GUIUtility.HotControl == id && e.Button == 0)
    {
        GUIUtility.HotControl = 0;
        e.Use();
    }
    GUI.Label(area, "自定义捕获区域");
}
```

GUI 消费发生在模拟之前，过滤后的 FrameContext/Input 对应设备/按键不可用；鼠标捕获不阻断无关游戏键盘。Window.KeyDown/MouseDown 是原始物理轮询，不属于该过滤接口。隐藏、失焦、Editor 抢占和 Stop 会取消 GUI 捕获；恢复时同步物理前态，避免把仍按住的输入当作新的激活。

## 最终输出

主 Game 输出由宿主附加一次性会话/序号令牌。RenderCommandExecutor 仅在正常 End 中，在场景及后处理之后加载最终颜色并混合 GUI，再执行 FrameCapture、最终状态转换和原有一次提交/呈现。Scene/Preview、单独相机 RenderTexture 和没有宿主令牌的额外上下文不会合成 GUI。Abort 不进入 GUI 合成。没有 GUI 绘制命令是正常空 pass，不是渲染失败。

运行时 Canvas 拥有独立 ImGui Context；Layout/输入阶段不调用 Dear ImGui 控件或 NewFrame。最终 Repaint 使用 ImDrawList 生成文字、矩形、纹理和裁剪，不向 Dear ImGui 再发送原始输入。提交保留绘制资源与 Canvas 到实际 fence 完成，resize/Stop 不使用 CPU 帧号代替 GPU 完成。
