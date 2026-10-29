# Shader authoring

内置与项目 Shader 的产物及编译缓存均位于项目 Library，由 Editor 或显式导入工具生成。程序构建不编译 Shader；参见 [项目资源导入管线](AssetImportPipeline.md)。

`.ursashader` 使用接近 Unity ShaderLab 的命名 Shader、Properties、SubShader 和 Pass 结构，程序块使用 HLSL。源码不需要填写 `ID` 或 `Version`；资产 GUID 唯一来自相邻 `.meta`，源语法和导入产物的格式版本由引擎管理。移动或重命名资源时应同时保留 `.meta`。格式仍由 Ursa 定义，不直接导入 Unity `.shader` 或 Shader Graph。

旧源码中的 `ID`/`Version` 声明可作为过渡语法读取，但源码 ID 不参与资产身份解析，也不会用来生成新 `.meta`。源库加载需要有效 `.meta`，Editor 在导入新资源前创建元数据；只读引擎源资源必须随仓库提供元数据。内置 Shader 迁移时保留原 GUID，已有材质引用继续有效。

The canonical sample is [Unlit.ursashader](../Assets/Shaders/Examples/Unlit.ursashader). It uses the layered `ShaderLibrary/Core.hlsl` entry and reads sRGB color textures through their sRGB image format, so hardware sampling decodes them to linear. `UrsaShaderExampleImportTests` imports the asset through VFS, resolves nested library includes, and compiles every declared stage for Vulkan.

## Surface and lighting contract

The public material contract remains metallic-roughness. `SurfaceInput.hlsl` samples metallic from the packed map's B channel and perceptual roughness from G, multiplying each by its scalar factor; base color is sampled as sRGB while metallic-roughness, normal, and occlusion are linear data. Ursa's serialized `_Roughness` is already perceptual roughness. When translating an input expressed as URP smoothness, first use the URP 12.1.16 `PerceptualSmoothnessToPerceptualRoughness` rule `roughness = 1 - smoothness`; for example, smoothness `0.25`, `0.5`, `0.75` maps to Ursa roughness `0.75`, `0.5`, `0.25`. Do not square that value: URP's separate `PerceptualSmoothnessToRoughness` helper derives microfacet roughness for BRDF calculations.

`SurfaceData.hlsl` and `SurfaceInput.hlsl` define the shared material shape and sampling, `InputData.hlsl` defines world-space lighting inputs, `BRDF.hlsl` implements GGX distribution, Schlick-GGX visibility and Schlick Fresnel, and `RealtimeLights.hlsl`/`Lighting.hlsl` evaluate the bounded directional and point-light snapshot. This follows the responsibilities of the pinned URP `ShaderLibrary/{SurfaceData,SurfaceInput,BRDF,RealtimeLights,Lighting}.hlsl` while retaining Ursa's channels, field names, light caps and single-main-light shadow contract. It does not claim URP's full specular workflow, light layers, reflection probes, or identical image output.

`UrsaMaterialRendererTests` verifies visible metallic/roughness, normal, occlusion and emission effects. Its packed-map reference compares G/B sampling against equivalent scalar factors and checks the URP smoothness conversion at `0.75 -> 0.25`; `UrsaShaderReflectionTests` verifies reflected material offsets and cross-stage layout merging. `UrsaShaderExampleImportTests` compiles and renders the layered Unlit shader through Vulkan.

The parser preserves declaration order and source locations. Shader names and resource IDs must be unique in the loaded source library. Supported property types are `Float`, `Range(min, max)`, `Int`, `Color`, `Vector`, and `2D`/`Texture2D`. `Range` defaults must lie within ordered finite bounds. Numeric defaults must be finite; `Color` components are in `[0, 1]` unless `[HDR]` is present. `Color` and `Vector` defaults use four components. The initial 2D texture defaults are empty, `white`, `black`, `gray`, `bump`, and `red`.

Supported property attributes are `[HideInInspector]`, `[HDR]`, `[Normal]`, `[NoScaleOffset]`, `[Gamma]`, `[MainColor]`, and `[MainTexture]`. `HDR` and `MainColor` apply to Color; texture attributes apply to 2D textures. Unknown or incompatible attributes, duplicate property identifiers, invalid values, and unsupported source versions fail import with a source-path and line/column diagnostic.

纹理属性引用引擎或项目已导入的纹理资源，图片文件的解码和导入由纹理导入器负责。

`#include "relative/path.hlsli"` resolves through the same VFS scheme as the `.ursashader` file, relative to the including file. Parent segments are normalized only while the resolved path remains inside that scheme root. Missing files and include cycles fail dependency resolution. Shader names must be unique within the loaded source library. `UsePass "Ursa/Common/Forward"` references a named Pass, and `Fallback "Ursa/Error"` references another Shader by its declared name; missing references and cycles are errors.

## Pass declarations and compilation

The source grammar accepts ordered `SubShader` and `Pass` blocks, `Tags`, `UsePass`, `Fallback`, `CustomEditor`, shared `HLSLINCLUDE` blocks, and `HLSLPROGRAM` blocks. `CustomEditor "Example/MaterialInspector"` stores an Editor registration identifier in the compiled Shader metadata. The universal renderer selects the `UrsaUniversal` SubShader and `UniversalForward`, `ShadowCaster`, `DepthOnly`, or `DepthNormals` Pass according to its consumer. The migrated built-in Lit/Unlit shaders share material layout, skinning, alpha clipping and depth passes. The old scene Shader sources and `UrsaCore.hlsli` include have been retired; Editor ImGui remains a UI-specific pass. `UsePass` entries keep their position in that ordered pass list.

Pass state declarations are retained for the renderer; the first Vulkan implementation supports `Cull`, `ZWrite`, `ZTest`, `Blend`, `BlendOp`, `Stencil`, `ColorMask`, and `Offset`. These map to backend-neutral RHI pipeline fields, and unsupported values return an import or render diagnostic. Stage entry points use `#pragma vertex Name`, `#pragma fragment Name`, or `#pragma compute Name`. A compute-only variant cannot be used by a graphics Pass.

`#pragma shader_feature` and `#pragma shader_feature_local` declare optional keyword choices. `#pragma multi_compile` and `#pragma multi_compile_local` declare required combinations. `_` denotes the no-keyword choice; `shader_feature` also includes that choice when omitted. The importer always retains the default feature combination and all `multi_compile` combinations, then includes optional feature combinations used by project materials. Variant planning is bounded to 256 combinations by default; a project can set a lower limit. A material requesting a variant that was not compiled receives a runtime error.

The Editor lists Shader source beside imported resources. Reimport resolves VFS includes, compiles Vulkan SPIR-V and reflection, updates the compiled Shader asset, and shows source-located errors. Engine and package sources are read-only; their validation imports are staged in memory. See [ShaderMaterials.md](ShaderMaterials.md) for the generated Material Inspector, custom Inspector registration, and package-backed editing workflow.

For example, the documented [Unlit.ursashader](../Assets/Shaders/Examples/Unlit.ursashader) sample compiles through DXC, its reflection drives material/texture bindings, and `UrsaShaderExampleImportTests` renders it through the Vulkan scene renderer and checks the sampled pixel. The same test verifies SubShader selection, ordered passes, blend/depth state, and unsupported-stage diagnostics.

Shader 导入使用 Vulkan SDK 的 `dxc`、`spirv-val` 和 `spirv-cross`，目标为 Vulkan 1.2。普通 HLSL 声明无需填写 `register`；DXC 分配资源槽位后，自动和显式绑定均应用 Ursa 的映射：`b` 保持编号，`t` 偏移 16，`s` 偏移 32，`u` 偏移 48，使用 descriptor set 0。RHI 根据反射创建布局并绑定资源，显式 `register` 仍可用于需要固定槽位的高级用法。共享资源应在同一 Pass 的公共声明中定义；各阶段对同名资源使用不同绑定时导入失败并返回诊断。

编译参数、绑定策略版本和工具版本参与缓存身份。绑定策略升级会使旧导入记录失效，避免复用旧布局；有效的新缓存仍可在 SDK 工具不可用时加载。打包 Player 加载编译产物，不依赖源文件、`.meta` 或编译工具。

## 常用 HLSL 声明

引擎的 `Shaders/ShaderLibrary/Textures.hlsl` 提供 `TEXTURE2D`、`SAMPLER`、`SAMPLE_TEXTURE2D` 和 `SAMPLE_TEXTURE2D_LOD` 声明宏；场景输入、空间变换、颜色、表面、BRDF、实时光照和阴影分别由同目录的分层入口提供。ImGui 只依赖纹理声明层：

```hlsl
Shader "Ursa/Internal/ImGui"
{
    HLSLINCLUDE
    #include "../ShaderLibrary/Textures.hlsl"
    TEXTURE2D(imageTexture);
    SAMPLER(imageSampler);
    ENDHLSL
    // SubShader / Pass / HLSLPROGRAM ...
}
```

普通 `Texture2D`、`SamplerState` 和 `cbuffer` 声明同样支持自动绑定。这些宏提供通用声明和采样写法；它们不实现完整 Unity ShaderLab API。场景 Shader 应使用 `Core.hlsl` 与按需的 `SurfaceInput.hlsl`/`Lighting.hlsl`，不要重新声明 PerObject、PerFrame 或骨骼布局。

## 验证记录

2026-10-12：Windows x64 Debug/Release 构建通过，无编译警告。Debug 全量回归的两项旧格式断言在修正后复验通过，其余 122 项通过；Release 46 项相关回归全部通过。验证包括 `.meta` 身份及移动/复制、跨阶段自动绑定、显式寄存器混用、自动 UAV、反射冲突诊断、宏依赖、ImGui、PBR、蒙皮模型和 Shader 缓存。

实际 Demo 在两种配置中均通过显式导入、材质 Editor、Snake/材质 Player 和材质 pak Player 启动，未发现 Vulkan 验证错误。内置 Shader 原 `.meta` GUID 保持不变。clang-format 19 与 VS 工程目录/filters 检查通过。

2026-10-22：内置 Lit/Unlit 已将原 GUID 迁移到 `Shaders/Universal`，旧单体 PBR/Unlit、Pbr.frag 和 UrsaCore include 已删除。ShaderImporter 导入并编译共享库、场景 Shader 和内部后处理 Shader；产物通过松散内容与 Pak 加载测试。

## 固定 URP 参考、Ursa 映射与迁移

本库按本地 Unity Graphics 2021.3 源码固定实现：SRP Core/URP 12.1.16，提交 `6034ecf6489af2ac1d543f9de25a76b8e91a78cb`。可从 [本地版本清单](../../References/README.md) 和 [机器可读清单](../../References/unity-graphics-reference.json) 核对；以下文件链接指向该检出，不参与 Ursa 编译。

| 固定参考入口 | Ursa 源码 | 采用或适配 |
| --- | --- | --- |
| [Core `Common.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.core/ShaderLibrary/Common.hlsl)、[Core `Color.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.core/ShaderLibrary/Color.hlsl) | `Assets/Shaders/ShaderLibrary/Math.hlsl`, `Color.hlsl` | 基础数学与颜色函数按职责拆分；Ursa 使用自己的命名、Vulkan 边界及 sRGB 目标契约。 |
| [Core `SpaceTransforms.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.core/ShaderLibrary/SpaceTransforms.hlsl) | `Assets/Shaders/ShaderLibrary/SpaceTransforms.hlsl`, `Core.hlsl` | 对应对象/世界/裁剪空间变换；保持 Ursa 列主序矩阵、列向量乘法和 `_UrsaViewProjection` 输入。 |
| [URP `Core.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl)、[URP `Input.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/ShaderLibrary/Input.hlsl) | `Assets/Shaders/ShaderLibrary/Core.hlsl`, `Input.hlsl`, `Textures.hlsl`, `Skinning.hlsl` | 作为场景常用入口与输入布局；Ursa 只声明固定 4 个方向光、8 个点光和自己的骨骼缓冲，不复制 Unity 全局常量布局。 |
| [URP `SurfaceData.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/ShaderLibrary/SurfaceData.hlsl)、[URP `LitInput.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/Shaders/LitInput.hlsl) | `Assets/Shaders/ShaderLibrary/SurfaceData.hlsl`, `SurfaceInput.hlsl`, `InputData.hlsl` | 分离材质表面、材质采样和世界空间光照输入；Ursa 的 `_Roughness` 表示 perceptual roughness。 |
| [URP `BRDF.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/ShaderLibrary/BRDF.hlsl)、[URP `Lighting.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/ShaderLibrary/Lighting.hlsl) | `Assets/Shaders/ShaderLibrary/BRDF.hlsl`, `RealtimeLights.hlsl`, `Lighting.hlsl` | 采用 GGX/Schlick BRDF、直接光职责拆分与 Ursa 场景快照；不声称覆盖 URP 完整 Specular、反射探针或全部光型。 |
| [URP `Shadows.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/ShaderLibrary/Shadows.hlsl) | `Assets/Shaders/ShaderLibrary/Shadows.hlsl` | 采用方向光级联采样职责；Ursa 用 view-depth 选级联、级联过渡/淡出和手动 3×3 PCF。 |
| [URP `Lit.shader`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/Shaders/Lit.shader)、[URP `Unlit.shader`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/Shaders/Unlit.shader)、[URP `LitDepthNormalsPass.hlsl`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/Shaders/LitDepthNormalsPass.hlsl) | `Assets/Shaders/Universal/Lit.ursashader`, `Unlit.ursashader`; `Assets/Shaders/Passes/*Pass.hlsl` | 同一材质布局和顶点变形/Alpha Clip 逻辑供 Forward、ShadowCaster、DepthOnly、DepthNormals 使用；Blend 不参加不透明预处理和首批阴影。 |
| [URP `PostProcessPass.cs::SetupBloom`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/Runtime/Passes/PostProcessPass.cs)、[URP `Bloom.shader`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/Shaders/PostProcessing/Bloom.shader)、[URP `Common.hlsl::ApplyFXAA`](../../References/UnityGraphics-2021.3/Packages/com.unity.render-pipelines.universal/Shaders/PostProcessing/Common.hlsl) | `Assets/Shaders/Internal/Bloom.ursashader`, `FinalPost.ursashader`; `ScriptableRenderer.RecordBloom` | 采用不超过六级的亮部金字塔、FXAA 邻域/方向估计和最终输出职责；Ursa 阈值使用曝光后的线性 HDR 值，省略 soft knee、ACES、LUT 和 Volume。 |

Ursa 材质布局不是 `UnityPerMaterial` 的 ABI 兼容副本。固定顺序位于 `ShaderLibrary/SurfaceInput.hlsl::UrsaPerMaterial`：`_BaseColor`、`_Metallic`、`_Roughness`、`_NormalScale`、`_OcclusionStrength`、`_Emissive`、`_AlphaMode`、`_AlphaCutoff`；`ShaderLibrary/Input.hlsl` 声明 `_UrsaViewProjection`、对象矩阵、场景灯光和骨骼输入。不要在项目 Shader 中重新声明这些共享缓冲。

贴图约定为：`_BaseMap` 与 `_EmissiveMap` 按 sRGB 资源采样；`_MetallicRoughnessMap` 的 **G** 通道乘 `_Roughness`，**B** 通道乘 `_Metallic`；法线和 Occlusion 是线性数据，法线使用 `[Normal]` 并乘 `_NormalScale`。Ursa `_Roughness` 已是 perceptual roughness，直接使用，不平方；从 URP smoothness 转换时使用 `roughness = 1 - smoothness`，例如 `0.75 → 0.25`。Alpha Clip 使用 `_AlphaCutoff`；`_AlphaMode` 为 Opaque=0、Mask=1、Blend=2。

内置 Unlit/Lit 分别保留旧资产 GUID `10000000-0000-4000-8000-000000000001` 和 `10000000-0000-4000-8000-000000000002`，当前资源路径是 `Shaders/Universal/Unlit.ursashader` 与 `Shaders/Universal/Lit.ursashader`。旧 `Shaders/Renderer3D/*` 和 `Shaders/Includes/UrsaCore.hlsli` 已退役，不存在运行时 Shader fallback。版本 1 Unlit 材质会在内存中把 `_MainTex` 幂等升级到 `_BaseMap`；值冲突会拒绝升级。PBR 材质保留 `_Metallic`、`_Roughness`、法线/Occlusion/Emission 标量和纹理采样设置。加载不改写源文件；只有显式保存才写版本 2。迁移冲突、未知字段保留、重复升级和只读内容由 `UrsaMaterialMigrationTests` 覆盖。

相机输入由 `ScriptableRenderPass.Input` 按需声明：Color 从 `AfterOpaques` 起可用，按事件组首个消费者之前复制当前线性目标，同组共享 `_CameraColorTexture`；不同事件重新复制。Depth 与 Normal 从 `BeforeOpaques` 起可用，只绘制不透明 `DepthOnly`/`DepthNormals` 列表。深度纹理为可采样 D32，背景清到 1；法线纹理为 RGBA16F，世界空间 XYZ，背景为 `(0,0,0,0)`。缺少必需 Pass 时错误包含相机、消费者、Shader 和缺失的 LightMode；没有消费者时不会申请相应预处理目标。

当前可复现验证入口：`UrsaShaderExampleImportTests` 通过 DXC 导入/编译新库示例，`UrsaShaderPackageBuild` 从清空后的测试 Library 生成 Pak，`UrsaShaderPackageTests` 验证内置 GUID、元数据、运行产物和旧路径不可加载；`UrsaRenderCommandExecutorTests` GPU 读回验证深度写入/采样、世界法线/中性背景及颜色快照；`UrsaRenderSceneTests` 验证缺失 `DepthNormals` 时按 Shader 身份报错。Windows PowerShell 命令：

```powershell
cmake --build Engine/Build --config Release --target UrsaShaderExampleImportTests UrsaShaderPackageTests UrsaRenderSceneTests UrsaRenderCommandExecutorTests UrsaMaterialMigrationTests UrsaManagedTestAssemblies UrsaDotNetHostTests
$env:TEMP = (Resolve-Path 'Engine\Build\Temp\CodexTestTemp').Path
$env:TMP = $env:TEMP
ctest --test-dir Engine/Build -C Release -R '^(UrsaShaderExampleImportTests|UrsaShaderPackageTests|UrsaRenderSceneTests|UrsaRenderCommandExecutorTests|UrsaMaterialMigrationTests|UrsaDotNetHostSmoke)$' --output-on-failure
dotnet Engine/Build/Tests/Managed/Release/Ursa.ManagedHost.ProtocolTests.dll
```
