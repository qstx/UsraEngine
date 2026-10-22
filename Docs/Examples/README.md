# Scripted asset extension example

Create a project containing these files:

```text
Assets/
  GeneratedFormatAsset.cs
  Sample.ursadataset
  GeneratedTextures/Example.mask.png
  Models/Processed/Example.fbx
  Editor/
    DatasetImporter.cs
    DatasetImporterEditor.cs
    GeneratedFormatAssetEditor.cs
    TextureImportRules.cs
    ModelImportRules.cs
```

Copy the C# files from this directory, preserving the Editor folder. `Example.mask.png` and `Example.fbx` are your own source assets. `Sample.ursadataset` contains:

```json
{"label":"Example", "size":1.0, "main":"data"}
```

The importer generates a ScriptableObject, Texture2D, Mesh, Material and Prefab. Stable output keys preserve their references through a clean Library rebuild. The texture and model rules run before publication for matching project paths; change their path predicates for your project.

Build the engine and run the same project through both entry points (from the repository root):

```powershell
cmake --build Engine/Build --config Debug --target UrsaEditor UrsaAssetImport UrsaScriptPlayer
Engine/Build/Editor/Debug/UrsaAssetImport.exe --project C:/Projects/ExtensionExample --configuration Debug
Engine/Build/Editor/Debug/UrsaEditor.exe --project C:/Projects/ExtensionExample --asset Sample.ursadataset --view Inspector
```

Select `Sample.ursadataset`: its importer Inspector edits a draft. Apply writes settings and requests import; Revert restores saved settings. Expand the source row in Project to select read-only published objects and inspect their fields. The Preview object selector also exposes the generated main object and sub-assets. Select the generated ScriptableObject to use `GeneratedFormatAssetEditor`; switch between RGBA texture and mesh/material views and drag the mesh preview to rotate its private camera. `OnInspectorGUI` and Preview callbacks share the same asset Editor instance and viewing fields. Imported output properties are read-only; edit the source/importer to regenerate them.

Use the generated assets from Game scripts through normal `AssetReference` loading, and include that script/scene in your Player project. Editor files stay outside the runtime assembly and content closure. The repository's automated example workflow creates the runtime reader and scene, imports from an empty Library, compares Editor/CLI outputs, and loads the generated resources in Loose/Pak Player:

```powershell
ctest --test-dir Engine/Build -C Debug -R '^Ursa(ManagedAssetImportWorkflowTests|ScriptedImporterInspectorWorkflowTests|GeneratedAssetPlayerWorkflowTests|ManagedAssetPreviewTests)$' --output-on-failure
```

`TypedInspector.cs` remains a compilable component Inspector/window example. See [ScriptedAssetImporters](../ScriptedAssetImporters.md) and [ManagedEditorExtensions](../ManagedEditorExtensions.md) for ownership, failure and version rules.
