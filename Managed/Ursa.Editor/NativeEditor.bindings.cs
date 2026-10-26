using Ursa.Bindings;

namespace Ursa.Editor;

[BindingModule("Editor", CallScoped = true, Dependencies = new[] { "Runtime", "Rendering" })]
[NativeHeader("Scripting/Bindings/EditorServices.hpp")]
internal static partial class NativeEditorBindings
{
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::CreatePreviewUtility")]
	internal static partial ulong CreatePreviewUtility();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::DestroyPreviewUtility")]
	internal static partial void DestroyPreviewUtility(ulong utility);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::BeginPreview")]
	internal static partial void BeginPreview(ulong utility, Rect rect);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SetPreviewCamera")]
	internal static partial void SetPreviewCamera(ulong utility, PreviewCamera camera);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SetPreviewLighting")]
	internal static partial void SetPreviewLighting(ulong utility, Color ambient, float ambientIntensity, Color light, float lightIntensity, Quaternion rotation);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::PreviewDrawMesh")]
	internal static partial void PreviewDrawMesh(ulong utility, [HandleKind(4)] NativeHandle mesh, [HandleKind(5)] NativeHandle material, Vector3 position, Quaternion rotation, Vector3 scale);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::PreviewInstantiatePrefab")]
	internal static partial void PreviewInstantiatePrefab(ulong utility, string guid, ulong localFileId);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::EndPreview")]
	internal static partial void EndPreview(ulong utility);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::DrawPreviewTexture")]
	internal static partial void DrawPreviewTexture(Rect rect, [HandleKind(3)] NativeHandle texture);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::DrawPreviewPixels")]
	internal static partial void DrawPreviewPixels(Rect rect, uint width, uint height, Color[] pixels, bool linear);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetPreviewInput")]
	internal static partial PreviewInput GetPreviewInput();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetRuntimeCapability")]
	internal static partial EditorBindingCapability GetRuntimeCapability();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetTargetHandle")]
	internal static partial NativeHandle GetTargetHandle();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetTargetHandles")]
	internal static partial NativeHandle[] GetTargetHandles();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetTargetPrimaryIndex")]
	internal static partial uint GetTargetPrimaryIndex();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetComponentPayload")]
	internal static partial string GetComponentPayload([HandleKind(2)] NativeHandle component);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ApplyComponentFields")]
	internal static partial bool ApplyComponentFields([HandleKind(2)] NativeHandle component, string fields);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetTargetPayload")]
	internal static partial string GetTargetPayload();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ApplyTargetFields")]
	internal static partial bool ApplyTargetFields(string fields);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetImporterInfo")]
	internal static partial EditorImporterInfo? GetImporterInfo();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::HasModifiedImporterSettings")]
	internal static partial bool HasModifiedImporterSettings();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::CanApplyImporterSettings")]
	internal static partial bool CanApplyImporterSettings();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ApplyImporterSettings")]
	internal static partial bool ApplyImporterSettings();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::RevertImporterSettings")]
	internal static partial void RevertImporterSettings();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::MarkImporterControlsDrawn")]
	internal static partial void MarkImporterControlsDrawn();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetScope")]
	internal static partial EditorScopeValue GetScope();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetSelection")]
	internal static partial EditorSelectionValue[] GetSelection();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetTarget")]
	internal static partial EditorReferenceValue? GetTarget();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetDocument")]
	internal static partial EditorDocumentValue? GetDocument();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetAssets")]
	internal static partial EditorAssetValue[] GetAssets();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ResolveReference")]
	internal static partial EditorReferenceValue ResolveReference(EditorReferenceKind kind, string id, NativeAssetReference? asset);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::MarkDirty")]
	internal static partial void MarkDirty();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Save")]
	internal static partial void Save();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::OpenAsset")]
	internal static partial void OpenAsset(string path);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SetActiveCamera")]
	internal static partial void SetActiveCamera();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Text")]
	internal static partial void Text(string text);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Button")]
	internal static partial bool Button(string label);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SmallButton")]
	internal static partial bool SmallButton(string label);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SameLine")]
	internal static partial void SameLine();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::NewLine")]
	internal static partial void NewLine();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Separator")]
	internal static partial void Separator();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::FoldoutHeader")]
	internal static partial bool FoldoutHeader(string label);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::EndFoldout")]
	internal static partial void EndFoldout();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::IsLastItemEdited")]
	internal static partial bool IsLastItemEdited();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::BeginGroup")]
	internal static partial void BeginGroup();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::EndGroup")]
	internal static partial void EndGroup();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Toggle")]
	internal static partial bool Toggle(string label, bool value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::IntField")]
	internal static partial long IntField(string label, long value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ULongField")]
	internal static partial ulong ULongField(string label, ulong value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::FloatField")]
	internal static partial double FloatField(string label, double value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SliderFloat")]
	internal static partial double SliderFloat(string label, double value, double minimum, double maximum, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::DragFloat")]
	internal static partial double DragFloat(string label, double value, double speed, double minimum, double maximum, string format, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::TextField")]
	internal static partial string TextField(string label, string value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Popup")]
	internal static partial int Popup(string label, int selected, string[] options, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Vector2Field")]
	internal static partial Vector2 Vector2Field(string label, Vector2 value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Vector3Field")]
	internal static partial Vector3 Vector3Field(string label, Vector3 value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Vector4Field")]
	internal static partial Vector4 Vector4Field(string label, Vector4 value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::QuaternionField")]
	internal static partial Quaternion QuaternionField(string label, Quaternion value, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ColorField")]
	internal static partial Color ColorField(string label, Color value, bool mixedValue, bool hdr);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ObjectField")]
	internal static partial EditorReferenceValue? ObjectField(string label, EditorReferenceValue? value, EditorReferenceKind kind, string expectedType, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::AssetField")]
	internal static partial EditorReferenceValue? AssetField(string label, EditorReferenceValue? value, EditorReferenceKind kind, string expectedType, bool mixedValue);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetProperty")]
	internal static partial SerializedPropertyState GetProperty(ulong revision, string path);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SetProperty")]
	internal static partial void SetProperty(ulong revision, string path, SerializedValue value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ResizeArray")]
	internal static partial void ResizeArray(ulong revision, string path, int size);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::MoveArrayElement")]
	internal static partial void MoveArrayElement(ulong revision, string path, int fromIndex, int toIndex);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::UpdateProperties")]
	internal static partial ulong UpdateProperties();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ApplyProperties")]
	internal static partial bool ApplyProperties(ulong revision);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::HasModifiedProperties")]
	internal static partial bool HasModifiedProperties(ulong revision);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetPropertyPaths")]
	internal static partial string[] GetPropertyPaths(ulong revision);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::PropertyField")]
	internal static partial void PropertyField(ulong revision, string path, string label);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::DrawDefaultInspector")]
	internal static partial bool DrawDefaultInspector(ulong revision);
}
