using System;
using System.Text.Json.Serialization;

namespace Ursa;

public readonly struct AssetReference
{
	[JsonConstructor]
	public AssetReference(string guid, ulong localFileId = 0)
	{
		if (!System.Guid.TryParse(guid, out Guid parsed))
		{
			throw new ArgumentException("Asset GUID is invalid.", nameof(guid));
		}
		Guid = parsed.ToString("D");
		LocalFileId = localFileId;
	}

	public string Guid { get; }

	public ulong LocalFileId { get; }
}
