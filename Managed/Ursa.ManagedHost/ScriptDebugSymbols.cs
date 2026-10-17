using System;
using System.IO;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

namespace Ursa.ManagedHost;

internal static class ScriptDebugSymbols
{
	internal static void Validate(byte[] image, byte[] symbols)
	{
		if (symbols.Length == 0) throw new InvalidDataException("Project Portable PDB is empty.");
		using var pe = new PEReader(new MemoryStream(image, writable: false));
		using var provider = MetadataReaderProvider.FromPortablePdbStream(new MemoryStream(symbols, writable: false));
		MetadataReader metadata = provider.GetMetadataReader();
		DebugMetadataHeader header = metadata.DebugMetadataHeader
		                             ?? throw new InvalidDataException("Project symbols are not a Portable PDB.");
		var identity = new BlobContentId(header.Id);
		foreach (DebugDirectoryEntry entry in pe.ReadDebugDirectory())
		{
			if (entry.Type != DebugDirectoryEntryType.CodeView) continue;
			CodeViewDebugDirectoryData codeView = pe.ReadCodeViewDebugDirectoryData(entry);
			if (codeView.Guid == identity.Guid && entry.Stamp == identity.Stamp) return;
		}
		throw new InvalidDataException("Project Portable PDB does not match its assembly debug identity.");
	}
}
