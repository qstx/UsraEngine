using System;

namespace Ursa;

[AttributeUsage(AttributeTargets.Class, Inherited = false, AllowMultiple = false)]
public sealed class CreateAssetMenuAttribute : Attribute
{
	public string MenuName { get; set; } = string.Empty;

	public string FileName { get; set; } = "NewAsset";

	public int Order { get; set; }
}
