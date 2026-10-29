# VS2026 统一开发验收

验收日期：2026-10-28～29。本次证据来自 `Build/VS2026`，未复用旧 VS2022 二进制。使用方式见 [统一开发工作区](UnifiedDevelopment.md)。

| 工具 | 实际版本 |
|---|---|
| CMake | 4.4.5，生成 `UrsaEngine.slnx` |
| Visual Studio | 2026 / 18.11.2，MSBuild 18.11.1 |
| C++ | v145，MSVC 19.51.36260.0，x64 |
| .NET SDK | 10.0.403，托管工程 Any CPU |
| C++ 格式 | clang-format 19.1.1 |
| 工作区 Python | 仓库自带 3.11.17 |

## bat / Python 入口追加验收（2026-10-29）

`GenerateEngineSolutionWithTracy.bat` 现通过 Python 实现生成、工具检测与结构检查。将 PATH 中所有 PowerShell 路径移除，确认 `where pwsh` 与 `where powershell` 均找不到后，默认入口完成实际五阶段生成；生产方案为 17 个第一方目标、424 个 C++ 文件项和 7 个 SDK C# 工程。随后通过 `--tests` 恢复完整工作区，检查 163 个目标、615 个 C++ 文件项及 12 个 C# 工程，Demo 关联为 Ready，bootstrap 为 OFF。

新增 Python 回归 8/8 通过，覆盖 bat 从其他当前目录执行、带空格参数、缺失解释器、过旧 CMake、旧缓存原样保留、子命令失败返回 42 且不发布、XML 元数据/源码/项目引用、原 .NET GUID 的固定值、无变化文件时间戳、源码增删以及 sln/slnx 映射。受影响的 7 个 CTest 全部通过（20.12 秒），包含旧 ps1 转发兼容、结构负例、工程发现、脚本刷新与独立验收夹具；VS 中的 `UrsaProjectStructureCheck` 目标也完成 Python 检查。本轮未启动 VS 或 Editor GUI；此前真实语言服务与混合调试证据保留。

本机日志位于 `Build/VS2026/PythonProductionGeneration.log`、`PythonWorkspaceFinalGeneration.log`、`PythonWorkspaceFinalRegression.log` 和 `PythonStructureTarget.log`。

## 正式托管构建使用原始源码（2026-10-29）

正式框架、目录工具和托管测试直接编译原始 csproj，移除 StageManaged 源码复制；framework、test、IDE 各自使用独立 artifacts/output。增加稳定的源码输入清单，覆盖 CMake/MSBuild 同一次构建中的新增与删除。

- Debug/Release 正式框架、目录工具、测试夹具构建通过；`UrsaManagedSourceSymbolsTests` 验证五个正式框架 DLL/PDB 身份、原始手写源码路径和文件校验和，以及测试自身的原始源码路径。绑定工具和目录工具的两个配置 PDB 分别有 7 和 2 个原始源码文档。
- 临时新增 Runtime C# 类型后，同一次正式构建的 DLL/PDB 包含该类型和源文件；删除并重新构建后两者均移除，探针已清理。
- 完成两种配置全部目标后，无变化重复构建保留 436 个正式/IDE DLL/PDB 的大小与时间戳，源码树 379 个既有 bin/obj 缓存未新增或改写。
- 受影响回归 Debug 11/11（152.78 秒）、Release 11/11（104.14 秒）通过，包括样例准备、绑定生成、源码符号、Snake 逻辑及七个工程回归；本轮未启动 VS 或 Editor GUI。
- 本机旧 `Managed/Source` 与 `Tests/Managed/Source` 共 1099 个文件先完整归档并校验，再清理。归档为 `Build/VS2026/LegacyManagedSources-20261029-125222-e9daa7.zip`；重复正式构建未重建这些目录。

另行执行的完整 `UrsaManagedProtocolTests` 在两个配置均失败于 `CustomEditorRegistrationTests.cs:86` 的 Inspector 数量/Camera 多编辑回退断言。清理前运行迁移前暂存目录中的旧 Debug 二进制，复现相同断言；这是既有失败，未在本次源码构建迁移中修改注册逻辑。新增源码符号检查独立登记，不将该完整协议测试记为通过。

日志见 `Build/VS2026/DirectManagedRegression-Debug.log`、`DirectManagedRegression-Release.log`、`DirectManagedIncremental.log`、`DirectManaged-AddedSourceSymbols.log` 和 `DirectManagedProtocolBaseline.log`。

## 此前统一工作区验收已通过

- 新构建根配置、完整 Debug 方案构建，以及 Debug/Release Editor、Player、脚本工具构建。
- 无 Library 且有错误 C# 源码的独立项目完成工作区引导，不生成正式游戏程序集或导入游戏资源。
- IDE 框架与正式产物隔离；在同一构建入口中，代表性原生、正式托管和 IDE 工程无变化增量构建保持产物时间不变。
- 托管元数据生成、脚本增删/跨组移动、Library 重建、关联失败恢复、工程结构及发现回归。
- 无项目、关闭测试及关闭 .NET 的配置成员隔离。
- 真实 VS2026 中 Game、EditorScripts、框架生成 API 补全，Demo 到框架及 EditorScripts 到 Game 的定义导航，以及重复源码定位。源码磁盘内容未改变。
- 打开文件、定义导航目标和 C# 断点停靠均断言真实 csproj 归属：Game、EditorScripts、Ursa.Runtime；不接受“杂项文件”作为验收成功。
- Debug 完整 Editor 工作流 136.79 秒，Release 141.47 秒；覆盖编译错误恢复、编辑环境重载、未保存字段及多轮 Run/Stop。
- 最小 Vulkan RHI 与 Editor 合成呈现回归。
- Tracy MCP 在 VS2026/v145 配置并构建 `TracyServerBindings.cp311-win_amd64.pyd`，保留第三方固定提交。
- 验收进程所有权检查、正常/失败清理和重复清理；仅关闭本次启动的 VS/宿主，仅清理本次新增断点。
- 独立统一调试方案复用真实原生工程和引擎 SDK 工程，链接夹具原始脚本；不切换主解决方案、Demo 关联或宿主自己的 IDE 工程。
- 同一 PID 29692 的原生与托管混合调试：C++ 局部变量 `42`；Editor 扩展 `41 → 42`、Game `10 → 11`。每次都验证断点、调用栈、单步和局部变量，保持附加完成正式 Stop/重载并保留未保存字段。
- 框架正式重建与宿主重启：临时改变 `GameObject.Name` 的受控回调行为，正式 Runtime DLL 哈希改变；新 PID 27256 得到 `Framework2:Debug:10`。结束后源码逐字节恢复，正式 DLL SHA-256 恢复为原值。

机器上的详细输出位于 `Build/vs2026-*.log`；真实补全与导航证据为 `Build/VS2026/IdeAcceptance/UnifiedLanguageEvidence.json`。

混合断点、源码工程归属、正式 PDB 指纹及版本切换记录见 `Build/VS2026/IdeAcceptance/managed-debug-evidence.json`；框架重启记录见同目录 `FrameworkRestartEvidence.json`。主方案文档清理确认没有遗留的验收脚本文档，也没有关闭未保存的用户缓冲区。

## Editor 超时定位

初次线程采样位于 Vulkan fence 等待，但阶段追踪显示初始加载能够连续完成数千帧。完整工作流随后明确报告 `Custom Inspector target is not an exposed component: ursa.Animator`。Animator 已存在于原生持久化和托管框架，Editor 类型目录遗漏了它，导致默认托管 Inspector 注册失败、编辑环境不能发布。

修复补齐 Animator 的类型元数据和创建工厂，并验证组件所有者、持久化身份和版本。未修改 Vulkan 同步语义或关闭验证层。生产代码中的临时呈现日志已移除。

## 调试时序与检查边界

磁盘清单更新早于实际编辑环境重载。验收等待 `Editor script session` 切换和任务完成，再调用新版扩展。外部修改脚本后，从其所属工程重新载入已保存的源码，并重新绑定验收创建的断点，确保缓冲区与新 PDB 源码一致。不会覆盖未保存的缓冲区。

本机混合调试的 DTE 分离调用曾返回 `0x8000FFFF`；本次成功路径保持调试器附加，最后正常关闭受控宿主和 VS。该结果不推断其他 IDE 操作的分离行为。

本次改动的 C++ 文件通过 clang-format 19 检查。全仓检查另有 22 个未修改文件的既有格式差异，保持原状。第三方 11 个子模块固定提交及已跟踪内容未变，Demo 已跟踪 C# 和 `.meta` 身份未变。

最终完整 Debug 方案构建成功；受影响的 Debug 回归 11/11、Release IDE/隔离/清理回归 5/5，Release 最小图形回归含资源夹具 3/3。完整托管 Editor 工作流在 Debug/Release 均通过。OpenSpec 严格校验及 Git 空白检查通过。全量测试集未全部重跑。
