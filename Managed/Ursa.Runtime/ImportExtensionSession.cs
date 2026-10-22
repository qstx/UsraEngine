using System;
using Ursa.Bindings;

namespace Ursa;

[WireValue]
internal struct ScriptedImporterInfo
{
	public string SourceGuid;
	public ulong LocalFileId;
	public string ClrTypeName;
	public uint Version;
	public string[] Extensions;
	public string DefaultSettings;
}

internal interface IImportExtensionSession : IDisposable
{
	ScriptedImporterInfo[] DescribeImporters();
	AssetPostprocessorInfo[] DescribePostprocessors();
	void ImportAsset(string identity);
	void ProcessAsset(string identity);
	void EndInvocation();
}

[WireValue]
internal struct AssetPostprocessorInfo
{
	public string SourceGuid;
	public ulong LocalFileId;
	public string ClrTypeName;
	public uint Version;
	public int Order;
	public uint Targets;
}
