# Tracy MCP 发布流程

本工具从固定的 Tracy 子模块版本构建 Windows x64 Python 扩展，并将 Tracy MCP 服务、扩展、协议头和使用说明发布到工作区根目录的 `tracy-mcp-windows-x64-<版本>/`。Tracy Profiler 图形程序仍单独放在相邻的 `tracy-windows-<版本>/` 发布目录。

## 构建发布包

需要 64 位 Python 3.11 及开发头文件、Visual Studio 2022 C++ 工具、CMake 和 Git。先初始化 `Engine/ThirdParty/tracy` 子模块，然后从工作区根目录运行：

```powershell
Engine/Tools/TracyMcp/BuildRelease.ps1 -PythonExecutable (Get-Command python).Source
```

脚本在被 Engine 忽略的 `Engine/Build/TracyMcp/CMakeBuild/` 中使用 VS 2022 配置 Release `TracyServerBindings`，启用 `TRACY_CLIENT_PYTHON=ON`，并关闭静态 Tracy、LTO 和 CPM 源码缓存。第三方依赖由 CMake 下载到临时构建目录。MSVC 使用 UTF-8 和异常处理选项，并抑制 Tracy 上游的 C4996、C4018 警告。构建成功后，脚本校验扩展文件，记录子模块提交和扩展 SHA-256，再以暂存目录替换发布目录。

如果机器离线，可通过 `-PackageExistingBinding` 仅打包已由本脚本成功构建并记录了来源的扩展：

```powershell
Engine/Tools/TracyMcp/BuildRelease.ps1 -PythonExecutable (Get-Command python).Source -PackageExistingBinding
```

离线模式会核对 Tracy 提交、Python ABI 和扩展 SHA-256。它不会重新编译扩展；版本清单的 `buildMode` 会注明复用了既有构建产物。若源码或扩展已变化，应联网执行常规构建命令。

## 准备 Python 环境并启动

发布包不包含 Python 解释器、虚拟环境或 capture。创建工作区内的 Python 3.11 虚拟环境并安装 MCP SDK：

```powershell
python -m venv Engine/Build/TracyMcp/.venv
& Engine/Build/TracyMcp/.venv/Scripts/python.exe -m pip install -r tracy-mcp-windows-x64-@TRACY_VERSION@/extra/mcp/requirements.txt
& tracy-mcp-windows-x64-@TRACY_VERSION@/Start-TracyMcp.ps1 -PythonExecutable Engine/Build/TracyMcp/.venv/Scripts/python.exe
```

服务默认监听 `http://127.0.0.1:47380/mcp`。启动器会检查 Python 版本、MCP SDK 和 Tracy 扩展，并把 capture 列表目录设为 `%LOCALAPPDATA%/UrsaEngine/TracyMcp/Captures`。保持启动窗口运行，结束时按 Ctrl+C。若端口已占用，停止占用该端口的 MCP 进程，或启动时指定其他 `-Port`，并同步修改 `.codex/config.toml` 中的 URL。

## 配置 Codex

工作区根目录的 `.codex/config.toml` 已注册本机 MCP 地址。首次使用时信任该工作区并重新加载 Codex。Tracy MCP 是独立进程，Codex 配置不会代为启动它。

若 Tracy 工具显示 unavailable：

1. 确认 `Start-TracyMcp.ps1` 的终端仍在运行，并检查其中打印的地址与 `.codex/config.toml` 一致。
2. 在工作区执行 `codex mcp list`，确认 `tracy` 已加载；修改配置后重新加载 Codex。
3. 若启动器提示 Python 依赖缺失，使用该虚拟环境重新执行 `pip install -r .../requirements.txt`；启动器也会报告扩展导入失败。
4. 检查端口占用；更换端口后同时更新启动参数和 Codex URL。

## 安全和清理

上游 MCP 提供 `eval` 工具，可在服务进程权限下执行 Python 分析代码。服务仅绑定 loopback，不要将 HTTP 端口暴露或转发到不可信网络；只加载可信 capture，分析后调用 `unload_capture` 释放内存。

临时构建文件和虚拟环境位于被忽略的 `Engine/Build/TracyMcp/`。capture 位于用户数据目录，不会放进发布包。重新发布时常规构建会重建并替换发布目录；可删除 `Engine/Build/TracyMcp/` 清理临时数据。`release-manifest.json` 记录 Tracy 版本、提交、Python ABI、构建模式和选项。
