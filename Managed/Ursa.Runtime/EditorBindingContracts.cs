using Ursa.Bindings;

namespace Ursa;

// Framework capability transfer only; project-facing Editor references never contain native addresses.
[WireValue]
internal struct EditorBindingCapability
{
	public ulong ModuleId;
	public ulong DescriptorAddress;
	public ulong ContextToken;
}

[WireValue]
internal enum EditorExtensionKind : uint { Menu, Window, Inspector }

[WireValue]
internal struct EditorExtensionInfo
{
	public string Id;
	public EditorExtensionKind Kind;
	public string Path;
	public string Target;
	public string Title;
	public bool CanEditMultipleObjects;
}

[WireValue]
public struct Rect
{
	public float X;
	public float Y;
	public float Width;
	public float Height;
	public Rect(float x, float y, float width, float height) { X = x; Y = y; Width = width; Height = height; }
}

[WireValue]
internal enum EditorPreviewPhase : uint { Query, Settings, Draw, Release, Inspector }

[WireValue]
internal struct EditorPreviewResult
{
	public bool Success;
	public bool HasPreview;
	public bool ConstantRepaint;
	public string Info;
	public string Diagnostic;
}
