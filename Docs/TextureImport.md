# Texture import

The Editor imports PNG and JPEG/JPG source images through VFS. It never rewrites
the image file. A sidecar `.meta` stores the `ursa.Texture` importer settings and
the source GUID; import output is a versioned `.ursatex` runtime artifact in the
rebuildable `lib:Artifacts/` cache. `ResourceSession` resolves that GUID to the
latest valid artifact through the AssetDatabase resolver.

Select a source image under `Assets/` to open **Texture Import** in the Inspector.
The source preview remains at the original dimensions; the imported preview shows
the effective size and mip chain. Change a setting and choose **Reimport** to
produce a new artifact. A decode or import failure leaves the previous preview
and successful artifact available.

The first importer supports these settings:

- Type: Default, Normal Map, or Linear Data. Normal and linear data are imported
  in linear color space.
- sRGB color interpretation for default color textures.
- Preserve or discard alpha.
- Generate a full mip chain.
- Point or linear filtering, repeat or clamp wrapping.
- Maximum dimension, preserving the source aspect ratio when scaling down.

The first runtime artifact is RGBA8. Platform-specific compression, sprite
slicing, cubemaps, and virtual texturing are not part of this importer.

Code that opens an Editor project can import a source using the project's
AssetDatabase and VFS:

```cpp
auto settings = Ursa::Editor::TextureImporter::Settings(database, sourcePath);
if (!settings)
    return Ursa::Result<Ursa::Editor::TextureImportResult>::Failure(settings.error);
return Ursa::Editor::TextureImporter::Import(database, sourcePath, settings.value);
```

`UrsaTextureImporterTests` generates PNG and JPEG VFS fixtures to verify decode,
settings persistence, mip serialization, cache hits, dependency invalidation,
failure safety, and runtime `ResourceSession` loading. `UrsaDemoTextureImportTests`
imports the sample `Demo/Assets/Textures/UrsaEngine.png` without modifying its
source bytes.
