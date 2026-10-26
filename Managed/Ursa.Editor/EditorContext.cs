using System;
using System.Linq;

namespace Ursa.Editor;

public static class EditorContext
{
	public static EditorSelection[] Selection { get { _ = EditorApi.Generation; return NativeEditorBindings.GetSelection().Select(value => new EditorSelection(value)).ToArray(); } }
	public static EditorReference? Target { get { _ = EditorApi.Generation; return NativeEditorBindings.GetTarget() is { } value ? new(value) : null; } }
	public static EditorDocument? Document { get { _ = EditorApi.Generation; return NativeEditorBindings.GetDocument() is { } value ? new(value) : null; } }
	public static EditorAsset[] Assets { get { _ = EditorApi.Generation; return NativeEditorBindings.GetAssets().Select(value => new EditorAsset(value)).ToArray(); } }
	public static string[] ProjectTags { get { _ = EditorApi.Generation; return NativeEditorBindings.GetProjectTags(); } }
	public static string[] ProjectLayers { get { _ = EditorApi.Generation; return NativeEditorBindings.GetProjectLayers(); } }
	internal static uint ComponentMenuState { get { _ = EditorApi.Generation; return NativeEditorBindings.GetComponentMenuState(); } }
	internal static void SetComponentMenuCommand(uint command)
	{
		_ = EditorApi.Generation;
		NativeEditorBindings.SetComponentMenuCommand(command);
	}
	public static void OpenTagLayerSettings(bool layers) { _ = EditorApi.Generation; NativeEditorBindings.OpenTagLayerSettings(layers); }
	public static EditorReference ResolveObject(EditorReferenceKind kind, string persistentId)
	{
		_ = EditorApi.Generation;
		return new(NativeEditorBindings.ResolveReference(kind, persistentId, null));
	}
	public static EditorReference ResolveAsset(AssetReference asset, EditorReferenceKind kind = EditorReferenceKind.Asset)
	{
		_ = EditorApi.Generation;
		return new(NativeEditorBindings.ResolveReference(kind, string.Empty, NativeAssetReference.From(asset)));
	}
	public static void Save() { _ = EditorApi.Generation; InspectorTargetScope.SynchronizeCurrent(); NativeEditorBindings.Save(); }
	public static void MarkDirty() { _ = EditorApi.Generation; InspectorTargetScope.SynchronizeCurrent(); NativeEditorBindings.MarkDirty(); }
	public static void OpenAsset(string virtualPath) { _ = EditorApi.Generation; NativeEditorBindings.OpenAsset(virtualPath); }
	public static void SetActiveCamera() { _ = EditorApi.Generation; NativeEditorBindings.SetActiveCamera(); }
	internal static void SaveMeshComponentAs(string path, bool replaceBinding)
	{
		_ = EditorApi.Generation;
		ArgumentException.ThrowIfNullOrWhiteSpace(path);
		NativeEditorBindings.SaveMeshComponentAs(path, replaceBinding);
	}
}
