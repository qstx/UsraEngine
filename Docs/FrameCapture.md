# 临时渲染画面捕获接口

为渲染管线迁移的基线和验收提供无遮挡图片。`Application::SetFrameCapture` 和
`EditorApplication::SetFrameCapture` 配置一次性捕获；CLI 使用 `--capture` 指定 PNG 输出，
`--capture-frame` 指定第几个实际渲染帧（从 1 开始，默认 1）。

```powershell
Engine/Build/Debug/UrsaScriptPlayer.exe --project Demo --scene MaterialShowcase.ursascene --hidden --frames 30 --capture Engine/Build/SrpBaseline/MaterialShowcaseRendered.png --capture-frame 30
Engine/Build/Editor/Debug/UrsaEditor.exe --project Demo --scene Snake/Snake.ursascene --hidden --frames 30 --capture Engine/Build/SrpBaseline/EditorRendered.png --capture-frame 30
```

图片来自呈现前的交换链纹理，包含整个客户区和 Editor 的 ImGui 面板，不包含操作系统标题栏、
鼠标或桌面弹窗。像素尺寸等于实际 framebuffer 尺寸；窗口被遮挡不影响捕获。
最小化而没有渲染的帧不计数。隐藏窗口仍会渲染，可用于无人值守验收。
Editor 启动导入进度不计入捕获帧。`--asset` 可打开材质等资产，`--view Scene|Game|"Material Preview"`
选择初始面板，`--play` 请求进入 Run；使用该选项时，捕获及 `--frames` 从 Run 激活后计数。
完整基线脚本、配置与证据索引见 [ScriptableRenderingBaseline.md](ScriptableRenderingBaseline.md)。

捕获会增加一次 GPU 读回和等待，因此该帧不能作为正常帧性能样本。
输出通过独立 VFS 挂载原子写入；支持 RGBA8/BGRA8，BGRA 会转换通道并保存为不透明 PNG。
设备的 surface 不支持 TransferSource 或退出前未渲染到指定帧时返回明确错误。
该接口用于诊断，不能代替真实界面交互验收。
