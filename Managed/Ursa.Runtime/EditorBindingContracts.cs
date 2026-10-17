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
}
