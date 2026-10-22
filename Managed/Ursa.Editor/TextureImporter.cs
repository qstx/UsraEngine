namespace Ursa.Editor;

public enum TextureImportType { Default, NormalMap, LinearData }
public enum TextureFilterMode { Point, Linear }
public enum TextureWrapMode { Repeat, Clamp }

public sealed class TextureImporter : AssetImporter
{
	private readonly AssetImportContext? context;
	internal TextureImporter(AssetImportContext context) { this.context = context; BindImport(context); }
	internal TextureImporter(EditorImporterScope scope) { BindEditor(scope); }
	private TextureImporterSettingsValue Settings
	{
		get { if (EditorScope is { } editor) return editor.ReadTexture(); context!.RequireValid(); return NativeImportBindings.GetTextureSettings(); }
		set { if (EditorScope is { } editor) { editor.WriteTexture(value); return; } context!.RequireValid(); NativeImportBindings.SetTextureSettings(value); }
	}
	public TextureImportType TextureType { get => (TextureImportType)Settings.Type; set { var settings = Settings; settings.Type = (uint)value; if (value != TextureImportType.Default) settings.SRgb = false; Settings = settings; } }
	public bool SRgb { get => Settings.SRgb; set { var settings = Settings; settings.SRgb = value; Settings = settings; } }
	public bool Alpha { get => Settings.Alpha; set { var settings = Settings; settings.Alpha = value; Settings = settings; } }
	public bool GenerateMipmaps { get => Settings.GenerateMipmaps; set { var settings = Settings; settings.GenerateMipmaps = value; Settings = settings; } }
	public TextureFilterMode FilterMode { get => (TextureFilterMode)Settings.Filter; set { var settings = Settings; settings.Filter = (uint)value; Settings = settings; } }
	public TextureWrapMode WrapMode { get => (TextureWrapMode)Settings.Wrap; set { var settings = Settings; settings.Wrap = (uint)value; Settings = settings; } }
	public uint MaxDimension { get => Settings.MaxDimension; set { var settings = Settings; settings.MaxDimension = value; Settings = settings; } }
}
