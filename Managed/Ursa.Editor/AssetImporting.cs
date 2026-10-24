using System;
using System.Text;

namespace Ursa.Editor;

[AttributeUsage(AttributeTargets.Class, Inherited = false, AllowMultiple = false)]
public sealed class ScriptedImporterAttribute : Attribute
{
	public ScriptedImporterAttribute(uint version, params string[] extensions)
	{
		Version = version;
		Extensions = (string[])extensions.Clone();
	}
	public uint Version { get; }
	public string[] Extensions { get; }
}

public abstract class AssetImporter : ISerializedExtensionTarget
{
	private string assetPath = string.Empty;
	private AssetImportContext? importContext;
	internal EditorImporterScope? EditorScope { get; private set; }
	public string AssetPath { get { RequireAccess(); return assetPath; } internal set => assetPath = value; }
	internal void BindImport(AssetImportContext context) { importContext = context; assetPath = context.AssetPath; }
	internal void BindEditor(EditorImporterScope scope) { EditorScope = scope; assetPath = scope.Info.Path; }
	private void RequireAccess() { if (EditorScope != null) EditorScope.RequireValid(); else importContext?.RequireValid(); }
	void ISerializedExtensionTarget.RequireSerializedAccess() => RequireAccess();
	public void SaveAndReimport()
	{
		if (EditorScope == null) throw new InvalidOperationException("SaveAndReimport requires an active importer Inspector; import callbacks cannot synchronously reimport.");
		EditorScope.Apply();
	}
}

public abstract class ScriptedImporter : AssetImporter
{
	public abstract void OnImportAsset(AssetImportContext context);
}

public sealed class AssetImportContext
{
	private readonly ImportScopeValue scope;
	internal AssetImportContext()
	{
		ImportExecution.RequireActive();
		scope = NativeImportBindings.GetScope();
	}
	internal void RequireValid()
	{
		ImportExecution.RequireActive();
		NativeImportBindings.RequireInvocation(scope.Session, scope.Generation, scope.Invocation);
	}
	public string AssetPath { get { RequireValid(); return scope.AssetPath; } }
	public byte[] ReadSourceBytes() { RequireValid(); return NativeImportBindings.ReadSourceBytes(); }
	public byte[] ReadSourceAssetBytes(string path) { RequireValid(); return NativeImportBindings.ReadSourceAsset(path); }
	public string ReadSourceAssetText(string path) => DecodeText(ReadSourceAssetBytes(path));
	public void DependsOnSourceAsset(string path) { RequireValid(); NativeImportBindings.DependsOnSourceAsset(path); }
	public byte[] ReadArtifactBytes(AssetReference reference)
	{
		RequireValid();
		return NativeImportBindings.ReadArtifact(new NativeAssetReference { Guid = reference.Guid, LocalFileId = reference.LocalFileId });
	}
	public void DependsOnArtifact(AssetReference reference)
	{
		RequireValid();
		NativeImportBindings.DependsOnArtifact(new NativeAssetReference { Guid = reference.Guid, LocalFileId = reference.LocalFileId });
	}
	public void LogImportWarning(string message) { RequireValid(); NativeImportBindings.LogDiagnostic(1, message); }
	public void LogImportError(string message) { RequireValid(); NativeImportBindings.LogDiagnostic(2, message); }
	public string ReadSourceText()
	{
		return DecodeText(ReadSourceBytes());
	}
	private static string DecodeText(byte[] bytes)
	{
		int offset = bytes.Length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
		return new UTF8Encoding(false, true).GetString(bytes, offset, bytes.Length - offset);
	}
	public void AddObjectToAsset(string identifier, object value)
	{
		AddObjectToAsset(identifier, value, identifier);
	}
	public void AddObjectToAsset(string identifier, object value, string displayName)
	{
		RequireValid();
		ArgumentNullException.ThrowIfNull(value);
		NativeImportBindings.AddObject(identifier, ImportExecution.ReferenceFor(value),
			string.IsNullOrWhiteSpace(displayName) ? identifier : displayName);
	}
	public void SetMainObject(object value)
	{
		RequireValid();
		ArgumentNullException.ThrowIfNull(value);
		NativeImportBindings.SetMainObject(ImportExecution.ReferenceFor(value).Token);
	}
}
