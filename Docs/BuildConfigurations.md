# 构建配置

Ursa 在原生工程、托管框架、项目脚本和生成 IDE 工程中使用同一组配置：

| 配置 | 优化 | 基础 profiling | Deep Profiling | 用途 |
|---|---|---|---|---|
| Debug | 关闭 | 默认开启，可显式关闭 | 按需开启 | 开发调试 |
| Release | 开启 | 默认开启，可显式关闭 | 按需开启 | 性能分析和开发验证 |
| Publish | 开启 | 编译期空实现，不链接 Tracy | 禁止 | 正式发布 |

`Publish` 继承原生 Release 编译/链接优化，C# 显式开启 Optimize；保留匹配的 portable PDB 供内容校验与独立符号管理。Publish 不意味着移除所有业务日志或改变资源身份。

重新运行 `Engine/Tools/GenerateSolution.bat` 后，统一解决方案及独立脚本解决方案均可选择三种配置。框架、IDE 中间文件、脚本 staging、指纹及运行内容按配置和构建输入隔离。

```powershell
cmake --build Engine/Build/VS2026 --config Debug --target UrsaScriptCompile
cmake --build Engine/Build/VS2026 --config Release --target UrsaScriptCompile
cmake --build Engine/Build/VS2026 --config Publish --target UrsaScriptCompile
```

项目脚本必须引用相同配置的框架。使用 Debug 工具请求 Publish 脚本时，必须先构建 Publish 框架；工具不会回退使用 Debug DLL。

```powershell
Engine/Build/VS2026/Publish/UrsaScriptCompile.exe Demo Publish
Engine/Build/VS2026/Release/UrsaScriptCompile.exe Demo Release --deep-profile
Engine/Build/VS2026/Release/UrsaScriptCompile.exe Demo Release --no-deep-profile
```

CMake 的 `URSA_ENABLE_TRACY` 控制开发配置是否包含基础采样；`URSA_ENABLE_TRACY_MEMORY` 和 `URSA_ENABLE_TRACY_DIAGNOSTICS` 仍默认关闭。`URSA_ENABLE_DEEP_PROFILING` 控制开发配置项目构建的默认深度插桩状态，默认 OFF。Publish 始终剔除基础采样，显式指定 `Publish --deep-profile` 会报错。

托管代码使用 `URSA_PUBLISH` 区分发布配置，使用 `URSA_ENABLE_PROFILER` 区分编译时采样能力。自动采样和手动 marker 的详细说明见 [Tracy 性能分析](Profiling.md)。
