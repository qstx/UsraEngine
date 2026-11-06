# Python 自动化工具

第一方工具、CTest 驱动和 VS 验收脚本统一使用 Python 3.11+。原 PowerShell 实现及兼容转发已移除；第三方发行物中的脚本（例如 CPython 自带的 `Activate.ps1`）属于依赖内容，不做修改。

从工作区根目录运行 `Engine/Tools/RunPython.bat <脚本.py> <参数>`。该入口默认选择仓库自带 Python，并启用 UTF-8；也可用环境变量 `URSA_PYTHON` 指定解释器完整路径。脚本参数使用 `--kebab-case`，每个可执行入口均支持 `--help`。

| 用途 | Python 入口 |
|---|---|
| VS 工程与 C# 项目生成 | `Tools/GenerateSolution.py`、`GenerateManagedIde.py` |
| 工具链、解决方案与结构检查 | `Tools/WindowsToolchain.py`、`ReadVisualStudioSolution.py`、`CheckProjectStructure.py` |
| 格式与资源 I/O 检查 | `Tools/Format.py`、`AuditResourceIo.py` |
| Editor ICO | `Tools/GenerateEditorIcon.py` |
| 渲染截图与性能矩阵 | `Tools/CaptureRenderingBaseline.py`、`RunSrpPerformanceMatrix.py` |
| 外部 glTF 验证 | `Tools/ValidatePublicGltf.py` |
| Tracy MCP | `Tools/TracyMcp/BuildRelease.py`、`Start-TracyMcp.py` |
| VS 补全、混合调试、首次 Play 前断点 | `Tests/Editor/Scripting/RunUnifiedIdeAcceptance.py`、`RunManagedDebugAcceptance.py`、`RunPrePlayRenderDebugAcceptance.py` |

统一工程仍使用工作区根的 `GenerateEngineSolution.bat`。Tracy MCP 提供 `Tools/TracyMcp/BuildRelease.bat` 和发布目录内的 `Start-TracyMcp.bat`；bat 只选择解释器与转发参数。

```bat
GenerateEngineSolution.bat --tests
Engine/Tools/RunPython.bat Engine/Tools/Format.py --check --clang-format-path "C:\Tools\clang-format.exe"
Engine/Tools/RunPython.bat Engine/Tools/CheckProjectStructure.py --build-dir Engine/Build/VS2026
Engine/Tools/RunPython.bat Engine/Tools/GenerateEditorIcon.py --output Engine/Build/UrsaEditor.ico
Engine/Tools/TracyMcp/BuildRelease.bat --help
Engine/Tools/TracyMcp/Release/WindowsX64/0.14.1/Start-TracyMcp.bat --check
```

迁移保留原有行为：格式检查要求 clang-format 19；截图证据目录不得覆盖；文件操作校验工作目录边界；发布失败恢复原包；验收只关闭本轮启动并核对路径的进程；环境变量在失败后恢复。图标和像素验收通过 Python `ctypes` 调用 Windows GDI+，没有新增 Python 包依赖。VS 验收通过 Python 启动既有的 C# 控制器，需要 VS2026 与 .NET 10，不需要 Python COM 包。

重新运行生成入口或 CMake 配置后，CTest 会调用 Python 驱动。工作区测试保留原名称，新增 `UrsaPythonAutomationTests` 覆盖 UTF-8 参数、JSON 替换、路径边界、归档检查、发布回滚、格式替换比较和无 PowerShell PATH 的 CLI 启动。VS 与 GPU 集成验收仍需要相应原生目标、Windows 桌面和图形设备。实际通过项和未通过的原生阶段见 [迁移验收记录](PythonAutomationValidation.md)。

旧验收记录中的 `.ps1` 命令是当时执行的历史证据；当前执行方式以本页和各工具 `--help` 为准。
