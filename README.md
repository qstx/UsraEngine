# UrsaEngine

UrsaEngine is a C++17 graphics engine foundation with an API independent RHI and a Vulkan 1.2 backend. GPU tests exercise compute, texture sampling, storage images, depth attachments, offscreen rendering, transfers, and readback. The playable Snake application lives in the independent sibling `Demo` repository.

## Requirements

- Windows 10 or newer, Visual Studio 2022 with C++ desktop tools, CMake 3.20 or newer.
- Vulkan SDK 1.2 or newer with `glslc`, and a Vulkan 1.2 capable GPU and driver.
- Git submodules containing pinned GLFW 3.5.1, VMA 3.4.0, nlohmann/json 3.12.0, EASTL 3.21.23 (`7fadbf0`), its matching EABase (`123363e`), spdlog 1.13.0 (`7c02e20`), Dear ImGui `v1.92.9b-docking` (`b48d1af`), and ImGuizmo `1.10` (`b796ac3`) sources.

Initialize the top-level dependencies after checkout with `git submodule update --init`. To verify the Editor dependencies recursively, run `git submodule update --init --recursive third_party/imgui third_party/imguizmo`. EABase also declares upstream test-only submodules over SSH; they are not needed for this Engine build. The Dear ImGui and ImGuizmo Git links are fixed to the revisions above; the tags document the upstream releases but are not followed automatically.

`UrsaRuntime` owns the window, RHI interface, and Vulkan backend. `UrsaEngine` provides the application loop and links to `UrsaRuntime`. The optional `UrsaEditorCore` target (alias `Ursa::Editor`) owns the Editor UI and links to `Ursa::Engine`. CMake aliases `Ursa::Runtime`, `Ursa::Engine`, and `Ursa::Editor` are available to other targets when the Editor is enabled.

## Source layout

`Runtime/Core`, `Runtime/Vfs`, `Runtime/Window`, and `Runtime/Rhi` contain the runtime modules. Vulkan implementation files live under `Runtime/Rhi/Vulkan`. `Engine/Application`, `Engine/Math`, `Engine/Material`, `Engine/Scene`, `Engine/Renderer3D`, and `Engine/Serialization` contain the engine modules. `Editor/Application` and `Editor/Ui` contain the Editor loop and UI bridge. Headers and corresponding source files sit directly in each module directory. Include public headers by module path, such as `<Rhi/RHI.hpp>` and `<Renderer3D/Renderer3D.hpp>`. Engine-owned resources, including the renderer's shaders, live under `Assets/`. Tests are grouped by module under `Tests/`.

CMake discovers `.cpp` and `.hpp` files within the listed module directories and checks for additions or removals during a build. New modules and test executables still need a CMake target or directory entry.

## Logging

Include `<Core/Log.hpp>` to record messages from Runtime, Engine, Editor, or a game without using spdlog types in public code. `LogSession` initializes before a window or GPU device and flushes at scope exit:

```cpp
#include <Core/Log.hpp>

int main() {
    Ursa::LogSession log;
    if (!log.status())
        return 1;
    URSA_LOG_INFO("Example", "Logging ready");
    return 0;
}
```

The levels, from least to most important, are Trace, Debug, Info, Warn, Error, and Critical; `Off` disables output. Debug builds default to Debug and Release builds to Info. Call `Log::setLevel` to change the minimum level while running. The `URSA_LOG_*` macros check the level before evaluating the message expression. A Critical message labels a failure but does not terminate the process; `Result` remains the way an operation reports failure to its caller.

Each accepted message includes a timestamp, level, module name, and UTF-8 body. The synchronous thread-safe sinks write to the colored console and to `logs/<program>.log` beside the executable, with a default 5 MiB file and three rotated backups. `LogConfig` can override the level, path, size, and backup count. Warn and higher flush immediately; shutdown flushes the rest. A file initialization failure returns a diagnostic and writes to `stderr`, so a launcher may continue without file logging. Build and run `UrsaLogHeaderSmoke` to check the example; `UrsaLogTests` covers levels, filtering, rotation, fallback, and concurrent writes.

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
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For a new clone, run the submodule update commands above before configuring CMake. A full recursive checkout may attempt EABase's unrelated test-only SSH submodules. CMake reads dependency sources from `third_party/`, including EABase through a local FetchContent source override; configuration does not download dependencies.

## Editor scene and Prefab authoring

To edit a scene, enter `level.ursascene` in **New asset**, click **New Scene**, then select a GameObject in **Hierarchy**. **Add child** creates a nested GameObject. The hierarchy checkbox controls whether a GameObject or component is enabled; **Inspector** edits its name, local position and scale, parent, sibling order, and built-in Box, Camera, Directional Light, and Point Light components. Use **Add Box**, then select a saved material by name; the file stores its stable material ID. The **Viewport** always has an editor camera, even when the scene has no game camera. Select a nested GameObject and use **Translate**, **Rotate**, or **Scale** with the gizmo; the world-space movement is converted back to that GameObject's local transform. Zero scale, singular parent transforms, shear, and non-finite gizmo results are rejected with a diagnostic. Save, close, and reopen the scene to check its name, transform, order, component fields, and material reference. Prefab assets are edited as independent GameObject graphs; the Editor creates a temporary preview Scene only to render the graph.

For a linked instance, first save its Prefab source. Open a scene, select a regular parent node, and choose that Prefab from **Place Prefab**; each placement receives its own instance ID. Editing an instance node or its components changes only that placement. **Pin label override** records an explicit property override; hierarchy, component, and transform changes are captured as property or structure overrides on save. Open the `.ursaprefab` itself to edit the shared source. The next explicit scene reload merges source changes with saved instance overrides. A missing override target or invalid merge is reported, and the active scene and saved patch remain available. `UrsaSceneDraftTests` checks two placements, source isolation, and round-trip structure; `UrsaPrefabTests` checks source merges and conflict preservation.

Project components need both a `ComponentCodec` in `EditorProject::components` and, for editable fields, a callback in `EditorProject::componentEditors`. Register them in the launcher setup passed to `EditorApplication`. The callback receives the live component, scene, and node and returns whether a persistent field changed; the Editor then marks the document dirty. A codec without a property editor remains visible and its serialized fields are preserved on save. If decoding needs project services, configure `editContextSetup` to provide them and keep those services alive. Demo's `Editor/Main.cpp` and `Editor/SnakeEditors.cpp` show the Snake registration.

Click **Run** on a scene to clone the current effective edit scene in memory, including unsaved edits. The copy receives fresh project runtime services from `playContextSetup`; a missing service reports an error and leaves Edit mode intact. Engine `FrameScheduler` drives Update, fixed updates, and Late Update on the copy. The game uses the scene's active camera; focus the viewport image to send keyboard input to the game. Input sent to other Editor panels stays with the UI. **Save** and document switching are disabled during Play. Click **Stop** to destroy the play copy; the edit scene, dirty flag, and asset files remain as they were before Run. `UrsaPlaySessionTests` covers clone isolation, lifecycle order, service failure, save blocking, and cleanup.

## Editor UI bridge

The ImGui platform bridge forwards key and modifier state and uses the window's UTF-8 system clipboard callbacks. Editor text fields support Ctrl+C, Ctrl+X, and Ctrl+V with the Windows clipboard.

The Editor is enabled by default (`-DURSA_BUILD_EDITOR=ON`). From the Engine repository, build it with `cmake --build build --config Debug --target UrsaEditor`, then launch it with `& .\build\Editor\Debug\UrsaEditor.exe --project ..\Demo\Assets`. The project asset directory is required at startup; `--hidden --frames 3` runs a short window smoke test. The default docked workspace places Hierarchy on the left, the scene viewport in the center, Inspector on the right, and Project along the bottom. Run/Stop is in the top bar. Panels can be rearranged during the session. `ctest --test-dir build -C Debug -R '^(UrsaImGuiBridgeTests|UrsaEditorWorkspaceWindowTests)$' --output-on-failure` checks UI rendering and the startup workspace. The same commands accept `Release`.

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

`<Vfs/VirtualFileSystem.hpp>` addresses resources with `scheme:relative/path` strings instead of host paths. A scheme names an ordered list of host roots: lookup returns the first root that contains the relative path, so an engine root and a generated-assets root can share one layout. Each root is mounted read-only or read-write, and a write only ever lands on the first root of a read-write scheme. `VirtualPath::parse` rejects host drive letters, absolute paths, and any `..` segment, and every resolved file is checked to stay inside its mount root, including through reparse points such as Windows junctions.

```text
ursa:Shaders/Renderer3D/Cube.vert      -> <engine>/Assets/Shaders/Renderer3D/Cube.vert
ursa:Shaders/Renderer3D/Cube.vert.spv  -> <project>/cache/Assets/Shaders/Renderer3D/Cube.vert.spv
prj:Snake/Snake.ursascene              -> <project>/Assets/Snake/Snake.ursascene
```

```cpp
#include <Vfs/AssetRoots.hpp>
#include <Vfs/VirtualFileSystem.hpp>

Ursa::Result<void> openResources(const std::filesystem::path &engineRoot,
                                 const std::filesystem::path &engineMirror,
                                 const std::filesystem::path &projectAssets) {
    Ursa::VirtualFileSystem vfs;
    if (!vfs.mount("ursa", engineRoot, Ursa::MountAccess::ReadOnly) ||
        !vfs.mount("ursa", engineMirror, Ursa::MountAccess::ReadOnly) ||
        !vfs.mount("prj", projectAssets, Ursa::MountAccess::ReadWrite))
        return Ursa::Result<void>::failure("Cannot mount resource roots");
    auto shader = Ursa::VirtualPath::parse("ursa:Shaders/Renderer3D/Cube.vert.spv");
    if (!shader)
        return Ursa::Result<void>::failure(shader.error);
    auto bytes = vfs.read(shader.value);
    if (!bytes)
        return Ursa::Result<void>::failure(bytes.error);
    return {};
}
```

`resolveEngineAssetRoots` and `openEngineFileSystem` select the engine roots in the order explicit override, `Assets/` beside the executable (a packaged layout), then the engine source assets followed by the generated mirror, which overlay into one `ursa:` view. The mirror defaults to `<project>/cache/Assets` and is produced at build time; set `URSA_ASSET_CACHE_DIR` at configure time to move it. `list(scheme)` walks every root, keeps the first occurrence of each relative path, and reports host locations that later roots shadow. The scheme names are a convention, not a built-in: an application registers `ursa` for the engine and `prj` for the writable project. `UrsaVirtualFileSystemTests` covers parsing, mount permissions, overlays, executable-relative resolution, and mount-root isolation.

## Resource files and IDs

Resources can be stored under multiple registered roots, such as `Engine/Assets` and a sibling project's `Demo/Assets`. The resource index recognizes `.ursatex` (RGBA8 texture), `.ursamat` (material), `.ursascene` (scene), and `.ursaprefab` (node subtree). Each file carries a format version and a stable 128-bit resource ID. References use this ID rather than a path: after moving or renaming a file within a registered root, call `rebuild()` before resolving it again. A copied file must receive a new ID; indexing two files with the same ID reports both paths as an error.

```cpp
#include <Serialization/ResourceIndex.hpp>

void inspectResources() {
Ursa::ResourceIndex resources;
auto added = resources.addRoot("Assets");
if (!added)
    return;
auto scanned = resources.rebuild();
if (!scanned)
    return;
auto id = Ursa::ResourceId::parse("de305d54-75b4-431b-adb2-eb6b9e546014");
if (!id)
    return;
auto location = resources.resolve(id.value, "example scene");
if (!location)
    return;
}
```

Header parsing rejects unsupported versions, malformed IDs, and files above configured size limits. The index skips filesystem links and reports missing IDs with the referring asset name supplied by the caller. `UrsaSerializationBaseTests` covers ID parsing, header validation, move/rename, duplicate IDs, and failure-safe file replacement.

Procedural RGBA8 pixels can be saved directly as a `.ursatex` file. A `.ursamat` file stores validated material parameters and the IDs of its bound textures. The example below needs no window or GPU:

```cpp
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/TextureResource.hpp>

void saveAndLoadMaterial(const std::filesystem::path &assetRoot) {
    auto pixels = Ursa::MaterialTexture::create(1, 1, Ursa::TextureColorSpace::Srgb,
                                                {220, 80, 40, 255});
    if (!pixels)
        return;
    const auto textureId = Ursa::ResourceId::generate();
    if (!Ursa::saveTextureResource(assetRoot / "swatch.ursatex", textureId, *pixels.value))
        return;

    Ursa::MaterialInstance material(Ursa::MaterialType::Unlit);
    if (!material.setTexture(Ursa::MaterialTextureSlot::BaseColor, pixels.value))
        return;
    const auto materialId = Ursa::ResourceId::generate();
    if (!Ursa::saveMaterialResource(assetRoot / "swatch.ursamat", materialId, material,
                                    [textureId](Ursa::MaterialTexturePtr) {
                                        return Ursa::Result<Ursa::ResourceId>::success(textureId);
                                    }))
        return;

    auto session = Ursa::ResourceSession::open({assetRoot});
    if (!session)
        return;
    auto loaded = session.value->loadMaterial(materialId);
    if (!loaded)
        return;
}
```

Call `refreshed()` to create a new indexed loading session after files change. Existing material and texture objects stay alive for scenes that still reference them. `UrsaAssetPersistenceTests` runs the same save/load path and checks both exact pixel recovery and cache identity.

## Saving and loading scenes

Register the component codecs used by the Scene before saving or loading. The new `.ursascene` schema uses `sceneGraphVersion: 2` and a `gameObjects` array containing object IDs, names, parent IDs, local transforms, enabled flags, and component data. Ambient light and active camera remain Scene-level fields. Files without the supported schema version fail with an explicit diagnostic; old Scene files are not migrated. Resource references in component fields are IDs resolved through `ResourceSession`.

```cpp
#include <Serialization/SceneResource.hpp>

void roundTripScene(const std::filesystem::path &path,
                    Ursa::ResourceSession &resources) {
    Ursa::ComponentRegistry registry;
    if (!Ursa::RegisterBuiltinComponents(registry))
        return;
    Ursa::Scene source;
    auto camera = Ursa::GameObject::Create("Camera");
    if (!camera->AddComponent<Ursa::CameraComponent>() || !source.AttachGameObject(camera))
        return;
    if (!Ursa::SaveSceneResource(path, Ursa::ResourceId::Generate(), source, registry))
        return;
    Ursa::ComponentLoadContext context(resources);
    auto loaded = Ursa::LoadSceneResource(path, registry, context);
    if (!loaded)
        return;
    std::unique_ptr<Ursa::Scene> active = std::move(loaded.value.scene);
    auto reloaded = Ursa::ReloadSceneResource(path, registry, context, active);
}
```

`GameObject::Id()` and `Component::PersistentId()` return IDs stored in the file. A `GameObject::Ptr` refers to a GameObject instance; serialization uses persistent IDs rather than pointer values to restore references after load. `ReloadSceneResource` builds and binds a candidate before replacing `active`. On failure the old Scene remains alive. Loaded components begin their lifecycle during the next normal frame, not while parsing. Use a newly `Refreshed()` resource session to pick up changed texture and material files. `UrsaScenePersistenceTests` exercises save, load, and reload.

For project components, register a stable type name and data version before either operation. Save only persistent fields; use `ComponentLoadContext::Service<T>()` for references to project-owned runtime objects. `create` can read fields and attach the component, while `bind` runs after all GameObjects and components exist and can resolve saved IDs through `context.GameObjectById(id)` and `context.Component(id)`. A minimal registration looks like this:

```cpp
#include <Serialization/ComponentRegistry.hpp>

struct CounterComponent : Ursa::Component {
    CounterComponent(int &total, int step) : total(total), step(step) {}
    int &total;
    int step;
};

Ursa::Result<void> RegisterCounter(Ursa::ComponentRegistry &registry) {
    Ursa::ComponentCodec codec;
    codec.type = "game.Counter";
    codec.version = 1;
    codec.matches = [](const Ursa::Component &value) {
        return dynamic_cast<const CounterComponent *>(&value) != nullptr;
    };
    codec.save = [](const Ursa::Component &value, const Ursa::ComponentSaveContext &) {
        return Ursa::Result<nlohmann::json>::Success(
            {{"step", static_cast<const CounterComponent &>(value).step}});
    };
    codec.create = [](Ursa::Scene &scene, const Ursa::GameObject::Ptr &object, const nlohmann::json &data,
                      Ursa::ComponentLoadContext &context) {
        if (!data.is_object() || !data.contains("step") || !data["step"].is_number_integer())
            return Ursa::Result<Ursa::ComponentId>::Failure("Invalid Counter step");
        auto total = context.Service<int>();
        if (!total)
            return Ursa::Result<Ursa::ComponentId>::Failure(total.error);
        return scene.AddComponent<CounterComponent>(object, *total.value, data["step"].get<int>());
    };
    return registry.RegisterCodec(std::move(codec));
}
```

Call `context.Provide(total)` before loading and keep `total` alive as long as the loaded Scene. The type name and version belong to the file format; changing either requires a deliberate format change. An unknown type or version fails with a file and GameObject location. `UrsaCustomComponentTests` compiles this registration shape and checks cross-object binding and error paths.

## Prefab files and linked scenes

`SavePrefabAssetResource` writes a `PrefabAsset` containing one independent GameObject tree to a `.ursaprefab` file using `prefabGraphVersion: 2` and the GameObject graph schema. `LoadPrefabAssetResource` creates detached GameObjects and components without creating or attaching them to a runtime Scene; their GameObject and component IDs are preserved when saved again. `PrefabAsset::Instantiate` clones that graph into a Scene with fresh runtime identities. The Editor keeps a preview Scene for its viewport and stores a detached PrefabAsset graph for persistence. A linked `.ursascene` keeps each placement's Prefab resource ID, independent instance ID, optional parent GameObject ID, and overrides. Files with missing or unsupported `prefabGraphVersion` fail with an explicit diagnostic; legacy Prefab data is not migrated. The following calls form the path exercised by `UrsaPrefabTests`:

Use `LoadLinkedSceneResource` for a Scene containing Prefab instances. The plain `LoadSceneResource` entry reports an error on that file so it cannot silently omit the instances.

```cpp
#include <Serialization/PrefabResource.hpp>
#include <Serialization/JsonResource.hpp>

void placePrefab(const std::filesystem::path &root, Ursa::Scene &source,
                 const Ursa::GameObject::Ptr &subtree) {
    Ursa::ComponentRegistry registry;
    if (!Ursa::RegisterBuiltinComponents(registry))
        return;
    const auto prefabId = Ursa::ResourceId::Generate();
    if (!Ursa::SavePrefabResource(root / "tile.ursaprefab", prefabId, source, subtree, registry))
        return;
    Ursa::Scene empty;
    const auto scenePath = root / "level.ursascene";
    auto document = Ursa::EncodeSceneDocument(scenePath, Ursa::ResourceId::Generate(), empty, registry);
    if (!document)
        return;
    const auto instanceId = Ursa::ResourceId::Generate();
    if (!Ursa::AddPrefabInstance(document.value, prefabId, instanceId) ||
        !Ursa::WriteJsonResource(scenePath, document.value))
        return;
    auto resources = Ursa::ResourceSession::Open({root});
    if (!resources)
        return;
    Ursa::ComponentLoadContext context(*resources.value);
    auto linked = Ursa::LoadLinkedSceneResource(scenePath, registry, context);
    if (!linked)
        return;
    auto sourceId = source.PersistentId(subtree);
    if (!sourceId || !Ursa::SetPrefabPropertyOverride(linked.value, instanceId, sourceId.value,
                                                       "/localTransform/position/0", 2.0f) ||
        !Ursa::SaveLinkedSceneResource(scenePath, linked.value, registry))
        return;
    auto refreshed = resources.value->Refreshed();
    if (!refreshed)
        return;
    Ursa::ComponentLoadContext reloadContext(*refreshed.value);
    if (!Ursa::ReloadLinkedSceneResource(scenePath, registry, reloadContext, linked.value))
        return;
    if (!Ursa::ClearPrefabPropertyOverride(linked.value, instanceId, sourceId.value,
                                            "/localTransform/position/0") ||
        !Ursa::SaveLinkedSceneResource(scenePath, linked.value, registry))
        return;
    Ursa::ReloadLinkedSceneResource(scenePath, registry, reloadContext, linked.value);
}
```

Property paths use JSON Pointer syntax. GameObject paths include `/localTransform/position/0`, `/enabled`, and `/name`; component paths include `/data/material` and registered persistent fields under `/data/`. Calling `SetPrefabPropertyOverride` records explicit intent even if the value currently equals the source. Calling `ClearPrefabPropertyOverride` removes that intent; reload then inherits the current source value. `SetPrefabStructureOverride` accepts `deletedGameObjects`, `addedGameObjects`, `gameObjectParents`, `gameObjectOrders`, `deletedComponents`, `addedComponents`, `componentOwners`, and `componentOrders`. IDs in these operations are local to the Prefab instance; an order list fixes the named objects first and leaves newly added source objects in source order afterward. Save compares the current Scene to its loaded snapshot, so direct edits to persistent fields and structure are captured as overrides. Keep the resource session alive for `LinkedSceneResource`, and pass a material ID resolver to `SaveLinkedSceneResource` when assigning a material object that was not loaded by that session. An invalid merge reports the Prefab, instance, object and operation and leaves the active Scene and saved file intact.

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

Attach `CameraComponent` and `BoxRenderComponent` to GameObjects. The camera looks along its GameObject's local negative Z axis; its world transform supplies the view, and its fields specify perspective projection. The Scene gathers active boxes with complete world matrices, including ancestor rotation and scale. Disabled GameObjects or components are omitted. Without an enabled active camera, `CollectRenderData(aspect)` and an `Application` scene frame return a diagnostic error.

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
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```
