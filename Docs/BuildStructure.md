# 构建工程结构

## 入口与目录

从引擎仓库根配置工程。根 `CMakeLists.txt` 声明选项和执行顺序，各层入口维护自己的目标；子目录不单独声明 `project()`。

| 目录 | 职责 |
|---|---|
| `CMake/` | 依赖、编译策略、.NET 环境及源码/IDE 分组辅助配置 |
| `Runtime/` | Core、Window、Rhi、Vfs 基础实现 |
| `Engine/` | 应用、场景、渲染、资源及脚本接口 |
| `Engine/Scripting/DotNet/` | 可选原生 .NET 宿主，链接 Engine |
| `Editor/` | 编辑器模块和窗口入口 |
| `Managed/` | 生产框架与脚本目录生成工具 |
| `Tools/` | 通用导入、打包、迁移、脚本编译和 Player 程序 |
| `Tests/` | Runtime/Engine/Editor 模块测试、Integration、Benchmarks、Fixtures、Managed 和 Build 检查 |
| `ThirdParty/` | 固定提交的第三方子模块，内部沿用上游命名 |

头文件与实现位于同一模块目录。生产模块自动发现 `.cpp/.hpp/.h/.inl`，正常构建会检查文件增删。新独立模块需要在所属入口声明目录；可选宿主实现由自己的目标持有，不混入 `UrsaEngine`。

公开目标及别名继续使用 `UrsaRuntime` / `Ursa::Runtime`、`UrsaEngine` / `Ursa::Engine`、`UrsaEditorCore` / `Ursa::Editor`、`UrsaDotNetHost` / `Ursa::DotNetHost`。Runtime 不依赖场景；Engine 不依赖宿主，宿主在启用 .NET 时链接 Engine。

## Visual Studio 分类

解决方案目标按 Runtime、Engine、Editor、Tools、Tests、Managed 和 ThirdParty 分类。项目内 filter 使用源码相对引擎仓库根的真实目录，例如 `Runtime/Core/Memory`、`Engine/Scripting/DotNet` 和 `Tests/Editor/Scene`，同目录头文件和源文件放在一起。

源码登记与 filter 使用同一集合；只修改 `.filters` 不能补齐未登记的头文件。生成的 `.sln/.vcxproj/.filters` 由 CMake 管理，修改配置后重新生成。

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64 `
  -DURSA_ENABLE_DOTNET=ON -DURSA_PROJECT_DIR=../Demo
cmake --build Build --config Debug --parallel --target UrsaEditor UrsaScriptPlayer
cmake --build Build --config Debug --target UrsaProjectStructureCheck
```

Editor 位于 `Build/Editor/Debug/UrsaEditor.exe`，Player 位于 `Build/Debug/UrsaScriptPlayer.exe`，导入工具位于 `Build/Editor/Debug/UrsaAssetImport.exe`。其他配置将 Debug 替换为相应配置名。MSVC 默认启用 `/MP`，构建入口同时使用 `--parallel` 调度多个目标。

工作区根的 `GenerateEngineSolutionWithTracy.bat` 会显式开启 `/MP`，覆盖旧 Build 缓存中的并行关闭设置。默认保留 Editor、Player、导入和打包工具，使用 `URSA_BUILD_TESTS=OFF` 避免整解决方案构建连带编译全部测试；运行 `GenerateEngineSolutionWithTracy.bat --tests` 可在同一个解决方案中启用测试工程。直接运行 CMake 的测试默认值保持原样。

在 VS 中右键所需的 Editor、Player 或工具目标并选择 **Build**，会构建该目标及其依赖；**Build Solution** 会构建当前配置勾选的全部目标。命令行可使用上面的 `--target` 选择一个或多个目标。Debug 和 Release 使用各自的编译产物，切换配置后的首次构建仍需要编译对应依赖。

Tracy 的版本和 Git 提交号只传给 `Runtime/Core/Profiler.cpp`。提交后重新生成解决方案时，提交号变化只使该文件重新编译，再更新受影响的库和链接产物。

## 测试与准备步骤

`Tests/CMakeLists.txt` 调度各层测试，每个模块就近登记独立测试入口、链接、定义和 fixture。共享辅助头文件与实现由 `UrsaTestFixtures` 持有，测试通过链接获得 include 路径，生产目标不会继承测试辅助代码。

测试通过 `LABELS` 标记实际目录，原有测试名称保持可用于 `-R` 过滤。编译测试不会执行资源导入；只有读取导入内容的测试依赖 `UrsaTestContent`，项目集成测试依赖 `UrsaTestSample` 及其所需脚本/打包准备步骤。纯 CPU 测试不附加资源 fixture。

```powershell
cmake --build Build --config Debug --parallel
ctest --test-dir Build -C Debug --output-on-failure -L "Tests/Runtime/Core"
ctest --test-dir Build -C Debug --output-on-failure -L "Tests/Editor/Scene"
ctest --test-dir Build -C Debug --output-on-failure -R "UrsaProject(Structure|Discovery)Tests"
```

托管测试源码位于 `Tests/Managed`，其生成工程位于 `Build/Tests/Managed/Source`。生产程序集仍位于 `Build/Managed/<配置>`，测试夹具通过独立 `UrsaManagedTestAssemblies` 目标生成到 `TestScripts`。关闭 `URSA_BUILD_TESTS` 时不构建测试程序集、不添加 CTest 准备步骤。

```powershell
cmake -S . -B Build/Production -DURSA_BUILD_TESTS=OFF -DURSA_ENABLE_DOTNET=ON
cmake --build Build/Production --config Release --parallel --target UrsaEditor UrsaScriptPlayer
```

VS 结构检查会核对目标文件夹、头文件/源文件登记、filter 路径、重复编译与测试关闭状态。增删检查在 Build 内的隔离源码副本中运行，不修改实际模块文件。

## 第三方路径迁移

依赖父目录使用 `ThirdParty`。`.gitmodules` 的 `path` 和 gitlink 已迁移，子模块逻辑名称可以保留旧名称；它们用于 Git 元数据身份，不是源码目录路径。

```powershell
git submodule update --init --recursive
git submodule status
```

从旧版本切换后重新运行 CMake，旧生成工程中的 `third_party` 路径已失效。本次保留 11 个子模块的固定提交及声明远端；第三方上游源码和依赖版本未更新。旧独立 Git 元数据备份在迁移工作区的 Build 内，不纳入源码提交。测试和子模块对比基线位于 `Tests/Build/ProjectBaseline.json`。

托管构建目录改变后，新程序集的内容指纹可能变化。已有项目的 Player 内容需由当前构建的导入工具重新准备；Player 的旧内容检查仍然有效，普通程序构建不会代替导入。

```powershell
Build/Editor/Debug/UrsaAssetImport.exe --project ../Demo --configuration Debug
Build/Debug/UrsaScriptPlayer.exe --project ../Demo --scene Snake/Snake.ursascene
```

## 本次迁移验收

2026-10-11，Windows x64 / VS 2022，使用全新的 `Build/Structure` 验证完整 Editor、.NET 和 Tracy 配置：

- Debug/Release 均完成全量及最终增量构建，成功构建日志中无编译警告。
- 保留原有 113 个测试名称，新增 3 个工程结构用例。Debug/Release 各通过完整的 115 项测试，以及随后注册的错误 filter 检测用例，合计覆盖 116 项。
- VS 检查通过：109 个登记的第一方工程、354 个 C++ 文件项，分类和 filter 与磁盘一致。隔离用例验证默认完整构建会检测文件增删，错误 filter 会被拒绝。
- `Build/StructureNoTests` 验证测试 OFF、Tracy OFF 的 Editor/Player 构建；无测试目标、测试程序集或导入内容。`Build/StructureNative` 验证 Editor/.NET/Tracy 均关闭的原生构建与代表性 CPU 测试。
- 实际 Demo 的通用 Debug/Release Editor 正常打开并关闭；编辑器窗口测试完成三次 Run/Pause/Resume/Stop，最小化期间 GUI 帧数为 0。显式刷新当前构建的托管内容后，Debug Snake、材质展示和 Release Snake Player 均正常退出。
- C++ 格式检查通过，两个 benchmark 正常执行，11 个第一层子模块初始化检查及固定提交对比通过，Demo 源码状态保持干净。

测试运行目录统一使用引擎 Build 根，测试配置入口显式创建隔离 Temp，资源 fixtures 按实际需求配置。验收全量构建重试时使用 `--parallel 2` 控制同时编译的项目数；默认多核策略保持有效。原始日志保存在此次工作区的 Build 内，不纳入源码提交。
