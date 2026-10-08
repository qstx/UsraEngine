using System;

namespace Ursa;

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class UrsaScriptTypeAttribute : Attribute
{
	public UrsaScriptTypeAttribute(string id)
	{
		Id = string.IsNullOrWhiteSpace(id) ? throw new ArgumentException("Script type ID is required.", nameof(id)) : id;
	}

	public string Id { get; }

	public uint Version { get; set; } = 1;
}
