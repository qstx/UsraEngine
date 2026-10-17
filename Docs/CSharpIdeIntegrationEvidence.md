# C# IDE 集成验收记录

验证日期：2026-10-16 至 2026-10-17。`csharp-ide-integration` 的 28 项任务已完成。下文分别记录自动回归、实际 IDE 操作及用户手工验收结果。

## 已通过

- Windows x64、.NET SDK 10.0.100 环境下，Release 原生 Editor、脚本编译工具、IDE 工程与外部工具测试目标构建通过。
- `UrsaScriptIdeProjectTests`：空项目、无 Game DLL、错误源码、成员增删、跨组移动及 GUID、方法体修改不重写工程、框架身份变化、生成前后输入过时、写入故障与重试、Library 重建；同时验证中文路径及 SARIF 1/2 的 URI、行列与非源码诊断映射。
- `UrsaExternalScriptEditorTests`：偏好保存/恢复与无效配置、注入的发现结果去重与失效过滤、三种 IDE 的参数协议、假启动器、真实参数回显子进程（空格、中文、引号、末尾反斜杠及 shell 字符）、辅助进程超时和缺失工具；验证未初始化宿主的调试状态。
- `UrsaScriptBuildTests`：正式编译、缓存、绑定身份校验与失败安全继续通过；重复稳定类型标识、缺失类型及语法错误时仍可生成 IDE 工程；编译器警告和类型目录错误提供结构化位置。实际构建改变输入后的 IDE 解决方案，验证正式 Game DLL 字节不变、正式输入仍被判定过时。
- `UrsaManagedEditorWorkflowTests`：真实 Editor 渲染/后台编译、错误源码下重新生成和定位请求、无 IDE 选择时的恢复入口、项目切换保护、生成过程中关闭、字段重载与三轮 Run/Stop 通过。Run 中新增源码可进入 IDE 工程，活动脚本版本保持到 Stop 后重载。最新工作流 Release 耗时 66.44 秒，Debug 耗时 65.95 秒。
- `UrsaManagedProtocolTests`：增加 Portable PDB 与程序集调试身份匹配检查；验证可解析的源码文档、缺失符号和身份不匹配符号被拒绝，既有协议测试通过。
- Demo 的 IDE 解决方案使用 `dotnet build ... --configuration Debug/Release --disable-build-servers -m:1 -nr:false` 实际构建成功，均为 0 警告、0 错误。Release 构建前后比较正式程序集、清单、所有项目脚本与 `.meta` 的 SHA-256，全部保持不变。
- Visual Studio Professional 2022 17.13.6 实际通过辅助进程打开 Demo 工作区中的 `MoveWithInputBehaviour.cs`，并通过 IDE 自动化读取回当前位置，验证为第 10 行、第 5 列。日志位于本机构建输出 `Build/IdeAcceptance/vs-navigation.log`。独立的 CoreCLR 调试结果见下文；该版本的 C# 14 工程补全测试未通过。

## 真实 CoreCLR 调试

使用 Visual Studio Professional 2022 **17.13.6**，调试类型 **Managed (.NET Core, .NET 5+)**，引擎 `{2E36F1D4-B23C-435D-AB41-18E608940038}`。验收程序通过安装的 VS SDK/EnvDTE 操作实际调试器，附加新启动的原生 Editor 测试宿主 **PID 3972**。

本机原始记录为 `Build/IdeAcceptance/managed-debug-evidence.json`，测试项目为 `Build/IdeAcceptance/DebugRun-5fbd43ab5a604d8c9b55aa044662ca08`。工程由正式脚本构建发布 Debug DLL/Portable PDB，调试器按 PDB 文档打开 Assets 原源码。每次先命中断点，执行 StepOver，再求值局部变量：

| 脚本位置 | 回调 | 旧版本局部变量 | 重载后局部变量 |
| --- | --- | --- | --- |
| `Assets/DebugProbe.cs:11` → 单步到 12 | `DebugAcceptance.DebugProbe.Awake` | `runtimeMarker = 10` | `runtimeMarker = 11` |
| `Assets/Editor/EditorProbe.cs:9` → 单步到 10 | `DebugAcceptance.EditorProbe.Pulse` | `editorMarker = 41` | `editorMarker = 42` |

Game 调用栈包含 `BehaviourRegistry.Invoke`、`HostCallbacks.InvokeBehaviour` 和生成的绑定回调；Editor 调用栈包含 `ExtensionRegistry.Invoke`、`ScriptSessions.InvokeExtension`、`HostCallbacks.InvokeEditorExtension` 和生成的绑定回调。四次局部变量求值均为 `Valid: true`。

Run 中修改源码后，正式 Game 构建指纹仍为 `0ed127207cf6c46f`。分离调试器、Stop 并等待正式编译和卸载检查完成后，新指纹为 `d35740e4cb79bea6`，重新附加后两处断点再次命中。没有跳过旧会话卸载检查，也没有将 IDE Build 输出作为正式内容加载。Game PDB SHA-256 从 `9B727AAC93E8D9045627B1BA993ACEECDBC5CC61855BD87A1620B6CC3C66279C` 变为 `6556705D9EA585F1E816F71B7147656935CC2B1177B7A90DA655994EB2E81392`。

重载状态保留未保存字段 `value = 9`、`Dirty = true`，源 GUID 为 `0970db11-24d7-4536-9960-9190fc30e4ad`。调试信息显示当前项目、实际 PID、CoreCLR initialized、正式 Debug/Portable PDB 和 Game/Editor 符号可用。测试最终分离调试器、Stop、关闭自己启动的宿主，退出码为 0。验收使用分离/重新附加流程，没有宣称保持附加时能无缝重载。

## IDE 编辑支持与验收

| 工具 | 实际安装/版本 | 已验证 | 尚未通过 |
| --- | --- | --- | --- |
| Visual Studio | Professional 2022 17.13.6 | Demo 工作区打开、文件行列定位、Game/Editor CoreCLR 调试及重新附加后的新版本断点 | .NET 10 / C# 14 工程补全未通过，不能作为完整编辑验收 |
| Rider | 2026.2.1 | 安装发现、参数适配、Demo 双工程加载、Engine API 补全、Editor → Game 源码导航；用户手工确认工程打开、双击脚本、错误行列定位及含错时仍可打开全部通过 | 本次编辑验收通过；CoreCLR 调试验收基线为 Visual Studio，未单独验收 Rider 调试器 |
| VS Code | 未检测到安装 | 工作区和 `--goto` 参数适配测试 | 真实 IDE 验收未执行 |

[Microsoft 官方版本规则](https://learn.microsoft.com/en-us/dotnet/core/porting/versioning-sdk-msbuild-vs)要求 Visual Studio 18.0+ 才正式支持目标 `net10.0`；本机 VS 17.13.6 的调试成功不能证明其工程编辑兼容。

2026-10-17，代理在 Rider 2026.2.1 中实际打开 Demo 的 `Library/ScriptIDE/Ursa.Scripts.sln`，确认 `Ursa.Game` 与 `Ursa.EditorScripts` 均加载并显示 Assets 层级。在 `MoveWithInputBehaviour.cs` 中输入 `Transform.LocalPosit`，按当前 Visual Studio 键位方案的 Ctrl+J，显示 `LocalPosition` 候选及 `public Vector3 LocalPosition { get; set; }` 签名；接受补全后恢复原表达式。随后在 Editor Inspector 脚本加入临时 `typeof(MoveWithInputBehaviour)` 引用，按 F12 跳至 Game 原源码第 4 行的类型定义。临时探针移除并保存后，46 个 `.cs`/`.meta` 文件的 SHA-256 与验收前一致；本机哈希基线位于 `Build/IdeAcceptance/rider-source-before.json`。

外部启动链路的早期尝试曾遇到权限拒绝及 Rider `DirectoryLock.CannotActivateException` / `SocketException: Invalid argument: bind`；这些记录属于 Rider 启动/实例通信阶段，不能归为 C# 编译错误。没有通过重置 IDE 设置或跳过安全确认处理；首次解决方案信任由用户手动确认。具体启动失败诱因未确定，不记录为已修复的代码缺陷。

用户随后接手手工验收，并于 2026-10-17 明确确认“这些步骤我都验证好了，没有问题”。该确认覆盖以下完整步骤，作为 OpenSpec **6.1 完成**的手工验收依据：

- 在 External Tools 选择并保存 Rider，使用 Scripts → Open C# Project 打开 Demo 双工程解决方案。
- 从 Ursa Project 双击 Game 与 Editor 脚本，打开 Assets 原文件。
- 验证 Engine API 成员补全及 Editor → Game 类型定义跳转，移除临时导航字段。
- 将一处成员访问临时改为不存在的成员并编译，点击 Script Build 的带位置错误，确认 Rider 定位到相应源码行列。
- 保持源码错误时，双击脚本及 Open C# Project 仍可打开工作区。
- 恢复源码和临时字段后重新编译成功。

代理直接观察到的补全/导航结果与用户报告的完整链路结果分别归属上述来源，没有将用户手工验收描述为自动测试。

## 构建与回归矩阵

托管 ON 的 Release 使用 `Build`，Debug 使用 `Build/BindingsFinalVerify`；托管 OFF 使用 `Build/SrpNativeConfiguration`。OFF 测试将 PATH 中 dotnet、WindowsApps、JetBrains 和 VS Code 入口移除，并覆盖未选择 IDE 与托管关闭状态，证明原生 Editor 不要求这些工具可调用；这不等于卸载了本机 IDE。

| 托管 | 配置 | 构建结果 | 最新测试结果 |
| --- | --- | --- | --- |
| ON | Release | Editor、IDE 服务、脚本构建/工作流与宿主测试目标成功 | IDE 工程、外部编辑器、脚本构建、Editor 工作流、托管协议 5/5 通过（125.75 秒）；宿主内容加载与 VFS 另行通过 |
| ON | Debug | 同上，使用 VS 附带的 CMake 3.30 | IDE 工程、外部编辑器、脚本构建、Editor 工作流、宿主内容加载与托管协议及内容准备夹具 7/7 通过（137.73 秒）；VFS 另行通过 |
| OFF | Release | Editor 和两项原生 IDE 服务测试目标成功 | 2/2 通过（0.94 秒） |
| OFF | Debug | 同上 | 2/2 通过（0.95 秒） |

`UrsaDotNetHostSmoke` 覆盖内容指纹、绑定模块身份、会话隔离与卸载失败保护；Debug/Release 均通过。`UrsaVirtualFileSystemTests` 的 Debug/Release 均通过，中文路径则由两种配置下的 IDE 工程测试覆盖。VFS 测试夹具需要 Windows junction，沙箱内不能创建，因此在沙箱外运行；`TEMP/TMP` 使用磁盘实际目录 `Temp` 的原始大小写，避免既有测试对返回根路径的字面比较失败。

clang-format **19.1.1** 检查 474 个第一方 C++ 文件通过，额外检查 `Tools/IdeBridge/Main.cpp` 通过；`openspec validate csharp-ide-integration --strict` 通过。未格式化 ThirdParty、生成目录或构建输出。

## 可复用命令

```powershell
cmake --build Engine/Build --config Release --target UrsaScriptIdeProjectTests UrsaExternalScriptEditorTests UrsaScriptBuildTests UrsaManagedEditorWorkflowTests -- /m:1 /nr:false
ctest --test-dir Engine/Build -C Release -R '^(UrsaScriptIdeProjectTests|UrsaExternalScriptEditorTests|UrsaScriptBuildTests|UrsaManagedEditorWorkflowTests|UrsaManagedProtocolTests)$' --output-on-failure
Engine/Build/Release/UrsaScriptCompile.exe Demo --ide
dotnet build Demo/Library/ScriptIDE/Ursa.Scripts.sln --configuration Debug --disable-build-servers -m:1 -nr:false
```

Debug 构建目录使用本机 VS 附带的 CMake；OFF 的 Debug/Release 采用同样的单节点命令：

```powershell
$cmake = 'E:/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake --build Engine/Build/BindingsFinalVerify --config Debug --target UrsaEditor UrsaScriptIdeProjectTests UrsaExternalScriptEditorTests UrsaScriptBuildTests UrsaManagedEditorWorkflowTests UrsaDotNetHostTests -- /m:1 /nr:false
ctest --test-dir Engine/Build/BindingsFinalVerify -C Debug -R '^(UrsaScriptIdeProjectTests|UrsaExternalScriptEditorTests|UrsaScriptBuildTests|UrsaManagedEditorWorkflowTests|UrsaManagedProtocolTests|UrsaDotNetHostSmoke)$' --output-on-failure

$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'dotnet|WindowsApps|JetBrains|Microsoft VS Code' }) -join ';'
cmake --build Engine/Build/SrpNativeConfiguration --config Release --target UrsaEditor UrsaScriptIdeProjectTests UrsaExternalScriptEditorTests -- /m:1 /nr:false
ctest --test-dir Engine/Build/SrpNativeConfiguration -C Release -R '^(UrsaScriptIdeProjectTests|UrsaExternalScriptEditorTests)$' --output-on-failure
# 同一 OFF 目录将配置参数替换为 Debug 再执行。
```

本机沙箱中的 MSBuild 多节点执行不能可靠完成，因此验证使用单节点、关闭节点复用。Debug 矩阵的构建缓存采用 `/Z7`，没有修改仓库默认编译选项。Visual Studio COM 调试自动化在本机沙箱外验收，源码定位辅助进程受 30 秒限制、调试控制子进程受 15 秒限制，日志保留在工作区。真实调试复现命令及使用步骤见 [C# 工程与外部编辑器](CSharpIdeIntegration.md)。
