# 构建资源与项目缓存

引擎源码资产位于 `Engine/Assets`。引擎内置 Shader 和托管引导内容在构建期间生成，项目资产在 Editor 或项目工具导入期间生成。

| 内容 | 默认位置 |
|---|---|
| 引擎运行资源 | `Engine/Build/Resources/<配置>/` |
| 引擎 Shader 编译缓存及暂存 | `Engine/Build/Intermediate/ShaderCompileCache/` |
| 项目数据库与导入产物 | `<项目>/Library/` |
| 项目 Shader 缓存 | `<项目>/Library/ShaderCompileCache/` |
| 项目脚本工程与程序集 | `<项目>/Library/ScriptBuild/` |

引擎生成路径跟随实际 CMake 二进制目录。单配置生成器必须明确选择 `CMAKE_BUILD_TYPE`。源码根下的 Engine/cache 不再作为生成根。

```powershell
cmake -S Engine -B Engine/Build -DURSA_ENABLE_DOTNET=ON -DURSA_PROJECT_DIR=Demo
cmake --build Engine/Build --parallel --config Debug --target UrsaEngineResources
cmake --build Engine/Build --parallel --config Release --target UrsaEngineResources
```

Editor、Player 和需要内置资源的工具与测试目标会自动准备资源。直接构建 Editor 或打包目标也会得到托管引导文件。兼容目标 `UrsaEngineShaderAssets` 转发到完整资源准备流程。

每个配置的 `BuildManifest.json` 记录配置、目标、输入、工具版本及输出校验信息。准备流程验证实际文件，全部内容成功后才原子发布清单。输入与产物均未变化时复用结果；不同配置共享按指纹组织的编译缓存，写入由导入锁协调。

## 清理与恢复

- 删除生成 Shader、其元数据、托管引导文件或整个 `Resources/<配置>` 后，重新构建对应消费者即可恢复。旧完成记录不会掩盖缺失文件。
- 删除整个构建目录后，重新配置并构建即可恢复引擎产物。
- 删除项目 Library 后，重新打开并导入项目，再编译项目脚本。保留 Assets 与 `.meta`，以保留资源身份。
- 缺少资源时直接启动开发程序会报告虚拟资源及配置并提示重建，不在运行时自动编译引擎 Shader。

例如，关闭程序后可验证单文件恢复：

```powershell
Remove-Item -LiteralPath Engine/Build/Resources/Debug/Shaders/Editor/ImGui.ursashaderasset
cmake --build Engine/Build --parallel --config Debug --target UrsaEditor
```

## 配置迁移

`URSA_ASSET_CACHE_DIR`、`URSA_ASSET_MIRROR_DIR` 已废弃。已有缓存保存旧默认路径时，重新配置会迁移并清除旧项。自定义旧值会得到诊断，需要清除旧选项后改用 `URSA_ENGINE_RESOURCES_ROOT` 与 `URSA_SHADER_COMPILE_CACHE_DIR`；新目录必须位于所选构建目录内且相互分离。配置迁移不删除未知自定义目录中的数据。

## 运行与打包

开发模式通过只读 `ursa:` 访问源资产与当前配置的生成内容。项目 Assets 使用 `prj:`，项目 Library 使用 `lib:`。显式部署目录和 pak 仍可独立提供资源。

```powershell
Engine/Build/Debug/UrsaScriptCompile.exe Demo Debug
Engine/Build/Debug/UrsaPak.exe --engine Engine/Assets `
  --engine-generated Engine/Build/Resources/Debug --configuration Debug `
  --project Demo/Assets --library Demo/Library --output Engine/Build/Package/Debug/Content.pak
```

打包校验清单、源输入、配置和必要文件，拒绝缺失或过时内容，并保留原有完整包。构建清单、编译缓存、源 Shader 和 `.meta` 不进入发行内容；已发布运行资产与目录信息不要求 Build 或 Library 存在。

项目 Library 缺失后，先在 Editor 中重新导入模型、图片和项目 Shader，再编译脚本和打包。单独编译脚本不会代替模型/图片导入。验证工程提供 `UrsaProjectLibraryRecoveryTests` 检查隔离项目的 Library 删除重建；项目打包测试的导入夹具会准备当前 Demo 缓存。
