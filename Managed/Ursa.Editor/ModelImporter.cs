namespace Ursa.Editor;

public sealed class ModelImporter : AssetImporter
{
	private readonly AssetImportContext? context;
	internal ModelImporter(AssetImportContext context) { this.context = context; BindImport(context); }
	internal ModelImporter(EditorImporterScope scope) { BindEditor(scope); }
	private ModelImporterSettingsValue Settings
	{
		get { if (EditorScope is { } editor) return editor.ReadModel(); context!.RequireValid(); return NativeImportBindings.GetModelSettings(); }
		set { if (EditorScope is { } editor) { editor.WriteModel(value); return; } context!.RequireValid(); NativeImportBindings.SetModelSettings(value); }
	}
	public float ScaleFactor { get => Settings.ScaleFactor; set { var settings = Settings; settings.ScaleFactor = value; Settings = settings; } }
	public bool GenerateNormals { get => Settings.GenerateNormals; set { var settings = Settings; settings.GenerateNormals = value; Settings = settings; } }
	public bool GenerateTangents { get => Settings.GenerateTangents; set { var settings = Settings; settings.GenerateTangents = value; Settings = settings; } }
	public bool FlipUvs { get => Settings.FlipUvs; set { var settings = Settings; settings.FlipUvs = value; Settings = settings; } }
	public bool ImportMaterials { get => Settings.ImportMaterials; set { var settings = Settings; settings.ImportMaterials = value; Settings = settings; } }
	public bool ImportTextures { get => Settings.ImportTextures; set { var settings = Settings; settings.ImportTextures = value; Settings = settings; } }
	public uint MaxBoneInfluences { get => Settings.MaxBoneInfluences; set { var settings = Settings; settings.MaxBoneInfluences = value; Settings = settings; } }
}
