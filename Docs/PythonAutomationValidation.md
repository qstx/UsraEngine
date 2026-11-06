# Python 自动化迁移验收

2026-11-05，Windows x64，仓库自带 CPython 3.11.17、VS2026/v145、.NET 10。构建目录为 `Engine/Build/VS2026`，日志位于忽略目录 `Engine/Build/PythonMigration/`。

## 完成的替换

移除 28 个第一方 `.ps1` 文件，包括工具、构建/集成/窗口测试、VS 调试验收，以及 Tracy MCP 发布目录内的旧启动器。原有 Python 实现复用于工具链、解决方案解析、工程生成和结构检查，其余功能迁移到对应 `.py`。CTest、CMake 格式检查目标、当前工具文档和代码规范入口均已同步。CPython、第三方子模块和 PythonPackages 内的依赖脚本不做修改；历史验收文档保留当时的执行记录。

迁移没有在 Python 中再次启动 PowerShell。VS 自动化仍使用已有的 C# 控制器；图标和像素测试使用标准库 ctypes 调用 Windows GDI+。

## 已通过的检查

- CMake 重新配置和生成成功；第一方活动脚本及 CMake 入口扫描不再引用 `.ps1`、`URSA_POWERSHELL_EXECUTABLE` 或 PowerShell 可执行文件。
- 9 项 CTest 通过：`UrsaPythonWorkspaceToolsTests`、`UrsaPythonAutomationTests`、`UrsaAcceptanceProcessTests`、`UrsaManagedWorkspaceGenerationTests`、`UrsaUnifiedIdeFixtureTests`、`UrsaUnifiedWorkspaceRefreshTests`、`UrsaProjectStructureTests`、`UrsaProjectDiscoveryTests`、`UrsaProjectStructureNegativeTests`。最终 JSON 共享读取/替换调整后，工作区刷新测试再次通过。
- `PythonAutomationTests.py` 最终 14 项通过：中文和特殊字符参数、预期失败状态、环境恢复、JSON 原子发布、Windows 共享读取时替换、路径边界、归档逃逸/链接拒绝、发布回滚、Tracy 清单拒绝、格式替换字节比较、无 PowerShell PATH 的 CLI 帮助、VS 控制器文本/JSON 响应和 RPC 重试、持有输出句柄的子进程不阻塞父命令返回。
- Python 图标工具实际生成 7 个尺寸的 ICO，验证目录和 PNG 数据；Windows GDI+ 像素读取可用。原始 Editor 图标没有被本次验收覆盖。
- Tracy MCP 发布目录的 `Start-TracyMcp.bat --check` 通过，确认固定运行时清单、版本、MCP 和原生扩展导入可用。没有重新下载运行时或重新发布第三方依赖包。
- `RunUnifiedIdeAcceptance.py` 真实通过 VS 补全、框架/Game 定义导航和已有解决方案定位，并核对源文件哈希。
- `RunPrePlayRenderDebugAcceptance.py` 最终真实通过冷 F5：在 Play 前命中 RenderInternal，检查 C# 工程成员资格、相机数量和加载框架与正式框架的 SHA-256。证据为 `IdeAcceptance/PrePlayLaunch-73b8214726724bea9cfb252f6568ed68/Evidence.json`，日志为 `PrePlayFinal.log`。
- 混合调试真实验证了 NativeDebugProbe 的 `nativeMarker=42`、RenderInternal 的 `cameras.Count=1`、Editor 扩展的 `editorMarker=41`、Game Awake 的 `runtimeMarker=10`，包括断点和单步。保持附加后的正式重编译/编辑会话重载未在原期限内完成，因此整项混合调试没有标记为通过。
- Python 版 `UrsaAssetImportCliTests` 首次通过（1083.68 秒），输出收集调整后的最终复验再次通过（944.14 秒），涵盖作用域、缓存、参数/目标失败、写锁、缺失产物恢复、只读 Player 和确定性打包。
- `git -C Engine diff --check` 通过。Python AST/CLI 帮助检查通过。

## 实现中处理的边界

Python 等待外部工具时使用临时日志文件并等待主进程退出，避免 .NET 编译服务继承输出句柄后，管道 EOF 延迟使驱动继续等待。对应回归测试启动一个仍持有输出句柄的后台子进程，验证父命令可及时返回并安全清理子进程。

Windows JSON 读取保留共享读/写/删除语义；替换已有文件使用 ReplaceFileW，再以 MoveFileExW 处理新建/竞争情况。实测仅使用 os.replace 时，正在共享读取的目标文件会被拒绝替换；最终回归覆盖了读取旧版本期间发布新版本。

测试生成的随机子目录缩短为 12 位十六进制，并缩短预览 CTest 默认证据根，避免原生 VFS 临时文件名叠加后超过 Windows 路径上限。仍使用独立目录，不覆盖已有证据。

## 尚未全部通过的集成验收

- 预览持久化：原生程序报告 `Application Preview workspace timed out`。绕过 Python 直接运行同一原生 fixture 也复现；日志为 `DirectPreview.log`。驱动保留原有检查和失败状态。
- 渲染管线/抓帧：完整 `UrsaProjectPipelineSamplesTests` 超过原有 600 秒期限。已经产出部分 Player 图像；没有将完整像素、20 次窗口循环和全部 Pak/Editor 分支宣称为通过。日志为 `IntegrationFinal.log`，部分图像及执行记录在该次隔离 PipelineSamples 项目中。
- 混合调试：首次断点/单步/局部变量通过后，正式重编译与编辑会话重载超时，状态仍为 Busy。日志为 `ManagedDebugVerified.log`，断点记录为 `IdeAcceptance/MixedDebugFailure.json`；保留断言，没有跳过重载检查。
- Python 格式检查与原脚本一致，仍报告 22 个既有第一方 C++ 文件；日志为 `Format.log`。本轮没有修改业务 C++ 来处理这些格式或原生验收问题。
- 没有执行完整 Tracy 扩展重编译/依赖重新打包、外部 glTF 下载验收或性能矩阵。打包的边界与回滚通过隔离测试验证，现有 MCP 运行时及新启动入口通过实测。

这些限制保留在记录中；本次完成的是脚本实现及调用链迁移，不代表整个引擎的 GUI/GPU/重载回归均已通过。
