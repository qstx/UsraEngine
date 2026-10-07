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
| `user:` | Platform application data, writable | Same | Same |

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
the Engine source Assets root. PakBuilder rejects conflicting virtual entries.
Place Content.pak beside the executable for engine shader discovery, or supply
the package explicitly at startup.

From the workspace root after configuring `Demo/build`:

```powershell
cmake --build Demo/build --config Debug --target UrsaDemoPackage
Demo/build/Debug/UrsaSnake.exe --project Demo
Demo/build/Debug/UrsaSnake.exe --pak Demo/build/package/Content.pak
Demo/build/Debug/UrsaDemoEditor.exe --project Demo --scene Snake/Snake.ursascene
Demo/build/Debug/UrsaDemoEditor.exe --project Demo --pak Demo/build/package/Content.pak --scene Snake/Snake.ursascene
```

`--scene` is relative to `prj:`. Runtime mounts resources once; renderer, UI and
scene loading share the configured VFS. Explicit `--pak` failure never falls back
to loose files. Snake smoke serialization uses a private memory provider. The
asset baker mounts the output project Assets directory and writes virtual files.

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
