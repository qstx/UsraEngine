# Tracy MCP 发布流程

本工具从固定的 Tracy 子模块版本构建 Windows x64 Python 扩展，并将 Tracy MCP 服务、扩展和使用说明发布到 `Engine/Tools/TracyMcp/Release/WindowsX64/<版本>/`。共用的 Python 运行时位于 `Engine/Tools/Python/<版本>/`，Tracy Profiler 图形程序仍由单独的发布目录管理。

## 构建发布包

构建需要 64 位 Python 3.11 及开发头文件、Visual Studio 2026 v145 C++ 工具、CMake 4.4.5+ 和 Git。先初始化 `Engine/ThirdParty/tracy` 子模块，然后从工作区根目录运行：

```powershell
Engine/Tools/TracyMcp/BuildRelease.ps1 -PythonExecutable (Get-Command python).Source
```

脚本在被 Engine 忽略的 `Engine/Build/TracyMcp/CMakeBuildVS2026/` 中使用 VS2026/v145 配置 Release `TracyServerBindings`，启用 `TRACY_CLIENT_PYTHON=ON`，并关闭静态 Tracy、LTO 和 CPM 源码缓存。第三方依赖由 CMake 下载到临时构建目录。MSVC 使用 UTF-8 和异常处理选项，并抑制 Tracy 上游的 C4996、C4018 警告。脚本还会下载并校验固定的 CPython 3.11.17 Windows x64 便携发行包，安装到 `Engine/Tools/Python/3.11.17/`；MCP Python 依赖单独放在 Tracy MCP 发布目录的 `PythonPackages/` 中。扩展构建仍需上文的 Python 开发环境，运行 Tracy MCP 不要求另装 Python。

更新发布包前先关闭正在运行的 Tracy MCP 服务，避免 Windows 锁定已加载的 `.pyd` 文件。

便携运行时来自 [Astral 的 python-build-standalone 20261009 发布](https://github.com/astral-sh/python-build-standalone/releases/tag/20261009)。Python 3.11.17 是 CPython 3.11 的安全修复版本；官方说明该阶段不再提供 Windows 安装包，因此这里使用该项目提供的 Windows x64 CPython 发行物，并在清单中固定来源和 SHA-256。

如果不需要重编译 Tracy 扩展，可通过 `-PackageExistingBinding` 打包已由本脚本成功构建并记录来源的扩展：

```powershell
Engine/Tools/TracyMcp/BuildRelease.ps1 -PythonExecutable (Get-Command python).Source -PackageExistingBinding
```

该模式会核对 Tracy 提交、Python ABI 和扩展 SHA-256。它不会重新编译扩展，但仍需准备固定版本的 Python 运行时并安装 MCP 依赖；只有运行时归档和 pip 依赖已缓存时才能完全离线完成。版本清单的 `buildMode` 会注明复用了既有扩展。若源码或扩展已变化，应联网执行常规构建命令。

## 启动 Tracy MCP

Tracy MCP 启动器会使用共享的 `Engine/Tools/Python/3.11.17/python.exe`，MCP 依赖位于 Tracy MCP 发布目录，不需要额外创建 Python 虚拟环境或使用系统 Python。直接启动：

```powershell
& Engine/Tools/TracyMcp/Release/WindowsX64/0.14.1/Start-TracyMcp.ps1
```

服务默认监听 `http://127.0.0.1:47380/mcp`。启动器会检查 Python 版本、MCP SDK 和 Tracy 扩展，并把 capture 列表目录设为 `%LOCALAPPDATA%/UrsaEngine/TracyMcp/Captures`。保持启动窗口运行，结束时按 Ctrl+C。若端口已占用，停止占用该端口的 MCP 进程，或启动时指定其他 `-Port`，并同步修改 `.codex/config.toml` 中的 URL。

## 配置 Codex

工作区根目录的 `.codex/config.toml` 已注册本机 MCP 地址。首次使用时信任该工作区并重新加载 Codex。Tracy MCP 是独立进程，Codex 配置不会代为启动它。

若 Tracy 工具显示 unavailable：

1. 确认 `Start-TracyMcp.ps1` 的终端仍在运行，并检查其中打印的地址与 `.codex/config.toml` 一致。
2. 在工作区执行 `codex mcp list`，确认 `tracy` 已加载；修改配置后重新加载 Codex。
3. 若启动器报告运行时或依赖缺失，检查 `Engine/Tools/Python/3.11.17/`、发布目录的 `PythonPackages/` 与 `requirements.lock.txt`；重新运行发布构建脚本以修复或重新生成。
4. 检查端口占用；更换端口后同时更新启动参数和 Codex URL。

## 安全和清理

上游 MCP 提供 `eval` 工具，可在服务进程权限下执行 Python 分析代码。服务仅绑定 loopback，不要将 HTTP 端口暴露或转发到不可信网络；只加载可信 capture，分析后调用 `unload_capture` 释放内存。

临时构建文件和构建所用的开发 Python 环境位于被忽略的 `Engine/Build/TracyMcp/`。共享 Python 运行时位于 `Engine/Tools/Python/3.11.17/`，Tracy MCP 专属依赖和锁定清单位于对应版本的发布目录。capture 位于用户数据目录，不会放进发布包。重新发布时构建脚本会更新 Tracy MCP 文件；可删除 `Engine/Build/TracyMcp/` 清理临时数据。两个 `runtime-manifest.json` / `release-manifest.json` 记录 Python 来源、校验值及 Tracy 构建版本。
