using Ursa.Bindings;

namespace Ursa;

[BindingModule("Import", CallScoped = true, Dependencies = new[] { "Runtime", "Rendering" })]
[NativeHeader("Scripting/Bindings/ImportServices.hpp")]
internal static partial class NativeImportBindings
{
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::GetImporterSettings")]
	internal static partial string GetImporterSettings();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::GetScope")]
	internal static partial ImportScopeValue GetScope();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::BeginInvocation")]
	internal static partial void BeginInvocation();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::EndInvocation")]
	internal static partial void EndInvocation();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::ReadSourceBytes")]
	internal static partial byte[] ReadSourceBytes();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::ReadSourceAsset")]
	internal static partial byte[] ReadSourceAsset(string path);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::DependsOnSourceAsset")]
	internal static partial void DependsOnSourceAsset(string path);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::ReadArtifact")]
	internal static partial byte[] ReadArtifact(NativeAssetReference reference);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::DependsOnArtifact")]
	internal static partial void DependsOnArtifact(NativeAssetReference reference);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::LogDiagnostic")]
	internal static partial void LogDiagnostic(uint severity, string message);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::GetTextureSettings")]
	internal static partial TextureImporterSettingsValue GetTextureSettings();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::SetTextureSettings")]
	internal static partial void SetTextureSettings(TextureImporterSettingsValue settings);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::GetPostprocessObject")]
	internal static partial NativeHandle GetPostprocessObject();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::GetModelSettings")]
	internal static partial ModelImporterSettingsValue GetModelSettings();
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::SetModelSettings")]
	internal static partial void SetModelSettings(ModelImporterSettingsValue settings);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::AddObject")]
	internal static partial void AddObject(string key, ImportReferenceInfo reference);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::SetMainObject")]
	internal static partial void SetMainObject(ulong token);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::RequireInvocation")]
	internal static partial void RequireInvocation(ulong session, ulong generation, ulong invocation);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::RequireCandidate")]
	internal static partial void RequireCandidate(NativeHandle candidate);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::CreateTexture2D")]
	internal static partial NativeHandle CreateTexture2D(uint width, uint height, bool linear);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::GetTexture2DInfo")]
	internal static partial Texture2DInfo GetTexture2DInfo(NativeHandle texture);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::GetTexture2DPixels")]
	internal static partial Color[] GetTexture2DPixels(NativeHandle texture);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::SetTexture2DPixels")]
	internal static partial void SetTexture2DPixels(NativeHandle texture, Color[] pixels);
	[NativeMethod("Ursa::Editor::Bindings::ImportServices::SetMaterialTexture")]
	internal static partial void SetMaterialTexture(NativeHandle material, NativeHandle texture, NativeAssetReference? reference, MaterialTextureSlot slot, string name);
}
