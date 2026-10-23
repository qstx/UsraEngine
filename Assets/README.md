# Engine assets

This directory contains resources built into UrsaEngine. `Shaders/ShaderLibrary/`, `Shaders/Passes/`, `Shaders/Universal/`, and `Shaders/Internal/` hold the shared HLSL library, scene passes, built-in Lit/Unlit shaders, and internal rendering shaders. The Editor or explicit asset-import tool imports and compiles them into the project's Library. Test-only shaders remain with the RHI tests under `Tests/Runtime/Rhi/Shaders/`. Ordinary CMake builds compile programs and do not import these resources.
