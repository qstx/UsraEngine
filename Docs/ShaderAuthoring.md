# Shader authoring

内置与项目 Shader 的产物及编译缓存均位于项目 Library，由 Editor 或显式导入工具生成。程序构建不编译 Shader；参见 [项目资源导入管线](AssetImportPipeline.md)。

`.ursashader` 使用接近 Unity ShaderLab 的命名 Shader、Properties、SubShader 和 Pass 结构，程序块使用 HLSL。源码不需要填写 `ID` 或 `Version`；资产 GUID 唯一来自相邻 `.meta`，源语法和导入产物的格式版本由引擎管理。移动或重命名资源时应同时保留 `.meta`。格式仍由 Ursa 定义，不直接导入 Unity `.shader` 或 Shader Graph。

旧源码中的 `ID`/`Version` 声明可作为过渡语法读取，但源码 ID 不参与资产身份解析，也不会用来生成新 `.meta`。源库加载需要有效 `.meta`，Editor 在导入新资源前创建元数据；只读引擎源资源必须随仓库提供元数据。内置 Shader 迁移时保留原 GUID，已有材质引用继续有效。

The canonical sample is [Unlit.ursashader](../Assets/Shaders/Examples/Unlit.ursashader), with its shared function in [Common.hlsli](../Assets/Shaders/Examples/Common.hlsli). `UrsaShaderExampleImportTests` imports the asset through VFS, resolves its include, and compiles every declared stage for Vulkan.

The parser preserves declaration order and source locations. Shader names and resource IDs must be unique in the loaded source library. Supported property types are `Float`, `Range(min, max)`, `Int`, `Color`, `Vector`, and `2D`/`Texture2D`. `Range` defaults must lie within ordered finite bounds. Numeric defaults must be finite; `Color` components are in `[0, 1]` unless `[HDR]` is present. `Color` and `Vector` defaults use four components. The initial 2D texture defaults are empty, `white`, `black`, `gray`, `bump`, and `red`.

Supported property attributes are `[HideInInspector]`, `[HDR]`, `[Normal]`, `[NoScaleOffset]`, `[Gamma]`, `[MainColor]`, and `[MainTexture]`. `HDR` and `MainColor` apply to Color; texture attributes apply to 2D textures. Unknown or incompatible attributes, duplicate property identifiers, invalid values, and unsupported source versions fail import with a source-path and line/column diagnostic.

纹理属性引用引擎或项目已导入的纹理资源，图片文件的解码和导入由纹理导入器负责。

`#include "relative/path.hlsli"` resolves through the same VFS scheme as the `.ursashader` file, relative to the including file. Parent segments are normalized only while the resolved path remains inside that scheme root. Missing files and include cycles fail dependency resolution. Shader names must be unique within the loaded source library. `UsePass "Ursa/Common/Forward"` references a named Pass, and `Fallback "Ursa/Error"` references another Shader by its declared name; missing references and cycles are errors.

## Pass declarations and compilation

The source grammar accepts ordered `SubShader` and `Pass` blocks, `Tags`, `UsePass`, `Fallback`, `CustomEditor`, shared `HLSLINCLUDE` blocks, and `HLSLPROGRAM` blocks. `CustomEditor "Example/MaterialInspector"` stores an Editor registration identifier in the compiled Shader metadata. The forward renderer chooses the first SubShader whose `RenderPipeline` tag is absent or equals `UrsaForward`, then executes its `Forward` and `SRPDefaultUnlit` passes in source order. Other `LightMode` values are ignored by the current forward renderer. `UsePass` entries keep their position in that ordered pass list.

Pass state declarations are retained for the renderer; the first Vulkan implementation supports `Cull`, `ZWrite`, `ZTest`, `Blend`, `BlendOp`, `Stencil`, `ColorMask`, and `Offset`. These map to backend-neutral RHI pipeline fields, and unsupported values return an import or render diagnostic. Stage entry points use `#pragma vertex Name`, `#pragma fragment Name`, or `#pragma compute Name`. A compute-only variant cannot be used by a graphics Pass.

`#pragma shader_feature` and `#pragma shader_feature_local` declare optional keyword choices. `#pragma multi_compile` and `#pragma multi_compile_local` declare required combinations. `_` denotes the no-keyword choice; `shader_feature` also includes that choice when omitted. The importer always retains the default feature combination and all `multi_compile` combinations, then includes optional feature combinations used by project materials. Variant planning is bounded to 256 combinations by default; a project can set a lower limit. A material requesting a variant that was not compiled receives a runtime error.

The Editor lists Shader source beside imported resources. Reimport resolves VFS includes, compiles Vulkan SPIR-V and reflection, updates the compiled Shader asset, and shows source-located errors. Engine and package sources are read-only; their validation imports are staged in memory. See [ShaderMaterials.md](ShaderMaterials.md) for the generated Material Inspector, custom Inspector registration, and package-backed editing workflow.

For example, the documented [Unlit.ursashader](../Assets/Shaders/Examples/Unlit.ursashader) sample compiles through DXC, its reflection drives material/texture bindings, and `UrsaShaderExampleImportTests` renders it through the Vulkan `Renderer3D` path and checks the sampled pixel. The same test verifies SubShader selection, ordered passes, blend/depth state, and unsupported-stage diagnostics.

Shader 导入使用 Vulkan SDK 的 `dxc`、`spirv-val` 和 `spirv-cross`，目标为 Vulkan 1.2。普通 HLSL 声明无需填写 `register`；DXC 分配资源槽位后，自动和显式绑定均应用 Ursa 的映射：`b` 保持编号，`t` 偏移 16，`s` 偏移 32，`u` 偏移 48，使用 descriptor set 0。RHI 根据反射创建布局并绑定资源，显式 `register` 仍可用于需要固定槽位的高级用法。共享资源应在同一 Pass 的公共声明中定义；各阶段对同名资源使用不同绑定时导入失败并返回诊断。

编译参数、绑定策略版本和工具版本参与缓存身份。绑定策略升级会使旧导入记录失效，避免复用旧布局；有效的新缓存仍可在 SDK 工具不可用时加载。打包 Player 加载编译产物，不依赖源文件、`.meta` 或编译工具。

## 常用 HLSL 声明

引擎提供 `Shaders/Includes/UrsaCore.hlsli`，支持 `TEXTURE2D`、`SAMPLER`、`SAMPLE_TEXTURE2D`、`SAMPLE_TEXTURE2D_LOD`、`CBUFFER_START` 和 `CBUFFER_END`。例如引擎的 ImGui Shader 使用：

```hlsl
Shader "Ursa/Internal/ImGui"
{
    HLSLINCLUDE
    #include "../Includes/UrsaCore.hlsli"
    TEXTURE2D(imageTexture);
    SAMPLER(imageSampler);
    ENDHLSL
    // SubShader / Pass / HLSLPROGRAM ...
}
```

普通 `Texture2D`、`SamplerState` 和 `cbuffer` 声明同样支持自动绑定。这些宏提供常用的声明和采样写法，并不代表支持 Unity ShaderLab 的全部 API。

## 验证记录

2026-10-12：Windows x64 Debug/Release 构建通过，无编译警告。Debug 全量回归的两项旧格式断言在修正后复验通过，其余 122 项通过；Release 46 项相关回归全部通过。验证包括 `.meta` 身份及移动/复制、跨阶段自动绑定、显式寄存器混用、自动 UAV、反射冲突诊断、宏依赖、ImGui、PBR、蒙皮模型和 Shader 缓存。

实际 Demo 在两种配置中均通过显式导入、材质 Editor、Snake/材质 Player 和材质 pak Player 启动，未发现 Vulkan 验证错误。内置 Shader 原 `.meta` GUID 保持不变。clang-format 19 与 VS 工程目录/filters 检查通过。
