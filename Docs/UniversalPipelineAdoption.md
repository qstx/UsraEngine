# 通用渲染管线采用指南

本指南适用于当前显式 CommandBuffer 的 Universal 管线实现。职责与 Shader 映射按本地 Unity Graphics SRP Core/URP 12.1.16、固定提交 `6034ecf6489af2ac1d543f9de25a76b8e91a78cb` 核对；Unity 源码只作参考，不参与 Ursa 构建。实现边界和验证见 [Scriptable Rendering](ScriptableRendering.md)、[Shader Authoring](ShaderAuthoring.md) 与 [渲染验证](ScriptableRenderingValidation.md)。

## 选择示例管线

Demo 的 `Assets/Rendering` 包含 `UniversalPipelineSample.ursaasset`、`UniversalRendererData.ursaasset` 与 `ScreenTintFeature.ursaasset`。Project Graphics 的选择保存资产 GUID/local file ID，而不是路径；资产移动或重命名后选择仍有效。打开 Editor 的 `Window > Project Graphics Settings`，在默认 Render Pipeline 下选择 `UniversalPipelineSample.ursaasset` 并按 **Apply Graphics Settings**。选中后配置会写入 `ProjectSettings/Graphics.json`。

示例 RendererData 按顺序引用 Screen Tint Feature。它在 `AfterTransparents` 运行，仅匹配 Game 相机；以当前相机的实际像素尺寸分配 RGBA16F 临时副本，在视口内滤色并把结果写回场景目标。SceneView、材质/模型 Inspector Preview 不会执行该 Feature。示例后处理默认关闭 Bloom 与 FXAA、使用 EV=0 和 Reinhard，并启用两级主方向光阴影。Feature 可在 Inspector 单独调整 tint。

## 新建项目 Feature

可在 `Assets > Create` 下创建 `Rendering/Universal Render Pipeline`、`Rendering/Universal Renderer Data` 和项目自有的 `ScriptableRendererFeature` 资产。管线资产引用 RendererData，RendererData 的 Features 数组定义执行顺序。配置资产只保存序列化参数；每个运行管线实例独立构造 Feature，切换或释放管线时 Dispose 运行实例。Pass 在 `AddRenderPasses` 中按相机筛选后入队，通过事件值排序，同事件保持入队顺序；仅在 Pass 声明输入时生成深度/法线或颜色快照。Pass 必须通过作用域管理它申请的临时目标。

创建 Feature 时先用普通 Shader/材质引用验证简单效果，再声明准确的事件、相机类型和 `ScriptableRenderPassInput`。公共场景 Shader 应引用 `ShaderLibrary/Core.hlsl` 和需要的表面/光照层，不重新声明 PerMaterial/PerObject 布局。Unlit 旧 `_MainTex` 迁移在载入时幂等映射到 `_BaseMap`；保存前不会改写源资产。内置 Lit/Unlit GUID 保持不变，旧 Renderer3D 与 UrsaCore include 已退役，缺失 include 会作为编译诊断报告，不会回退到旧 Shader。

## 从空 Library 构建并运行

为避免影响当前项目，可把 Demo 的 `Assets` 与 `ProjectSettings` 复制到独立目录，并在副本中通过 Project Graphics 界面选择示例。将该副本的 `Library` 移除或使用全新副本，然后按所需配置重新导入、构建 Pak 并运行 Player：

```powershell
$project = 'D:\UrsaProjects\UniversalSample'
Remove-Item -LiteralPath "$project\Library" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "$project\Build" -Force | Out-Null
Engine/Build/Editor/Release/UrsaAssetImport.exe --project $project --configuration Release
Engine/Build/Release/UrsaPak.exe --project-root $project --configuration Release --output "$project\Build\Content.pak"
Engine/Build/Release/UrsaScriptPlayer.exe --project $project --scene Snake/Snake.ursascene --hidden --frames 8 --capture "$project\Loose.png" --capture-frame 5
Engine/Build/Release/UrsaScriptPlayer.exe --pak "$project\Build\Content.pak" --scene Snake/Snake.ursascene --hidden --frames 8 --capture "$project\Pak.png" --capture-frame 5
```

Loose 与 Pak 的输出应有相同尺寸；Game 相机的绿色/青色分量会按已保存 tint 改变，红色分量保持原值。`UrsaProjectPipelineSamplesTests` 在独立临时项目中从未导入的 Assets 开始运行此对照，并检查 `MaterialShowcase` 几何、Snake Feature 像素及 Loose/Pak 结果。Editor 中可用相同项目打开 Snake 场景并按 Play 检查 Game；点击材质、模型或 Mesh 资产可确认 Preview 正常显示且不继承 Game-only tint。完整 Scene/Game/Inspector Preview 活动管线路由由 `UrsaEditorSrpAcceptanceTests` 覆盖。

## 回退与首批限制

回退时在 Project Graphics Settings 清除默认 Render Pipeline 并应用，`Graphics.json` 的 `renderPipeline` 变为 `null`，恢复默认 Forward。默认 Forward 已使用新的 Universal Lit/Unlit Shader 和共享场景 Pass；不要把已退役的 `Shaders/Renderer3D/*`、`UrsaCore.hlsli` 或旧手写 Shader 属性表恢复为 fallback。清除后重新运行 `UrsaAssetImport` 并重建 Pak，使发布内容与 Graphics 设置同步。

本轮未引入 RenderGraph、Volume、LUT、ACES、Deferred/Forward+、反射探针、TAA、多光源级联或异步计算队列。首批阴影为主方向光 1/2/4 级联和手动 3×3 PCF；项目材质仍使用 Ursa metallic-roughness 合约。URP smoothness 转换为 Ursa perceptual roughness 时使用 `1 - smoothness`，金属粗糙度贴图 B 为 Metallic、G 为 Roughness。具体来源、输入阶段和色彩域差异见 [Shader Authoring](ShaderAuthoring.md)。
