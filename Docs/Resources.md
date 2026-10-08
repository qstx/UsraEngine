# Resource I/O and deployment

Asset consumers use `VirtualFileSystem` and `VirtualPath`. Host paths are startup
inputs for directory/package providers. `ResourceIndex::Locate`, resource-file
host-path overloads, and Editor host save paths have been removed from production
APIs. Test fixture adapters live under `Tests/Fixtures` and are not engine APIs.

## Addresses

| Scheme | Editor | Loose runtime | Packaged runtime |
| --- | --- | --- | --- |
| `ursa:` | Engine source/generated roots, read only | Engine roots, read only | Content.pak, read only |
| `prj:` | Project Assets, writable | Project Assets, read only | Content.pak, read only |
| `lib:` | Project Library cache, writable (Editor only) | Not mounted | Not mounted |
| `user:` | Platform application data, writable | Same | Same |

The Editor uses `lib:` for its generated asset database and import cache. It is
separate from the source `Assets` tree, is rebuildable, and is never included in
runtime mount profiles or packages.

In Editor UI, `prj:` corresponds to the project `Assets/` directory. Asset
identity is stored in sidecar `.meta` files beside files and directories; move
and copy operations must go through the AssetDatabase so GUIDs follow moves and
copies receive new identities. Read-only project roots need valid sidecars in
advance.

Editor projects resolve source GUIDs and imported local file IDs through the live
AssetDatabase and its `lib:` artifact provider. Scene and Prefab edits in Play
mode use the same resolver. Standalone loose Players load direct runtime
`.ursa*` resources from `ursa:` and `prj:`; source FBX/OBJ/images are editor
inputs and should be cooked before a Player uses them. Pak-backed Players resolve
ordinary resources and imported sub-assets through the packaged asset catalog,
without mounting `lib:` or reading `.meta` sidecars.

Windows uses `%LOCALAPPDATA%/<applicationId>`. The mount profile's
`userDataDirectoryOverride` supports portable/test layouts. Use `UserDataStore`
for `user:Settings/...` and `user:Saves/...`; names cannot escape those folders.
Logs use `user:Logs/...` and VFS append/rename/removal for rotation. An unavailable
log mount reports the failure on stderr. Editor layouts use
`user:Settings/EditorLayout.ini` through ImGui's memory settings API.

## Persistent resources

This sequence is compiled and run by `UrsaResourceVfsTests`. Check every Result in
application code (the test's `Take`/`Done` helpers throw on failure).

```cpp
auto texture = Take(Ursa::MaterialTexture::Create(1, 1,
    Ursa::TextureColorSpace::Srgb, {200, 50, 20, 255}));
const auto textureId = Ursa::ResourceId::Generate();
Done(Ursa::SaveTextureResource(filesystem,
    Take(Ursa::VirtualPath::Parse("prj:Texture.ursatex")), textureId, *texture));
auto resources = Take(Ursa::ResourceSession::Open(filesystem));
auto restored = Take(resources->LoadTexture(textureId));
```

`SaveMaterialResource`, `SaveSceneResource`, `SavePrefabResource`,
`SavePrefabAssetResource` and `SaveLinkedSceneResource` similarly take
`VirtualFileSystem&` followed by `VirtualPath`. Load functions accept a const VFS.
Reload functions build a complete candidate before replacing an active graph.
`ResourceSession` indexes mounted virtual entries, resolves stable IDs, and caches
texture/material dependencies. `Refreshed()` picks up directory changes explicitly.
Resource kinds, IDs and versions remain in their envelopes. JSON model fields use
the shared archive; the V1 RGBA texture wire adapter retains its existing binary
header/pixel layout while delegating model fields to Serializable.

Directory and package providers use the same APIs; a Pak entry never needs to be
extracted. Unsupported versions, missing dependencies, malformed fields and
read-only writes return diagnostic errors naming the virtual asset.

## Editor saves

`DocumentWriter` receives `(VirtualFileSystem&, const VirtualPath&, ResourceId)`.
It writes to a private memory provider. EditorDocuments validates the staged
envelope, dependency/index consistency, and texture/material data before atomically
committing bytes to the project provider. On failure the original bytes and dirty
document remain. A package-backed project can be viewed and run, but saving is
rejected. Asset drafts receive virtual paths and never resolve assets to host files.

## Packages and startup

Build `UrsaEnginePackage` for engine content, or `UrsaDemoPackage` for a single
`Content.pak` containing both `ursa:` and `prj:`. The generated SPIR-V root joins
the Engine source Assets root. Demo packaging also reads the current `Library/`
import cache, copies its runtime artifacts under `prj:__UrsaCooked/`, and writes
`AssetCatalog.ursacatalog` with deterministic GUID/local-file-ID mappings. Pak
output omits `.meta` files, `Library/`, and raw FBX/OBJ/image/Shader sources.
Stale or missing imported artifacts stop packaging with a diagnostic. PakBuilder rejects conflicting virtual entries.
Place Content.pak beside the executable for engine shader discovery, or supply
the package explicitly at startup.

From the workspace root after configuring Engine with `-DURSA_PROJECT_DIR=Demo`:

```powershell
cmake --build Engine/build --config Debug --target UrsaPak UrsaScriptPlayer UrsaEditor
Engine/build/Debug/UrsaScriptCompile.exe Demo Debug
Engine/build/Debug/UrsaPak.exe --engine Engine/Assets --engine-generated Engine/cache/assets `
  --project Demo/Assets --library Demo/Library --configuration Debug `
  --output Engine/build/package/Content.pak
Engine/build/Debug/UrsaScriptPlayer.exe --project Demo --scene Snake/Snake.ursascene
Engine/build/Debug/UrsaScriptPlayer.exe --pak Engine/build/package/Content.pak --scene Snake/Snake.ursascene
Engine/build/editor/Debug/UrsaEditor.exe --project Demo --scene Snake/Snake.ursascene
```

When invoking `UrsaPak` directly for a project, pass `--library <Project/Library>`
to include imported textures, models, materials, skeletons, and clips. Runtime
resolves those references from the packaged catalog and does not mount `lib:`.

`--scene` is relative to `prj:`. Runtime mounts resources once; renderer, UI and
scene loading share the configured VFS. Explicit `--pak` failure never falls back
to loose files. Project scripts are compiled with `UrsaScriptCompile`; the pak
tool includes the current Game assembly and rejects stale script content.

Pak V1 validates the header and directory table at mount. Its 64 KiB payload blocks
are verified on their first actual read and cached for the immutable mount's
lifetime. CRC32 detects accidental corruption; it is not cryptographic authentication.
Do not modify a mounted package in place; replace it after releasing its mounts.

## Host filesystem boundary

Only directory providers perform production streams, file writes, rename and
deletion. Pak bootstrap opens its backing file through a directory provider.
Executable/project/user-root discovery is startup configuration. Compiler/shader
outputs and packaging invocation are build infrastructure. Test fixtures may set
up host files; engine behavior is verified through mounted VFS APIs.
