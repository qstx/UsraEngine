# 固定 C# 与默认通用管线迁移

C# 是引擎固定能力。完整根工程配置要求 Windows x64、.NET 10 SDK、兼容运行时、宿主包及引用包；Runtime、Engine、宿主和 Editor 仍保持独立目标。发行 Player 只要求规定的平台运行时和发布内容，不要求 SDK、项目源码或导入工具。

## 构建与旧缓存

新构建不再传入托管开关：

```powershell
cmake -S Engine -B Engine/Build/VS2026 -G "Visual Studio 18 2026" -A x64 -T v145 -DURSA_WORKSPACE_BOOTSTRAP=ON
cmake --build Engine/Build/VS2026 --config Debug --target UrsaEditor UrsaScriptPlayer
```

统一游戏工作区仍优先使用根目录 `GenerateEngineSolution.bat`。Editor、测试和 Tracy 的独立选项继续有效。

旧命令或缓存中存在 `URSA_ENABLE_DOTNET` 时，无论值为 ON 还是 OFF，配置都会明确报错。移除调用脚本中的参数，并仅清除这一个缓存项：

```powershell
cmake -UURSA_ENABLE_DOTNET -S Engine -B Engine/Build/VS2026
```

继续使用原来的生成器和其他设置。缺少 .NET 工具链应安装或修复对应依赖，不能再通过 OFF 配置绕过检查。绑定生成和快照漂移校验固定参与适用目标；只构建 Runtime 目标仍不会编译场景或宿主模块。

## 默认通用管线

项目缺少 Graphics 配置、选择为空或调用 `RenderPipelineManager.SetPipelineAsset(null)` 时，使用 Default Universal。引擎提供默认通用管线及 RendererData 源资产；导入在当前项目 Library 生成运行产物，发布包含完整默认依赖，因此不要求项目先创建管线资产。

默认配置为 EV=0、Reinhard、无阴影、无 Bloom、无 FXAA，RendererData 的 features 为空。需要完整效果时，通过 Rendering/Universal Pipeline 和相应 RendererData 创建项目配置，再在 Graphics 设置中选择。默认值不会自动写入项目设置、创建作者资产或标记文档 Dirty。清除显式选择会恢复默认通用配置。

Scene、Game、资产 Preview 和 Player 使用同一选择规则。显式配置缺失、类型不兼容或依赖损坏时给出诊断；编辑器保留上次有效呈现及界面操作，不把失败的显式选择当作默认值。

## 旧 Forward

`ForwardRenderPipeline`、`ForwardRenderPipelineAsset` 和 `NativeForwardPipeline` 已退役。旧 `ursa.rendering.forward` 标识不再注册为另一个类型，旧资产不会自动回写。

- 没有显式管线选择的项目直接使用默认通用管线。
- 显式引用旧 Forward 资产的项目，清除 Graphics 选择，或创建并选择新的 Universal 管线及 RendererData；按所需行为迁移配置，不修改其他材质和场景 GUID。
- 引用旧 C# 类型的项目，改用通用管线资产或自己的 RenderPipelineAsset/RenderPipeline，编译诊断会定位旧引用。
- 原生 Application 宿主需显式提供 `InitializeRenderPipeline` 和 `OnRender`；基础类不会自动创建原生 Forward。游戏程序使用脚本 Player 的托管 SRP 实现。Editor 预览同样通过当前 SRP 提供者渲染。

普通程序编译仍不导入内容。首次运行、清除 Library 后或依赖变化后，先执行正常项目导入；发行 Player 只读取已准备的松散或 Pak 内容。即使当前选择自定义 SRP，发布也包含恢复默认通用管线所需的依赖。历史 ON/OFF 与旧 Forward 验收记录仅适用于当时的版本。
