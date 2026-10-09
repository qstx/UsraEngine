# UrsaEngine

UrsaEngine is a C++17 graphics engine foundation with an API independent RHI and a Vulkan 1.2 backend. GPU tests exercise compute, texture sampling, storage images, depth attachments, offscreen rendering, transfers, and readback. The playable Snake application lives in the independent sibling `Demo` repository.

## Requirements

- Windows 10 or newer, Visual Studio 2022 with C++ desktop tools, CMake 3.20 or newer.
- Vulkan SDK 1.2 or newer with `glslc`, and a Vulkan 1.2 capable GPU and driver.
- Git submodules containing pinned GLFW 3.5.1, VMA 3.4.0, nlohmann/json 3.12.0, EASTL 3.21.23 (`7fadbf0`), its matching EABase (`123363e`), spdlog 1.13.0 (`7c02e20`), Assimp 6.0.5 (`392a658f`), stb (`2c980bb5`), Dear ImGui `v1.92.9b-docking` (`b48d1af`), and ImGuizmo `1.10` (`b796ac3`) sources.

Initialize the top-level dependencies after checkout with `git submodule update --init`. To verify the Editor dependencies recursively, run `git submodule update --init --recursive third_party/imgui third_party/imguizmo`. EABase also declares upstream test-only submodules over SSH; they are not needed for this Engine build. All submodule Git links are fixed to the revisions above.

`UrsaRuntime` owns the window, RHI interface, and Vulkan backend. `UrsaEngine` provides the application loop and links to `UrsaRuntime`. The optional `UrsaEditorCore` target (alias `Ursa::Editor`) owns the Editor UI and links to `Ursa::Engine`. CMake aliases `Ursa::Runtime`, `Ursa::Engine`, and `Ursa::Editor` are available to other targets when the Editor is enabled.

## Source layout

`Runtime/Core`, `Runtime/Vfs`, `Runtime/Window`, and `Runtime/Rhi` contain the runtime modules. Vulkan implementation files live under `Runtime/Rhi/Vulkan`. `Engine/Application`, `Engine/Math`, `Engine/Material`, `Engine/Scene`, `Engine/Renderer3D`, and `Engine/Serialization` contain the engine modules. `Editor/Application` and `Editor/Ui` contain the Editor loop and UI bridge. Headers and corresponding source files sit directly in each module directory. Include public headers by module path, such as `<Rhi/RHI.hpp>` and `<Renderer3D/Renderer3D.hpp>`. Engine-owned resources, including the renderer's shaders, live under `Assets/`. The Editor creates a generated, rebuildable `Library/` directory beside `Assets/`; keep it out of source control. Tests are grouped by module under `Tests/`.

CMake discovers `.cpp` and `.hpp` files within the listed module directories and checks for additions or removals during a build. New modules and test executables still need a CMake target or directory entry.

## Asset metadata

The Editor AssetDatabase stores a versioned JSON `.meta` sidecar next to each
asset and directory. A sidecar owns the stable GUID, importer identity/version,
and import settings; source bytes remain unchanged. In the Editor, `prj:` is the
VFS address for the project's `Assets/` directory, so `prj:Models/Hero.fbx`
appears as `Assets/Models/Hero.fbx` in project UI and its metadata is stored as
`Assets/Models/Hero.fbx.meta`.

Use the AssetDatabase move operation to move/rename a source together with its
sidecar; the GUID remains stable. Copying creates fresh GUIDs for the copy and
its imported source tree. If duplicate GUID sidecars are found during refresh,
the first sorted asset keeps its identity and later copies receive new GUIDs.
Missing sidecars are created on writable project mounts. A read-only project
must already contain valid metadata. The generated project `Library/` directory
is mounted as `lib:` only by the Editor, is ignored by source control, and can be
deleted and rebuilt from assets and their sidecars.

Before switching an existing asset root to the new identity format, back up the
project and run the migration tool once per asset root:

```powershell
Engine/Build/Debug/UrsaAssetMigrate.exe --assets Engine/Assets --library Engine/Library
Engine/Build/Debug/UrsaAssetMigrate.exe --assets Demo/Assets --library Demo/Library
```

The tool seeds `.meta` GUIDs from IDs embedded in supported `.ursa*` resources,
converts known external references to `{guid, localFileId}`, and leaves scene
object/component IDs alone. It writes a recovery manifest and copies changed
files into `Library/Migrations/<id>`. To restore a reported backup, pass its
`lib:Migrations/<id>` path with `--restore`; refresh the Editor workspace after
restoration.

See [Docs/TextureImport.md](Docs/TextureImport.md) for image formats, settings,
previews, and the runtime artifact path.

See [Docs/ModelImport.md](Docs/ModelImport.md) for FBX/OBJ import settings,
hierarchy and sub-assets, materials, dependencies, and current format limits.

## Logging

`LogSession` initializes the spdlog-backed logger and flushes on shutdown. Levels are
Trace, Debug, Info, Warn, Error, Critical and Off. Debug builds default to Debug;
Release defaults to Info. Use `URSA_LOG_INFO("Engine", message)` and the corresponding
level macros. Logging before initialization or after shutdown falls back to stderr.

Default rotating files live at `user:Logs/<executable>.log`, under the platform user
data directory. `Log::Initialize(sharedFilesystem, virtualPath, config)` binds an
explicit `user:` mount. `LogConfig::filePath` is a bootstrap override for fixtures
and portable layouts; file operations still go through a directory provider.

## Public container types

Include `<Core/Containers.hpp>` and use `Ursa::String`, `Ursa::Vector<T>`, `Ursa::Array<T, N>`, `Ursa::Map<K, V>`, `Ursa::UnorderedMap<K, V>`, `Ursa::Set<K>`, and `Ursa::Deque<T>` for engine-owned strings and collections. These names currently map to EASTL and keep vendor types out of consumer source. Existing code that passed `std::string` or `std::vector` to Runtime APIs must switch to these aliases or convert explicitly. Smart pointers, `std::function`, and filesystem paths retain their standard-library types.

```cpp
#include <Core/Containers.hpp>
#include <Rhi/RHI.hpp>

int main() {
    Ursa::Vector<uint8_t> code{1, 2, 3, 4};
    Ursa::Rhi::ShaderDesc shader;
    shader.code = code;
    shader.entry = Ursa::String("main");
    return shader.code.size() == 4 && shader.entry == "main" ? 0 : 1;
}
```

The same migration applies to Engine scene, material, renderer, and resource APIs. Rebuild Engine and consumers together. Keep `std::string` at APIs that require it, such as JSON, streams, exceptions, and `std::filesystem`; convert at the call site with `<Core/StringInterop.hpp>`:

```cpp
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <nlohmann/json.hpp>

void inspectId(Ursa::ResourceId id) {
    nlohmann::json record{{"id", Ursa::toStdString(id.toString())}};
    auto restored = Ursa::ResourceId::parse(
        Ursa::toUrsaString(record.at("id").get<std::string>()));
    if (!restored)
        return;
    auto file = Ursa::pathFromUrsaString(Ursa::String("Assets/sample.ursamat"));
    auto displayPath = Ursa::pathToUrsaString(file);
}
```

## Build and test

```powershell
git submodule update --init
git submodule update --init --recursive third_party/imgui third_party/imguizmo
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build Build --config Debug
ctest --test-dir Build -C Debug --output-on-failure
cmake --build Build --config Release
ctest --test-dir Build -C Release --output-on-failure
```

For a new clone, run the submodule update commands above before configuring CMake. A full recursive checkout may attempt EABase's unrelated test-only SSH submodules. CMake reads dependency sources from `third_party/`, including EABase through a local FetchContent source override; configuration does not download dependencies.

## Editor scene and Prefab authoring

To edit a scene, enter `level.ursascene` in **New asset**, click **New Scene**, then select a GameObject in **Hierarchy**. Hierarchy shows GameObject names and nesting. Inspector owns the selected object's name, active state, transform, and component sections. Use **Add Component** at the bottom of Inspector to search registered component types by name and category. Component sections expose enabled state, properties, and removal. The same Inspector structure works for Scene objects and detached Prefab GameObjects. Select a saved material on a Box Renderer; the file stores its stable material ID. The **Viewport** always has an editor camera, even when the scene has no game camera. Select a nested GameObject and use **Translate**, **Rotate**, or **Scale** with the gizmo; the world-space movement is converted back to that GameObject's local transform. Zero scale, singular parent transforms, shear, and non-finite gizmo results are rejected with a diagnostic. Save, close, and reopen the scene to check its name, transform, order, component fields, and material reference. Prefab assets are edited as independent GameObject graphs; the Editor creates a temporary preview Scene only to render the graph.

See [Inspector components and Editor extensions](Docs/EditorComponents.md) for component descriptors, custom property drawers, tool registration, and the future script-provider boundary.

For a linked instance, first save its Prefab source. Open a scene, select a regular parent node, and choose that Prefab from **Place Prefab**; each placement receives its own instance ID. Editing an instance node or its components changes only that placement. **Pin label override** records an explicit property override; hierarchy, component, and transform changes are captured as property or structure overrides on save. Open the `.ursaprefab` itself to edit the shared source. The next explicit scene reload merges source changes with saved instance overrides. A missing override target or invalid merge is reported, and the active scene and saved patch remain available. `UrsaSceneDraftTests` checks two placements, source isolation, and round-trip structure; `UrsaPrefabTests` checks source merges and conflict preservation.

Native Engine components use Serializable factories and the Editor's component descriptor API. Project gameplay and project-specific Inspector, menu, and window extensions are authored in C# under the project's `Assets/Scripts/` and `Assets/Scripts/Editor/` folders. `Ursa.Runtime` supplies built-in GameObject, Transform, input, time, camera, renderer, and asset-reference APIs; `Ursa.Editor` supplies `CustomInspector`, `MenuItem`, and `EditorWindowMenu` extensions. The Demo project exercises these APIs without project-owned C++ registration.

Click **Run** on a scene to clone the current effective edit scene in memory, including unsaved edits. The copy receives fresh project runtime services from `playContextSetup`; a missing service reports an error and leaves Edit mode intact. Engine `FrameScheduler` drives Update, fixed updates, and Late Update on the copy. The game uses the scene's active camera; focus the viewport image to send keyboard input to the game. Input sent to other Editor panels stays with the UI. **Save** and document switching are disabled during Play. Click **Stop** to destroy the play copy; the edit scene, dirty flag, and asset files remain as they were before Run. `UrsaPlaySessionTests` covers clone isolation, lifecycle order, service failure, save blocking, and cleanup.

## Editor UI bridge

The ImGui platform bridge forwards key and modifier state and uses the window's UTF-8 system clipboard callbacks. Editor text fields support Ctrl+C, Ctrl+X, and Ctrl+V with the Windows clipboard.

The Editor is enabled by default (`-DURSA_BUILD_EDITOR=ON`). For the C# Demo project, configure Engine with `-DURSA_ENABLE_DOTNET=ON -DURSA_PROJECT_DIR=../Demo`, build the `UrsaEditor`, `UrsaScriptPlayer`, `UrsaScriptCompile`, and `UrsaPak` targets, and launch the generic Editor with `UrsaEditor --project ../Demo --scene Snake/Snake.ursascene`. The generic Editor accepts a project root and an optional scene path relative to `Assets/`. `--hidden --frames 3` runs a short window smoke test. The docked workspace provides Hierarchy, Scene, Inspector, and Project panels, with Run/Stop in the top bar.

### C# project workflow

在工作区根目录配置并构建 Demo：

```powershell
cmake -S Engine -B Engine/Build -DURSA_ENABLE_DOTNET=ON -DURSA_PROJECT_DIR=Demo
cmake --build Engine/Build --config Debug --target UrsaEditor UrsaScriptPlayer UrsaScriptCompile UrsaPak
Engine/Build/Debug/UrsaScriptCompile.exe Demo Debug
ctest --test-dir Engine/Build -C Debug --output-on-failure -R 'Ursa(Project|Snake)'
```

在 Demo 中运行独立 Player 可使用 `UrsaScriptPlayer --project Demo --scene Snake/Snake.ursascene`，也可通过 `--pak Content.pak` 运行打包内容。Engine 的 `URSA_PROJECT_DIR` 选项启用针对项目资源的 Scene/Prefab、导入、脚本、Editor、运行时和打包集成测试。

To author project assets, pass a writable asset directory such as `--project ..\Demo\Assets`. The Project panel shows the selected root, lists each file's stable ID, and marks Engine assets as read-only. In the **New asset** field, enter `Checker.ursatex`, click **New texture**, edit its pixels or color space, and click **Save**. The new file appears only under the selected project root. Use **Refresh assets** after an external rename; references continue to use the stored ID. Switching or closing a changed document offers **Save**, **Discard**, and **Cancel**. Saving first writes a staged resource, validates its ID and kind, then atomically replaces the destination; a failed save leaves the previous file and the in-memory edit available. Paths outside the project root, including paths reached through symlinks, are rejected. `UrsaProjectWorkspaceTests`, `UrsaEditorDocumentsTests`, and `UrsaTextureDraftTests` exercise these rules.

The texture inspector edits dimensions (up to 2048 × 2048), linear or sRGB color space, an individual RGBA8 pixel, and a generated checker pattern. Its grid shows the current pixels before saving. Image file import is not included. To try a material, enter `Surface.ursamat` in **New asset**, click **New Unlit** or **New PBR**, adjust the color, alpha, sampling, and applicable PBR controls, then select a saved texture in the matching slot. The material preview renders a box with the current values. Save the material and reopen it from the browser; texture references retain their resource IDs. Base-color and emissive slots require sRGB textures; data slots require linear textures. The inspector reports incompatible choices and keeps the last accepted value. `UrsaMaterialDraftTests` creates a procedural texture, saves two materials that share its ID, reopens both, checks invalid parameters, and reads back the 3D preview.

`Window` owns GLFW callbacks and publishes key, character, pointer, scroll, and focus events to subscribers. `ImGuiBridge` subscribes to those events without replacing the game's polled input. The Editor calls `beginFrame`, builds widgets, calls `endFrame`, then records ImGui draws inside an RHI color pass with `record`. `endFrame` uploads Dear ImGui font atlas requests through RHI. Project images need a sampled RHI texture in `ShaderRead` state and a sampler; `registerTexture` returns the opaque ID passed to `ImGui::Image`, and `unregisterTexture` releases its UI binding. The Editor waits for each submitted frame fence before replacing transient vertex and index buffers. The bridge uses RHI only and keeps Vulkan types inside the backend.

Debug builds require `VK_LAYER_KHRONOS_validation`. The engine tests use a headless Vulkan device. When UrsaEngine is included from another CMake project, `URSA_BUILD_TESTS` defaults to `OFF`; when built directly, it defaults to `ON`.

## RHI use

Include `<Rhi/RHI.hpp>`. `Ursa::Rhi::CreateDevice` chooses the backend; currently `Backend::Vulkan` is implemented. A device exposes adapter details and capabilities and creates buffers, textures, samplers, SPIR-V shaders, descriptor layouts/groups, graphics and compute pipelines, command lists, and a swapchain. Commands require explicit resource barriers and return diagnostic `Result` values. The caller waits on a returned fence before reading GPU output. All RHI handles retain the device context required for their destruction; command lists retain referenced resources until completion.

The first version supports 2D textures with one mip and one layer, one color target and an optional depth target, one descriptor set per pipeline, one graphics/compute queue, and 32-bit indices. These are the implemented limits of the current public API. Vulkan types remain inside the backend source. The shader description tags its bytecode format so future OpenGL and D3D12 implementations can accept their own shader representations.

`Format::RGBA8Unorm` samples RGBA bytes as linear values. `Format::RGBA8Srgb` decodes RGB from sRGB to linear when sampled; alpha stays linear. Use the latter for color textures and the former for normal, metallic/roughness, and occlusion data. Both are four bytes per pixel. For example, an sRGB byte value of 128 samples to about 0.216 in a linear shader, while an UNORM byte value of 128 samples to about 0.502. The sampled texture can be uploaded with `copyBufferToTexture` after a transfer-destination barrier, then transitioned to `ShaderRead`.

`CommandList::drawIndexed(indexCount, instanceCount, firstInstance)` selects a range of instances. `firstInstance` defaults to zero, so existing two-argument calls remain valid. A vertex shader can read the selected instance through `gl_InstanceIndex` in SPIR-V. The RHI GPU test draws two indexed triangles with `drawIndexed(3, 1)` and `drawIndexed(3, 1, 1)` and checks their separate colors after readback. From the Engine repository, run `ctest --test-dir out/build -C Debug -R '^UrsaRhiTests$' --output-on-failure` after building `UrsaRhiTests` to verify both this case and sRGB sampling.

RHI and window methods are currently intended for one application thread. Shader stage masks in `LayoutBinding` use bit 0 for vertex, bit 1 for fragment, and bit 2 for compute. `Buffer::read` is valid after waiting for the GPU fence that produced its data.

Code uses the repository's `.clang-format` rules, including one blank line between method declarations and definitions. Run `clang-format -i` on changed C++ files before submitting changes.

## Virtual file system

引擎生成资源按配置存放于 `Build/Resources/<配置>`，编译缓存位于 `Build/Intermediate/ShaderCompileCache`。项目导入与脚本数据归属项目 Library。旧配置迁移、缺失文件恢复及打包说明见 [构建资源与项目缓存](Docs/BuildResources.md)。

`<Vfs/VirtualFileSystem.hpp>` addresses runtime content through `scheme:relative/path` virtual paths. The directory provider is used in Editor and loose development runs; a Pak provider serves packaged content. Reads, range reads, listing, atomic writes, and appends all pass through the VFS provider interface. `VirtualPath::Parse` rejects host drive letters, absolute paths, and any `..` segment. The directory provider checks canonical paths against its mount root, including Windows junctions.

```text
ursa:Shaders/Renderer3D/Unlit.ursashader       -> Engine/Assets
ursa:Shaders/Renderer3D/Unlit.ursashaderasset  -> Build/Resources/<configuration>
prj:Snake/Snake.ursascene              -> selected project's Assets directory
user:Logs/UrsaEditor.log                 -> per-user application data directory
```

```cpp
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/VirtualFileSystem.hpp>

Ursa::Result<void> OpenEditorResources(const std::filesystem::path &engineAssets,
                                       const std::filesystem::path &generatedAssets,
                                       const std::filesystem::path &projectAssets,
                                       const std::filesystem::path &projectLibrary,
                                       const std::filesystem::path &userData) {
    Ursa::RuntimeMountProfileConfig profile;
    profile.mode = Ursa::RuntimeMountMode::Editor;
    profile.applicationId = "UrsaEditor";
    profile.engineDirectories = {engineAssets, generatedAssets};
    profile.projectAssetsDirectory = projectAssets;
    profile.projectLibraryDirectory = projectLibrary;
    profile.userDataDirectoryOverride = userData;
    auto vfs = Ursa::OpenRuntimeFileSystem(profile);
    if (!vfs)
        return Ursa::Result<void>::Failure(vfs.error);

    auto shader = Ursa::VirtualPath::Parse("ursa:Shaders/Renderer3D/Unlit.ursashaderasset");
    if (!shader)
        return Ursa::Result<void>::Failure(shader.error);
    auto bytes = vfs.value.Read(shader.value);
    if (!bytes)
        return Ursa::Result<void>::Failure(bytes.error);

    auto assetPath = Ursa::VirtualPath::Parse("prj:Generated/ShaderCopy.bin");
    if (!assetPath)
        return Ursa::Result<void>::Failure(assetPath.error);
    auto saved = vfs.value.WriteAtomic(assetPath.value, bytes.value);
    if (!saved)
        return saved;

    auto logPath = Ursa::VirtualPath::Parse("user:Logs/UrsaEditor.log");
    if (!logPath)
        return Ursa::Result<void>::Failure(logPath.error);
    const Ursa::Vector<Ursa::UInt8> message{'o', 'k', '\n'};
    return vfs.value.Append(logPath.value, message);
}
```

`RuntimeMountMode::Editor` makes `ursa:` read-only, `prj:` read-write, and `user:` writable. `LooseRuntime` makes `ursa:` and `prj:` read-only while keeping `user:` writable. `PackagedRuntime` mounts read-only providers for both `ursa:` and `prj:` from the package and keeps writes in `user:`. Pass the package's scheme-specific provider views in `RuntimeMountProfileConfig::packagedProviders`.

Multiple providers for one scheme form an ordered overlay: the first provider containing a virtual path wins. Writes use the first provider and require it to be read-write. `Write` is an atomic replacement, `WriteAtomic` names that behavior explicitly, and `Append` is available for append-only streams such as logs. VFS diagnostics identify virtual paths and provider labels rather than making host paths part of resource identity. `UrsaVirtualFileSystemTests` covers parsing, permissions, overlays, range reads, atomic replacement, append, mount profiles, and mount-root isolation.

## Resource files, scenes and Prefabs

See [resource I/O and deployment](Docs/Resources.md) for virtual-path APIs, Editor
transactions, stable IDs, dependency loading, user data and single-file packages.
See [persistent serialization](Docs/Serialization.md) for Serializable model methods,
component factories, JSON/binary archives and deferred references.

`UrsaResourceVfsTests` exercises texture/material/Scene/PrefabAsset loads from both
directories and Pak, binary Scene serialization, reusable Prefab instances, and
failed writes/reloads. `UrsaPrefabTests` covers property/structure overrides, source
updates and explicit reload. Linked scenes retain source and instance IDs; use
`SetPrefabPropertyOverride`, `ClearPrefabPropertyOverride` and
`SetPrefabStructureOverride` before saving through the VFS.

### Build a package

```powershell
cmake --build Build --config Debug --target UrsaEnginePackage
```

The Engine-only package is `build/package/Content.pak`. Demo's `UrsaDemoPackage`
target includes project content and the current `Demo/Library` runtime imports in
the same file and supports runtime `--pak` startup. `UrsaPak` stages imported
artifacts and a deterministic GUID/local-file-ID catalog into the package while
filtering `.meta`, `Library`, and raw importer-only source files. Run the Editor
to import changed assets before packaging; stale imports stop package creation.

## Runtime materials

Include `<Material/Material.hpp>`. `MaterialInstance` owns editable CPU parameters and immutable `MaterialTexture` references; it does not require a device. Share a `std::shared_ptr<MaterialInstance>` between boxes for synchronized changes, or call `clone()` before changing one box independently. Successful setters increment `version()`; rejected input leaves both parameters and version unchanged.

```cpp
#include <Material/Material.hpp>
#include <memory>

auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::PbrMetallicRoughness);
auto pixels = Ursa::MaterialTexture::create(1, 1, Ursa::TextureColorSpace::Srgb,
                                            {220, 80, 40, 255});
if (!pixels || !material->setTexture(Ursa::MaterialTextureSlot::BaseColor, pixels.value) ||
    !material->setMetallic(0.2f) || !material->setRoughness(0.6f))
    return 1;
auto independent = material->clone();
```

Pixel input is tightly packed RGBA8, exactly `width * height * 4` bytes; dimensions must be positive. Base-color and emissive textures are sRGB, while metallic/roughness, normal, and occlusion textures are linear. The metallic/roughness texture uses green for roughness and blue for metallic; the occlusion texture uses red. Missing maps resolve to 1×1 neutral multipliers: white base color, roughness and metallic channels both 1, flat normal, occlusion 1, and white emissive. The default metallic and emissive *factors* are zero. Unlit accepts only a base-color texture and base-color parameters.

Base-color channels and alpha, metallic, roughness, occlusion strength, and alpha cutoff must be finite values from 0 to 1. Normal scale and emissive RGB must be finite and nonnegative. `AlphaMode` accepts `Opaque`, `Mask`, and `Blend`; `setAlphaMode` also accepts a mask cutoff. Sampler settings choose linear or nearest filtering and repeat or clamp addressing. `UrsaMaterialHeaderSmoke` compiles and runs the example above; `UrsaMaterialTests` checks validation, sharing, cloning, and texture defaults.

## Basic 3D math

Include `<Math/Math.hpp>` for `Vec3`, `Mat4`, `Transform`, and `Camera`, or include the corresponding type header directly. Each type has its own header and source file under `Engine/Math/`. The world is right-handed with +Y up; a board can lie on the XZ plane. Matrices are column-major and multiply column vectors. Perspective projection uses a 0-to-1 depth range, and its aspect argument is framebuffer width divided by height.

```cpp
#include <Math/Math.hpp>

Ursa::Camera camera;
camera.position = {0.0f, 12.0f, 18.0f};
camera.target = {0.0f, 0.0f, 0.0f};
const Ursa::Mat4 viewProjection = camera.viewProjection(16.0f / 9.0f);
const Ursa::Mat4 model = Ursa::Transform{{2.0f, 0.5f, 3.0f}, {1.0f, 1.0f, 1.0f}}.matrix();
const Ursa::Mat4 clipFromModel = viewProjection * model;
```

This example is compiled in the sibling Demo repository's public-header smoke test.

## Runtime scene hierarchy

`GameObject` is the object model for names, local transforms, components, activation, and ordered child objects. It can be constructed and configured before it is attached to a runtime `Scene`. A detached object's components can query their owner with `GetGameObject()`; `TryGetScene()` returns `nullptr` until a Scene attaches the object. `GetScene()` reports an error when called while detached. Destroying the GameObject invalidates all its external handles and destroys its children.

```cpp
#include <Scene/GameObject.hpp>
#include <Scene/RenderingComponents.hpp>

auto root = Ursa::GameObject::Create("Root");
auto box = root->AddComponent<Ursa::BoxRenderComponent>();
auto owner = box.value->GetGameObject();
```

Include `<Scene/Scene.hpp>` to attach and run GameObject trees. `Scene::CreateGameObject` creates and attaches a runtime object; `Scene::AttachGameObject` adds a detached root tree. Scene updates and renders only attached objects. `GameObject::Ptr` references identify the object itself; `IsValid()` becomes false after that object or an ancestor is destroyed. Reparenting preserves local transforms and rejects cycles.

```cpp
#include <Scene/Scene.hpp>

Ursa::Scene scene;
auto root = Ursa::GameObject::Create("Root");
auto child = Ursa::GameObject::Create("Child");
if (!child->SetParent(root.get()) || !scene.AttachGameObject(root))
    return 1;
Ursa::LocalTransform local;
local.position = {1.0f, 0.0f, 0.0f};
if (!child->SetLocalTransform(local))
    return 1;
const Ursa::Mat4 world = child->WorldTransform().value;
```

Each GameObject has local position, quaternion rotation, scale, and an enabled flag. World transforms combine ancestor and local transforms; disabling a parent also makes its descendants inactive. The `UrsaGameObjectTests` and `UrsaSceneGraphTests` targets exercise detached object trees, Scene attachment, handles, transforms, activation, and rendering-data collection.

Components derive from `<Scene/Component.hpp>`'s `Component` and are owned by their GameObject. Use `GameObject::AddComponent<T>()`, `FindComponent<T>()`, and `FindComponents<T>()`; use `Scene` component operations when a persistent `ComponentId` is needed. Multiple components of the same type may be attached to one GameObject. `ComponentId` handles are invalidated on removal. An inactive parent or disabled component pauses callbacks without restarting it after reactivation.

For each active component, `onStart` runs once immediately before its first phase callback. The application then invokes `onUpdate`, zero or more `onFixedUpdate` calls, and `onLateUpdate` in that order. Parent components run before child components; components on one node run in attachment order. `onRemove` runs exactly once when a component is removed, its node is destroyed, or the scene is cleared. It also runs for a component that never started. Callbacks receive `FrameContext`, containing a key-state snapshot, frame time, fixed step, frame number, and a window pointer for operations such as changing the title. Returning a failed `Result` stops that frame with a diagnostic error.

Creation, removal, and reparenting requested during a callback are committed after the current phase. A removal immediately invalidates its handle and suppresses later callbacks in that frame; the object is cleaned at the phase boundary. Newly attached behavior begins on the following frame, while a new GameObject can be rendered after its attachment is committed. Invalid handles and obvious hierarchy cycles fail at request time; a conflict discovered during commit fails the phase. The `UrsaSceneGraphTests` lifecycle and deferred-change cases cover these rules.

## Application frame loop

An `Application` may assign `scene_` during `onStart`. Each drawable frame polls window events, snapshots keys, clamps elapsed time to 0.25 seconds, runs scene `onUpdate`, executes due fixed steps, runs `onLateUpdate`, then gathers scene rendering data and presents. Scene commands commit at the end of each behavior phase; the frame renders the state left by late update. A subclass without a scene keeps using `onFrame(deltaSeconds)` as before.

The fixed step defaults to 1/60 second and at most five steps per frame. `configureFixedStep(seconds, maxSteps)` changes both. Remaining time below one step carries forward; after the step cap, whole-step backlog is discarded. When the framebuffer has zero size, the loop waits without updating or presenting and resets its time and fixed-step accumulator. It resumes without simulating the paused interval. A failed component callback stops the loop, returns its diagnostic error, and cleans scene components once. `UrsaFrameSchedulerTests` and `UrsaApplicationSceneTests` cover the phase order, timing, cleanup, and legacy callback path.

## Basic 3D rendering

Include `<Renderer3D/Renderer3D.hpp>` to draw boxes with a camera. The renderer generates one cube mesh, uploads a frame's transforms and colors, records RHI commands, manages a depth target, and rebuilds size-dependent resources after a resize. It waits for its previous GPU submission before reusing upload resources.

```cpp
auto created = Ursa::Renderer3D::create(device);
if (!created)
    return Ursa::Result<void>::failure(created.error);
auto renderer = std::move(created.value);
Ursa::Vector<Ursa::BoxInstance> boxes{
    {{{0.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}, {0.2f, 0.8f, 0.3f, 1.0f}},
};
auto frame = renderer->render(swapchain, camera, boxes);
```

`render` acquires and presents a swapchain image. `renderOffscreen` accepts an RHI color texture with color-attachment and transfer-source usage and returns a fence; after waiting for it, the texture is in transfer-source state for readback. The current renderer supports one cube mesh, positive per-axis scaling, one depth target, and one in-flight frame. Its SPIR-V shaders are compiled into the generated asset mirror and read through `ursa:Shaders/Renderer3D/`.

## Scene rendering

Attach `CameraComponent` and `BoxRenderComponent` to GameObjects. The camera looks along its GameObject's local negative Z axis; its world transform supplies the view, and its fields specify perspective projection. The Scene gathers active boxes with complete world matrices, including ancestor rotation and scale. Inactive GameObjects and disabled components are omitted. Without an enabled active camera, `CollectRenderData(aspect)` and an `Application` scene frame return a diagnostic error.

```cpp
#include <Scene/Scene.hpp>

Ursa::Scene scene;
auto cameraObject = Ursa::GameObject::Create("Camera");
Ursa::LocalTransform cameraPose;
cameraPose.position = {0.0f, 0.0f, 4.0f};
cameraObject->SetLocalTransform(cameraPose);
auto camera = cameraObject->AddComponent<Ursa::CameraComponent>();
auto boxObject = Ursa::GameObject::Create("Box");
boxObject->AddComponent<Ursa::BoxRenderComponent>(Ursa::Color{0.2f, 0.8f, 0.3f, 1.0f});
scene.AttachGameObject(cameraObject);
scene.AttachGameObject(boxObject);
scene.SetActiveCamera(camera.value->Id());
```

An application assigns its scene to `scene_` during `onStart`; `Application` owns the renderer for that path. Direct rendering code can call `collectRenderData(aspect)` and pass the returned view-projection matrix, boxes, lighting and camera position to `Renderer3D::renderModels` or `renderOffscreenModels`. The current path supports one explicitly selected camera and colored or material-bound cubes; arbitrary meshes and multiple active cameras are outside this phase. `UrsaRendererTests` exercises the example's collection path, inherited transforms, color, visibility, and missing-camera error.

## Scene lighting

Attach `DirectionalLightComponent` or `PointLightComponent` to GameObjects. A directional light shines along its GameObject's local negative Z axis, transformed to world space. A point light uses its GameObject's world position, intensity, color, and positive finite range. Disabling a light component, its GameObject, or an ancestor removes it from the collected frame. `Scene::SetAmbientLight(color, intensity)` sets nonnegative ambient fill. Invalid values return a diagnostic `Result` and preserve the previous value.

```cpp
Ursa::Scene litScene;
auto cameraObject = Ursa::GameObject::Create("Camera");
auto camera = cameraObject->AddComponent<Ursa::CameraComponent>();
auto sunObject = Ursa::GameObject::Create("Sun");
sunObject->AddComponent<Ursa::DirectionalLightComponent>();
auto lampObject = Ursa::GameObject::Create("Lamp");
lampObject->AddComponent<Ursa::PointLightComponent>();
litScene.AttachGameObject(cameraObject);
litScene.AttachGameObject(sunObject);
litScene.AttachGameObject(lampObject);
litScene.SetActiveCamera(camera.value->Id());
litScene.SetAmbientLight({1.0f, 1.0f, 1.0f, 1.0f}, 0.1f);
auto frame = litScene.CollectRenderData(16.0f / 9.0f);
```

The returned `SceneRenderData::lighting` is a value snapshot; later scene changes do not alter it. `SceneLighting::maxDirectionalLights` is 4 and `maxPointLights` is 8. Collecting a frame with more active lights returns an error with the actual count and capacity. `UrsaLightingHeaderSmoke` compiles and runs the example, and `UrsaLightingTests` checks transforms, activation, snapshots, validation, and capacity.

## Material scene rendering

Assign a shared `MaterialInstance` to `BoxRenderComponent::material`. When it is set, the material controls the box's appearance; `color` is used only by boxes without an explicit material. The material is independent of box geometry. `collectRenderData` captures active box transforms, material references, camera position, and scene lights after the scene update phases. `Renderer3D` reads material values once before recording that frame. In a single-threaded application, edits made during update or late update therefore appear in the same rendered frame.

```cpp
auto device = Ursa::Rhi::createDevice({}).value;
auto renderer = Ursa::Renderer3D::create(device).value;
Ursa::Scene scene;
auto cameraObject = Ursa::GameObject::Create("Camera");
Ursa::LocalTransform cameraPose;
cameraPose.position = {0.0f, 0.0f, 5.0f};
cameraObject->SetLocalTransform(cameraPose);
auto camera = cameraObject->AddComponent<Ursa::CameraComponent>();
scene.AttachGameObject(cameraObject);
scene.SetActiveCamera(camera.value->Id());
auto boxObject = Ursa::GameObject::Create("Box");
auto box = boxObject->AddComponent<Ursa::BoxRenderComponent>();
scene.AttachGameObject(boxObject);
auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::Unlit);
material->setBaseColor({1.0f, 0.0f, 0.0f, 1.0f});
box.value->material = material;
auto frame = scene.CollectRenderData(1.0f).value;
auto color = device->createTexture({64, 64, Ursa::Rhi::Format::RGBA8Unorm,
    Ursa::Rhi::TextureUsage::ColorAttachment | Ursa::Rhi::TextureUsage::TransferSource}).value;
auto submitted = renderer->renderOffscreenModels(color, frame.viewProjection, frame.boxes,
                                                  frame.lighting, frame.cameraPosition);
submitted.value->wait();
```

`UrsaMaterialSceneSmoke` compiles this setup and reads back its center pixel. For direct swapchain rendering, pass the same frame data to `renderModels`. The renderer uploads immutable procedural textures only when first used, keeps pipelines and unchanged material bindings cached, and waits for its previous frame fence before replacing uploads, bindings, or textures. Destroying the renderer also waits for its last submission. Shaders are read through `ursa:`, so a packaged build only needs the merged asset mirror beside the executable.

PBR uses GGX, Smith, and Schlick direct lighting from scene directional and point lights, plus an adjustable ambient approximation. It tone maps and encodes the result for UNORM output. This phase has no IBL or shadowing. Its cube mesh has per-face UVs and tangents; inverse-transpose normals handle rotated boxes under nonuniform parent scale. The material GPU test renders and reads back these cases.

### Alpha modes

`Opaque` ignores base-color alpha and writes depth. `Mask` discards pixels whose combined base-color factor and texture alpha are **below** the cutoff; a pixel equal to the cutoff remains and writes depth. The default cutoff is 0.5. `Blend` uses standard source-alpha blending, keeps depth testing, and does not write depth. Opaque and Mask boxes are drawn first. Blend boxes are sorted by camera-space center distance from far to near; equal distances retain scene traversal order. Their individual surfaces are drawn front-facing only.

Center-based sorting does not precisely resolve intersecting transparent cubes. Separate front/back boxes and masked holes are covered by `UrsaMaterialRendererTests`. To check repeatability, run `ctest --test-dir out/build -C Debug -R '^UrsaMaterialRendererTests$' --output-on-failure` twice from the Engine repository; the test compares blend results after different input orders and repeated equal-depth scenes.

If CMake cannot detect MSVC or MSBuild reports `MSB6001` because a PowerShell process has duplicate `PATH`/`Path` keys, normalize that process environment before configuring or building:

```powershell
$savedPath = $env:PATH
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:PATH = $savedPath
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Debug
```

## Unified serialization

See [persistent serialization](Docs/Serialization.md) for Serializable authoring, archive formats, component factories, schema versions and deferred references.

See [scene lifecycle and frame scheduling](Docs/SceneLifecycle.md) for GameObject activity, component lifecycle events, fixed-step time and Engine/Editor frame order.
