using System;

namespace Ursa.Bindings;

public enum BindingThread : uint
{
	Owner,
	Any
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class BindingModuleAttribute(string name) : Attribute
{
	public string Name { get; } = name;
	public string[] Dependencies { get; set; } = Array.Empty<string>();
	public bool Required { get; set; }
	public bool CallScoped { get; set; }
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class NativeHeaderAttribute(string header) : Attribute
{
	public string Header { get; } = header;
}

[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class NativeMethodAttribute(string name) : Attribute
{
	public string Name { get; } = name;
	public BindingThread Thread { get; set; } = BindingThread.Owner;
}

[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class ManagedCallbackAttribute(string target) : Attribute
{
	public string Target { get; } = target;
	public BindingThread Thread { get; set; } = BindingThread.Owner;
}

[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class RecordedCommandAttribute : Attribute
{
	public bool NativeOnly { get; set; }
}

[AttributeUsage(AttributeTargets.Struct | AttributeTargets.Class | AttributeTargets.Enum, Inherited = false)]
public sealed class WireValueAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Struct, Inherited = false)]
public sealed class SerializedArchiveAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Parameter, Inherited = false)]
public sealed class HandleKindAttribute(uint kind) : Attribute
{
	public uint Kind { get; } = kind;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, Inherited = false)]
public sealed class WireIgnoreAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Parameter, Inherited = false)]
public sealed class BindingSessionAttribute : Attribute
{
}
