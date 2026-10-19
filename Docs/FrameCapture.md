# 窗口画面捕获

## 运行期间按需获取

正常运行的 Editor 和 Player 会自动注册本地捕获实例，不需要为截图重新启动或预设帧号。
外部脚本和 AI 工具可以针对 PID 重复调用：

```powershell
Engine/Build/Release/UrsaCapture.exe --pid 12345 --output "Engine/Build/Captures/当前界面.png"
Engine/Build/Release/UrsaCapture.exe --pid 12345 --output "Engine/Build/Captures/第二次.png" --timeout-ms 10000
```

使用实际目标 PID 替换示例中的 12345。相对输出路径相对于调用者当前目录解释。
命令在目标主线程接收请求后获取下一幅完整渲染画面，保存成功后才返回；不需要关闭目标。
默认整体超时为 5000 ms，`--timeout-ms` 必须为正整数，覆盖连接、等待画面和等待结果。
每个实例同时容纳一个待完成请求，并发超出容量会返回 `Busy`。

stdout 是单个 UTF-8 JSON 结果，诊断写入 stderr。只有 `Status=Success` 的退出码为 0。
结果包含 `Version`、`Pid`、`SessionId`、`RequestId`、`Status`、`Message`、`Output` 和
`DeadlineUtc`；成功时额外包含 `Width`、`Height`、`FrameSerial` 和 `CapturedAtUtc`。
时间字段是 UTC Unix 毫秒；FrameSerial 是该运行实例完成的画面序号，不是调用方需要提供的帧号。
连接前失败时，无法确定的实例字段为 null。只有匹配的成功回执表示本次请求完成，不能凭旧 PNG 已存在判断成功。

| 状态 | 含义 |
|---|---|
| `Success` | PNG 已完整提交，返回实际路径、像素尺寸和捕获信息 |
| `InvalidRequest` | 参数、消息或请求身份不合法 |
| `TargetUnavailable` | 没有该目标，或 PID 对应的运行实例已经变化 |
| `NotReady` | 窗口尚未进入可捕获的正常渲染阶段 |
| `Busy` | 该实例已有一个待完成请求 |
| `Timeout` | 整体期限到达，或请求已取消/过期 |
| `TargetClosed` | 目标已关闭捕获通道 |
| `UnsupportedProtocol` | 控制协议版本不兼容 |
| `CaptureFailed` | 图像读回或回执验证失败 |
| `WriteFailed` | 输出文件无法完整写入 |

窗口最小化而不再绘制时，请求等待恢复；期限内未恢复则命令超时。Editor 内 Pause 仍会绘制界面，
截图不会恢复游戏或增加游戏更新。调试器挂起主线程时不保证获得图片，客户端仍会有界超时。
按需截图或写入失败只影响该请求，正常目标继续运行，后续可重试。

### 控制目录与测试隔离

控制目录使用 `ResolveUserDataDirectory("UrsaCapture")`，与项目 Assets、Library 分离。
Windows 默认位于 `%LOCALAPPDATA%/UrsaCapture`；设置 `URSA_USER_DATA_ROOT` 时为该根下的
`UrsaCapture`。macOS 和 Linux 遵循现有用户数据目录规则。
客户端和目标必须使用相同的目录环境。Windows 测试可在启动两个进程前共同设置：

```powershell
$env:URSA_USER_DATA_ROOT = (Resolve-Path Engine/Build/Temp).Path
```

`instances/<pid>.json` 登记实例身份及 ready 状态；
`sessions/<pid>/<SessionId>/inbox`、`replies`、`cancel` 保存有界的控制记录。
这些文件由 VFS 原子发布，单条控制消息上限 64 KiB。主线程限频检查请求，只有在完整窗口画面完成后才处理图像；
没有请求时不读回 GPU 或保存 PNG。实例身份与请求标识防止复用旧结果，控制记录不是图片历史缓存。

### 嵌入窗口宿主

`Rendering::FrameCapture` 分开维护按帧任务与按需请求。宿主创建窗口后调用
`StartWindowCapture(application)`，进入正常渲染后调用 `ReadyWindowCapture()`；
每轮在构建画面前调用 `PollWindowCapture()`，在完整提交之后、Present 之前调用 `Capture(...)`。
关闭时先 `StopWindowCapture()`，再销毁设备与窗口。Editor 和 Player 已自动接入这些阶段。

`Capture` 的 `countScheduled` 参数只控制旧按帧计数；为 false 时仍能服务按需请求。
相同画面同时满足两个任务时，共享一次读回和编码。`ReadbackCount()` 报告实际读回尝试数。
`FrameImage.hpp` 的 `ReadFrameImage`/`WriteFrameImage` 提供可单独复用的图像操作。

图像工作在窗口主线程进行，没有保存历史图像的后台线程。已经开始的 GPU 等待不会被强制中断，
发布结果前会复查期限。客户端超时不承诺撤销已经开始的文件原子提交；请求成功须以匹配回执确认。
旧按帧任务的 `Finish()` 仍只检查原任务是否完成，不会因一个按需请求失败而报错。

## 按指定渲染帧捕获

为渲染管线迁移的基线和验收提供无遮挡图片。`Application::SetFrameCapture` 和
`EditorApplication::SetFrameCapture` 配置一次性捕获；CLI 使用 `--capture` 指定 PNG 输出，
`--capture-frame` 指定第几个实际渲染帧（从 1 开始，默认 1）。

```powershell
Engine/Build/Debug/UrsaScriptPlayer.exe --project Demo --scene Snake/Snake.ursascene --hidden --frames 30 --capture Engine/Build/SrpBaseline/SnakeRendered.png --capture-frame 30
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
