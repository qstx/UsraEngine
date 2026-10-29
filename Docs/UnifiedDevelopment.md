# VS2026 统一开发工作区

## 环境与生成

需要 VS2026（18.x）的 Desktop development with C++、v145、CMake 4.4.5+、.NET 10 SDK/运行时、Python 3.11+，以及引擎已有的 Vulkan SDK 和第三方子模块。bat 默认使用仓库自带的 `Engine/Tools/Python/3.11.17/python.exe`，无需另装 Python；没有自带解释器时使用 PATH 中的 `python`。可用环境变量 `URSA_PYTHON` 指定解释器的完整路径（值不含额外引号或参数）。

从包含 Engine 与 Demo 的工作区根运行：

```bat
GenerateEngineSolutionWithTracy.bat
rem 同时包含测试：
GenerateEngineSolutionWithTracy.bat --tests
```

生成后打开 `Engine/Build/VS2026/UrsaEngine.slnx`。解决方案同时包含 Runtime/Engine/Editor C++、Managed 下的引擎 C# 框架，以及 Projects/Demo 下的 Ursa.Game 与 Ursa.EditorScripts。CMake 4.4.5 默认使用 slnx，既有桥接和检查也支持 sln。

首次生成会先配置并构建 Debug 的 UrsaScriptCompile，再复用资产数据库生成项目工程，准备 Debug/Release 的框架 IDE 绑定，最后完成 CMake 解决方案和关联发布。这个过程不编译 Demo 游戏脚本，不导入资源；错误的游戏脚本也能在生成的 IDE 中修复。旧 `Engine/Build` 中的 VS2022 缓存会保留，生成器不会自动删除或复用它。

指定其他项目或构建根仍可使用同一个 bat（路径有空格时加引号）：

```bat
GenerateEngineSolutionWithTracy.bat --build-dir "G:\Work\Ursa Build" --project-dir "G:\Work\My Game" --tests
GenerateEngineSolutionWithTracy.bat --help
rem 也可直接调用 Python：
Engine\Tools\Python\3.11.17\python.exe Engine\Tools\GenerateSolution.py --tests
```

bat 只负责选择解释器与转发参数；阶段调度、工具链检测、C# XML/JSON 生成和结构检查由 Python 标准库实现，常规生成不依赖 PowerShell。`Engine/Tools/GenerateSolution.bat` 提供同样参数。原 `GenerateSolution.ps1` 等接口保留兼容转发；可选的 `--tests` 中既有自动化测试、Tracy MCP 发布和格式检查等辅助脚本仍使用 PowerShell，其中 IDE 验收脚本需要 PowerShell 7。

不绑定游戏项目时可直接配置 Engine；关闭 .NET 时不生成托管工程。直接配置绑定项目的完整统一工作区前，应先通过生成入口完成工具引导。

## 编辑与刷新

解决方案中的 C++、引擎 C# 和项目脚本均指向原始源码。C# 框架工程保留原始 csproj 的编译设置与依赖，Demo 通过 ProjectReference 引用 Runtime/Rendering/Bindings，EditorScripts 另外引用 Game 与 Editor。Game 不引用 Editor 专属 API。

新增、删除或跨 Editor 目录移动脚本后，运行中的 Editor 会按既有资产刷新流程同步工程；Editor 关闭时重新运行生成入口即可。没有变化时不改写工程文件。删除项目 Library 后，生成入口或当前构建的 Editor 可从引擎工作区描述恢复关联；失效关联会提示重新生成，不静默打开旧工作区。

在 Ursa Editor 的 Edit > External Tools 中选择 VS2026 并保存。双击脚本、点击带行列位置的编译诊断或 Open C# Project 会定位到同一个统一解决方案。已有的用户 IDE 偏好不会被覆盖；若之前选的是 VS2022，需要自行改选 VS2026。未关联项目和其他编辑器继续使用独立脚本工作区。

## 构建与运行

```powershell
cmake --build Engine/Build/VS2026 --config Debug --parallel 3 --target UrsaEditor UrsaScriptPlayer
# 仅检查 Demo IDE 工程及其 C# 项目引用：
dotnet build Demo/Library/ScriptIDE/Projects/Editor/Ursa.EditorScripts.csproj -c Debug
```

VS 的 Debug/Release 映射到相应原生 x64 与托管 Any CPU 配置。引擎 CMake 目标生成正式框架；IDE 项目有独立输出，不覆盖正式框架或项目发布：

| 内容 | 位置 |
|---|---|
| 统一解决方案 | Engine/Build/VS2026/UrsaEngine.slnx |
| 引擎托管 IDE 工程 | Engine/Build/VS2026/Ide/Managed |
| IDE 框架输出与绑定 | Engine/Build/VS2026/Ide/Managed/Build/工程/配置 |
| 正式引擎框架 | Engine/Build/VS2026/Managed/配置 |
| 正式框架中间产物 | Engine/Build/VS2026/Managed/Artifacts |
| 正式脚本目录工具 | Engine/Build/VS2026/Managed/Tools/配置 |
| Demo IDE 工程/检查产物 | Demo/Library/ScriptIDE |
| Demo 正式脚本产物和清单 | Demo/Library/ScriptBuild |

IDE Build 用于编译检查。运行游戏或 Editor 扩展前，仍需通过 Editor 正式脚本构建与内容校验。修改引擎托管 API 或原生绑定后，先构建正式引擎目标并重启宿主，再重新编译项目脚本；单独构建 IDE C# 工程不会替换已加载的框架。

正式构建和 IDE 均编译 `Engine/Managed` 的原始源码，各自隔离输出；正式构建不再复制 `Managed/Source`。正式 PDB 的手写源码路径和校验和对应解决方案中的文件。旧构建树遗留的源码暂存目录已不被使用，可在停止相关程序后清理；`Bindings` 和 artifacts/obj 下的自动生成 `.g.cs`、AssemblyInfo 等仍是正常构建产物。

## 调试

以 UrsaEditor 为启动目标，或附加到当前 Editor PID，选择 Native 与 Managed (.NET Core/.NET) 调试支持。C++ 使用当前配置原生 PDB，游戏和 Editor 扩展使用正式 Debug 构建的 Portable PDB。符号窗口中的模块路径应对应正式产物，避免加载 IDE 检查输出的符号。

Run 中修改脚本保持现有版本；Stop 后等待正式编译及编辑环境重载完成。External Tools 的 `Editor script session` 标识当前编辑会话，磁盘 DLL 更新本身不代表该会话已切换。框架修改需要重建正式目标并重启宿主。

VS2026 已验证保持 Native 与 Managed 同时附加完成 Stop/重载。外部修改源码时，重新载入已保存的文件并核对当前符号，必要时重新绑定断点；未保存缓冲区应先自行处理。打开的 C# 文件及断点位置应属于 Game、EditorScripts 或相应框架 csproj。桥接等待项目加载后从工程成员打开文件，避免将未加载的源码当作杂项文件。

自动化 IDE 验收会记录本次启动的 VS 和测试宿主进程，在成功、失败或超时后关闭它们，并清理本次新增的断点。复用已有 VS 时，保留该实例和原有断点。

clang-format 继续要求版本 19，可通过 `Tools/Format.ps1 -ClangFormatPath <clang-format19.exe> -Check` 指定；VS2026 随附的其他版本不能代替此格式约束。

本机工具链、回归和真实 IDE 证据见 [VS2026 统一开发验收](VS2026UnifiedDevelopmentAcceptance.md)。
