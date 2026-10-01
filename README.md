# UrsaEngine

UrsaEngine is a C++17 graphics engine foundation with an API independent RHI and a Vulkan 1.2 backend. GPU tests exercise compute, texture sampling, storage images, depth attachments, offscreen rendering, transfers, and readback. The triangle application lives in the independent sibling `Demo` repository.

## Requirements

- Windows 10 or newer, Visual Studio 2022 with C++ desktop tools, CMake 3.20 or newer.
- Vulkan SDK 1.2 or newer with `glslc`, and a Vulkan 1.2 capable GPU and driver.
- Git submodules containing pinned GLFW 3.5.1 and VMA 3.4.0 sources.

`UrsaRuntime` owns the window, RHI interface, and Vulkan backend. `UrsaEngine` provides the application loop and links to `UrsaRuntime`. CMake aliases `Ursa::Runtime` and `Ursa::Engine` are available to other targets.

## Build and test

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For a new clone, use `git clone --recurse-submodules <repository-url>` or run the submodule update command above before configuring CMake. CMake reads dependency sources from `third_party/` and does not download them during configuration.

Debug builds require `VK_LAYER_KHRONOS_validation`. The engine tests use a headless Vulkan device. When UrsaEngine is included from another CMake project, `URSA_BUILD_TESTS` defaults to `OFF`; when built directly, it defaults to `ON`.

## RHI use

Include `<Ursa/RHI.hpp>`. `rhi::createDevice` chooses the backend; currently `Backend::Vulkan` is implemented. A device exposes adapter details and capabilities and creates buffers, textures, samplers, SPIR-V shaders, descriptor layouts/groups, graphics and compute pipelines, command lists, and a swapchain. Commands require explicit resource barriers and return diagnostic `Result` values. The caller waits on a returned fence before reading GPU output. All RHI handles retain the device context required for their destruction; command lists retain referenced resources until completion.

The first version supports 2D textures with one mip and one layer, one color target and an optional depth target, one descriptor set per pipeline, one graphics/compute queue, and 32-bit indices. These are the implemented limits of the current public API. Vulkan types remain inside the backend source. The shader description tags its bytecode format so future OpenGL and D3D12 implementations can accept their own shader representations.

RHI and window methods are currently intended for one application thread. Shader stage masks in `LayoutBinding` use bit 0 for vertex, bit 1 for fragment, and bit 2 for compute. `Buffer::read` is valid after waiting for the GPU fence that produced its data.

Code uses the repository's `.clang-format` rules, including one blank line between method declarations and definitions. Run `clang-format -i` on changed C++ files before submitting changes.

If CMake cannot detect MSVC or MSBuild reports `MSB6001` because a PowerShell process has duplicate `PATH`/`Path` keys, normalize that process environment before configuring or building:

```powershell
$savedPath = $env:PATH
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:PATH = $savedPath
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```
