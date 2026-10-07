# Shader-backed Materials

A material references a Shader by stable `ResourceId`. It stores only typed property overrides; values omitted from the material resolve to the Shader's declared defaults. Keep the loaded `ShaderAsset` available while editing so writes can be checked against its schema.

```cpp
auto shader = resources.LoadShader(shaderId);
if (!shader)
{
	return Result<void>::Failure(shader.error);
}

auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
auto assigned = material->SetShader(shaderId);
if (!assigned)
{
	return assigned;
}

auto tint = material->SetPropertyValue(
	"_BaseColor", *shader.value, MaterialPropertyValue::ColorValue({0.8f, 0.3f, 0.2f, 1.0f}));
if (!tint)
{
	return tint;
}

auto variant = material->SetKeywordEnabled("ALPHA_TEST", true);
if (!variant)
{
	return variant;
}

return SaveMaterialResource(filesystem, materialPath, materialId, *material, resolveTextureId);
```

`SetPropertyValue` rejects undeclared fields, wrong value types, invalid ranges, non-finite values, and incompatible normal texture color spaces. Texture values carry a stable resource ID; the material resource writer resolves legacy texture objects through `TextureIdResolver`. Cloning a material copies its overrides so the clone can be edited independently; objects sharing the same material observe the same updates.

The current Unlit/PBR convenience API remains available. Loading a version 1 `.ursamat` maps its type and fields to the built-in Unlit or PBR Shader ID in memory. Loading does not rewrite the file. An explicit successful save writes version 2 with both the Shader reference and typed property overrides, while preserving the resource ID and texture IDs. Unknown property override names remain serialized so Shader edits do not silently discard them.

## Editor workflow

The Project window lists `.ursashader` source files separately from imported resources. Selecting one opens the read-only source view and its import status. **Reimport Shader** resolves VFS includes, compiles all required Vulkan variants, refreshes compiled metadata, and reports compiler diagnostics with the source path and line. Project Shader assets write their `.ursashaderasset` beside the source. Engine or package-backed sources are compiled in memory; the editor never writes to a read-only mount.

When a Material is selected, the Inspector builds controls from its loaded Shader property metadata. It displays Float, Range, Int, Color, Vector, and Texture2D properties, honors `[HideInInspector]` and `[HDR]`, and lets texture properties select indexed Texture assets or restore the Shader default. Values remain typed overrides. Saving and reopening preserves texture resource IDs and unknown overrides.

## Custom Material Inspector

A Shader can name a native custom Inspector with `CustomEditor "Example/MaterialInspector"`. Register that identifier through the Editor project setup using the provider-neutral `MaterialInspectors` API. Callbacks receive a `MaterialDraft`, its `EditorDocument`, a `ResourceSession`, and the abstract `InspectorUI` controls. They do not receive ImGui or RHI objects, so another UI implementation or a future script provider can use the same boundary.

```cpp
auto registered = project.materialInspectors.Register(
    "Example/MaterialInspector",
    [](Ursa::Editor::MaterialInspectorContext& context,
       Ursa::Editor::InspectorUI& controls) -> Ursa::Result<bool>
    {
        const auto shader = context.draft.Material().Shader();
        if (!shader)
        {
            return Ursa::Result<bool>::Failure("Material Shader is not loaded");
        }
        auto current = context.draft.Material().PropertyValue("_Roughness", *shader);
        if (!current)
        {
            return Ursa::Result<bool>::Failure(current.error);
        }
        float roughness = current.value.floatValue;
        auto changed = controls.Float("Roughness", roughness, 0.0f, 1.0f, 0.01f);
        if (!changed || !changed.value)
        {
            return changed ? Ursa::Result<bool>::Success(false)
                           : Ursa::Result<bool>::Failure(changed.error);
        }
        auto set = Ursa::Editor::SetShaderMaterialProperty(
            context.document, "_Roughness", Ursa::MaterialPropertyValue::Range(roughness));
        return set ? Ursa::Result<bool>::Success(true) : Ursa::Result<bool>::Failure(set.error);
    });
```

If the named Inspector is not registered or reports an error, the Editor shows that diagnostic and uses the generated property controls. The example callback is compiled by `UrsaMaterialInspectorHeaderSmoke`.
