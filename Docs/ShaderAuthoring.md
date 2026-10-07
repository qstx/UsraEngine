# Shader authoring

Ursa shader source uses the versioned `.ursashader` format and declares a stable resource `ID` alongside its name. The syntax follows common Unity ShaderLab structure, while the program blocks contain HLSL compiled by the Engine's offline importer. Unity `.shader` files and Shader Graph assets are not imported.

The canonical sample is [Unlit.ursashader](../Assets/Shaders/Examples/Unlit.ursashader), with its shared function in [Common.hlsli](../Assets/Shaders/Examples/Common.hlsli). `UrsaShaderExampleImportTests` imports the asset through VFS, resolves its include, and compiles every declared stage for Vulkan.

The parser preserves declaration order and source locations. Shader names and resource IDs must be unique in the loaded source library. Supported property types are `Float`, `Range(min, max)`, `Int`, `Color`, `Vector`, and `2D`/`Texture2D`. `Range` defaults must lie within ordered finite bounds. Numeric defaults must be finite; `Color` components are in `[0, 1]` unless `[HDR]` is present. `Color` and `Vector` defaults use four components. The initial 2D texture defaults are empty, `white`, `black`, `gray`, `bump`, and `red`.

Supported property attributes are `[HideInInspector]`, `[HDR]`, `[Normal]`, `[NoScaleOffset]`, `[Gamma]`, `[MainColor]`, and `[MainTexture]`. `HDR` and `MainColor` apply to Color; texture attributes apply to 2D textures. Unknown or incompatible attributes, duplicate property identifiers, invalid values, and unsupported source versions fail import with a source-path and line/column diagnostic.

Texture properties reference existing Engine texture resources. This format does not add image-file loading.

`#include "relative/path.hlsli"` resolves through the same VFS scheme as the `.ursashader` file, relative to the including file. Parent segments are normalized only while the resolved path remains inside that scheme root. Missing files and include cycles fail dependency resolution. Shader names must be unique within the loaded source library. `UsePass "Ursa/Common/Forward"` references a named Pass, and `Fallback "Ursa/Error"` references another Shader by its declared name; missing references and cycles are errors.

## Pass declarations and compilation

The source grammar accepts ordered `SubShader` and `Pass` blocks, `Tags`, `UsePass`, `Fallback`, `CustomEditor`, shared `HLSLINCLUDE` blocks, and `HLSLPROGRAM` blocks. `CustomEditor "Example/MaterialInspector"` stores an Editor registration identifier in the compiled Shader metadata. The forward renderer chooses the first SubShader whose `RenderPipeline` tag is absent or equals `UrsaForward`, then executes its `Forward` and `SRPDefaultUnlit` passes in source order. Other `LightMode` values are ignored by the current forward renderer. `UsePass` entries keep their position in that ordered pass list.

Pass state declarations are retained for the renderer; the first Vulkan implementation supports `Cull`, `ZWrite`, `ZTest`, `Blend`, `BlendOp`, `Stencil`, `ColorMask`, and `Offset`. These map to backend-neutral RHI pipeline fields, and unsupported values return an import or render diagnostic. Stage entry points use `#pragma vertex Name`, `#pragma fragment Name`, or `#pragma compute Name`. A compute-only variant cannot be used by a graphics Pass.

`#pragma shader_feature` and `#pragma shader_feature_local` declare optional keyword choices. `#pragma multi_compile` and `#pragma multi_compile_local` declare required combinations. `_` denotes the no-keyword choice; `shader_feature` also includes that choice when omitted. The importer always retains the default feature combination and all `multi_compile` combinations, then includes optional feature combinations used by project materials. Variant planning is bounded to 256 combinations by default; a project can set a lower limit. A material requesting a variant that was not compiled receives a runtime error.

The Editor lists Shader source beside imported resources. Reimport resolves VFS includes, compiles Vulkan SPIR-V and reflection, updates the compiled Shader asset, and shows source-located errors. Engine and package sources are read-only; their validation imports are staged in memory. See [ShaderMaterials.md](ShaderMaterials.md) for the generated Material Inspector, custom Inspector registration, and package-backed editing workflow.

For example, the documented [Unlit.ursashader](../Assets/Shaders/Examples/Unlit.ursashader) sample compiles through DXC, its reflection drives material/texture bindings, and `UrsaShaderExampleImportTests` renders it through the Vulkan `Renderer3D` path and checks the sampled pixel. The same test verifies SubShader selection, ordered passes, blend/depth state, and unsupported-stage diagnostics.

Shader import requires the Vulkan SDK tools `dxc`, `spirv-val`, and `spirv-cross`. DXC compiles HLSL to standard SPIR-V for Vulkan 1.2; the importer validates the output and extracts reflection JSON from the SPIR-V with SPIRV-Cross. Register classes map to descriptor bindings with the Engine convention: `b` stays at its register index, `t` is shifted by 16, `s` by 32, and `u` by 48 within descriptor set 0. The compiler arguments, binding-map version, validator, and reflection tool versions are part of compile cache identity. Packaged runtime loads compiled shader data and does not require these tools.

The authoring format is `Version 1`, owned by Ursa, and does not load Unity `.shader` files. Existing fixed Unlit/PBR material files remain loadable through the planned material migration; they are rewritten only after an explicit successful save. Image-file import and Shader Graph remain out of scope.
