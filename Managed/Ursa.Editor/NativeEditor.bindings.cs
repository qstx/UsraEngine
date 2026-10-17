using Ursa.Bindings;

namespace Ursa.Editor;

[BindingModule("Editor", CallScoped = true, Dependencies = new[] { "Runtime", "Rendering" })]
[NativeHeader("Scripting/Bindings/EditorServices.hpp")]
internal static partial class NativeEditorBindings
{
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
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Text")]
	internal static partial void Text(string text);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Button")]
	internal static partial bool Button(string label);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SameLine")]
	internal static partial void SameLine();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Separator")]
	internal static partial void Separator();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::BeginGroup")]
	internal static partial void BeginGroup();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::EndGroup")]
	internal static partial void EndGroup();
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Toggle")]
	internal static partial bool Toggle(string label, bool value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::IntField")]
	internal static partial long IntField(string label, long value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::FloatField")]
	internal static partial double FloatField(string label, double value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::TextField")]
	internal static partial string TextField(string label, string value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Popup")]
	internal static partial int Popup(string label, int selected, string[] options);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Vector2Field")]
	internal static partial Vector2 Vector2Field(string label, Vector2 value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Vector3Field")]
	internal static partial Vector3 Vector3Field(string label, Vector3 value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::Vector4Field")]
	internal static partial Vector4 Vector4Field(string label, Vector4 value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::QuaternionField")]
	internal static partial Quaternion QuaternionField(string label, Quaternion value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ColorField")]
	internal static partial Color ColorField(string label, Color value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ObjectField")]
	internal static partial EditorReferenceValue? ObjectField(string label, EditorReferenceValue? value, EditorReferenceKind kind);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::AssetField")]
	internal static partial EditorReferenceValue? AssetField(string label, EditorReferenceValue? value, EditorReferenceKind kind);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::GetProperty")]
	internal static partial SerializedPropertyState GetProperty(ulong revision, string path);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::SetProperty")]
	internal static partial void SetProperty(ulong revision, string path, SerializedValue value);
	[NativeMethod("Ursa::Editor::Bindings::EditorServices::ResizeArray")]
	internal static partial void ResizeArray(ulong revision, string path, int size);
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
