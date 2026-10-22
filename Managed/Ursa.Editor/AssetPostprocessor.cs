using System;

namespace Ursa.Editor;

public abstract class AssetPostprocessor
{
	private AssetImportContext? context;
	public AssetImportContext Context => context ?? throw new InvalidOperationException("Postprocessor context is available only during its callback.");
	public string AssetPath => Context.AssetPath;
	public AssetImporter AssetImporter { get; internal set; } = null!;
	internal void Bind(AssetImportContext value, AssetImporter importer) { context = value; AssetImporter = importer; }
	public virtual uint GetVersion() => 0;
	public virtual int GetPostprocessOrder() => 0;
	public virtual void OnPreprocessTexture() { }
	public virtual void OnPostprocessTexture(Texture2D texture) { }
	public virtual void OnPreprocessModel() { }
	public virtual void OnPostprocessModel(GameObject root) { }
}
