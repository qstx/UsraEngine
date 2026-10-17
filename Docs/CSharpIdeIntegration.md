# C# 工程与外部编辑器

## 工程生成

启用 `URSA_ENABLE_DOTNET` 后，Editor 的正式脚本构建与 IDE 工程使用同一份输入快照：Assets 源码路径、`.meta` GUID、Game/Editor 分组、框架引用，以及 .NET 10 / C# 14 编译设置。收集快照不执行脚本语义分析，也不要求已经有 Game DLL；语法错误、缺失类型或重复脚本类型标识不会阻止 IDE 工程生成，仍由正式构建报告并拒绝发布无效内容。

可以先用 Engine 工具生成 IDE 工作区：

```powershell
Engine/Build/Release/UrsaScriptCompile.exe Demo --ide
dotnet build Demo/Library/ScriptIDE/Ursa.Scripts.sln --configuration Debug --disable-build-servers -m:1 -nr:false
```

生成目录为项目的 `Library/ScriptIDE`：

- `Ursa.Scripts.sln`：Game 与 Editor 两个标准 SDK 风格 C# 工程。
- `Projects/Game/Ursa.Game.csproj`：游戏脚本及运行时/渲染 API。
- `Projects/Editor/Ursa.EditorScripts.csproj`：Editor 目录脚本，项目引用指向 Game 工程，便于导航到游戏源码。
- `Ursa.Scripts.code-workspace`：VS Code 工作区。
- `Build`：IDE 构建输出与中间文件，按工程和配置隔离。
- `GenerationState.json`：完整生成且输入仍有效后记录的结构身份。

工程通过相对路径引用 Assets 中的原始 `.cs`，并显示对应的 Assets 层级，不复制源码。没有脚本时也生成有效的空工程；删除 Library 后可以重新生成。生成过程不更改已有 `.meta` GUID。

## 结构同步与正式编译

结构指纹包含源码成员、路径、GUID、分组、框架内容身份和编译设置。仅修改方法体时不必重写 IDE 工程；新增、删除、改名、跨 Editor 目录移动或框架配置变化会更新相应内容。生成文件按内容比较并原子替换，写入失败或输入过时时不标记为最新；可修复问题后重试。

正式构建继续使用包含源码内容、绑定要求、工具和配置的构建指纹。IDE 自行构建只写入 `Library/ScriptIDE/Build`，不会发布 `Library/ScriptBuild` 的类型目录、程序集、内容清单或重载状态。进入 Run 仍由 Editor 检查正式输入最新性并验证候选内容；IDE 构建成功不等于已经完成 Editor 编译和发布。

正式 Debug 工程显式使用 Portable PDB 并关闭脚本优化，符号文档保留本机源码路径；诊断显示使用的虚拟路径与调试器的源码查找是两个独立用途。

## Editor 打开与定位

1. 打开 **Edit > External Tools**，在 **External Script Editor** 中选择检测到的 Visual Studio、Rider 或 VS Code。也可以选 **Custom executable**，填写工具的 `.exe` 绝对路径。
2. 点击 **Save preferences** 保存。本机偏好位于用户数据目录下的 `Ursa/Editor/ExternalTools.json`，不写入项目资产或 ProjectSettings，删除 Library 不会清除选择。测试环境可通过既有 `URSA_USER_DATA_ROOT` 隔离该目录。
3. 使用 **Scripts > Open C# Project** 打开工作区。**Scripts > Regenerate C# Project** 可以只重新生成，不启动 IDE。Project 中双击 `.cs` 也会先同步工程，再打开原始脚本。

初次没有选择 IDE、工具已经卸载或启动失败时，External Tools 会显示错误，已生成的工程仍可使用。选择不会静默切换到其他工具。**Refresh installed editors** 可以重新检测；启动和 Visual Studio 定位均在后台执行。

自定义工具的参数使用 JSON 字符串数组。工程参数示例是 `["{solution}"]`，源码参数可以设置为 `["{solution}", "{file}", "{line}", "{column}"]`；具体选项顺序由该工具决定。支持 `{workspace}` 和 `{projectRoot}`，不支持任意环境变量或 shell 命令替换。每个数组元素作为一个独立参数，包含空格或中文的路径不需要手工增加引号。工具必须配置相应参数才能接收行列位置。

Visual Studio 的文件定位通过 Engine 提供的 `UrsaIdeBridge.exe` 选择对应解决方案实例，再打开文件并设置行列；辅助进程有超时限制，超时不会关闭 IDE。Rider 使用行列参数，VS Code 使用工作区和 `--goto`。SDK、IDE 及所需 C# 支持由作者安装。

当前工程目标为 .NET 10 / C# 14。Visual Studio 的完整工程编辑支持要求 Visual Studio 2026（18.0+）；不同 SDK feature band 的最低工具版本请参考 [Microsoft 版本兼容表](https://learn.microsoft.com/en-us/dotnet/core/porting/versioning-sdk-msbuild-vs)。本机 VS 2022 17.13.6 已通过文件定位和真实 CoreCLR 调试，但补全测试失败，不能据此视为兼容的工程编辑环境。

## 从编译诊断打开源码

点击工具栏 **Script Build** 查看错误和警告，再点击带源码位置的诊断条目。诊断来自类型目录工具的结构化记录或编译器错误日志，包含虚拟路径、行列、严重性、代码和消息。原始输出仍可在 **Build status and tool output** 展开查看。

例如 `prj:Probe.cs` 的语法错误可以直接定位原文件；缺失类型和重复稳定脚本标识也不会阻止工程重新生成。工具未启动、缺少引用等无位置错误仍显示文本。文件已删除或路径不属于当前项目源码范围时，会显示定位失败，不创建空文件，不打开消息中恰好出现的其他路径。

Run 中可以同步 IDE 工程结构，但当前运行继续使用原程序集。Stop 后仍需由 Editor 成功编译、校验和重载；错误定位或 IDE 构建不会绕过这一流程。

## 本地脚本调试

External Tools 显示当前 Editor PID、项目、CoreCLR 是否已经初始化，以及正式 Game/Editor PDB 的可用状态；可用 **Copy debug information** 复制。如果尚未初始化 CoreCLR，先让有效脚本完成 Editor 编译和环境加载。

使用兼容当前 .NET 运行时的托管调试器附加该 PID。在游戏回调中设置断点后点击 Play；在项目 Editor 菜单或 Inspector 回调中设置断点后触发对应工具。调试的程序集来自 Editor 的正式内容清单，不是 IDE Build 输出。

已验收的版本切换流程是：在 Run 中修改源码，确认运行中的脚本仍使用原版本；先从 IDE 分离调试器，再 Stop，等待正式编译、内容校验和旧会话卸载成功；重新附加当前 PID，确认断点使用当前符号，再触发 Editor 菜单或 Play。未保存的序列化字段由原有重载流程恢复。这次验收没有证明保持调试器附加时可无缝切换版本。

断点空心或位置不符时，核对 External Tools 中的项目和 PID、正式 Debug 构建是否成功、IDE 是否加载 `Library/ScriptBuild` 当前清单对应的 DLL/PDB，以及源码是否在上次正式编译后发生变化。不要将 IDE Build 的符号手动替换进正式目录。Portable PDB 的内容指纹及其与程序集的调试身份都会检查，不匹配的符号不能作为当前脚本符号加载。若旧会话无法卸载，按 Editor 诊断分离或重新启动会话，不能绕过卸载检查。

Engine 提供可复用的真实调试验收工具，要求已安装并可使用的 Visual Studio 和 .NET SDK。它创建独立临时项目、仅附加自己启动的验收进程，在 Game `Awake` 和 Editor 菜单中执行断点、单步、局部变量与版本切换检查，最后分离并关闭验收宿主；会在 IDE 打开该临时解决方案：

```powershell
cmake --build Engine/Build --config Release --target UrsaManagedDebugAcceptance -- /m:1 /nr:false
Engine/Tests/Editor/Scripting/RunManagedDebugAcceptance.ps1 -BuildDirectory Engine/Build -VisualStudioInstallDirectory 'E:/Microsoft Visual Studio/2022/Professional'
```

证据写入 `Engine/Build/IdeAcceptance/managed-debug-evidence.json`，包括源文件行号、调用栈、局部变量、正式构建指纹、PDB SHA-256 和重载后的字段状态。该工具不代替 IDE 补全和跨工程导航验收。

真实 IDE 的支持矩阵及回归结果记录在 [C# IDE 集成验收记录](CSharpIdeIntegrationEvidence.md)；OpenSpec `csharp-ide-integration/tasks.md` 区分已完成与待验证项。生成工程或 Portable PDB 本身不代表已经完成全部 IDE 和断点验收。
