using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Security.Cryptography;
using Ursa.ManagedHost;

internal static class ScriptDebugSymbolsTests
{
	internal static void Run()
	{
		string path = Assembly.GetExecutingAssembly().Location;
		byte[] image = File.ReadAllBytes(path);
		byte[] symbols = File.ReadAllBytes(Path.ChangeExtension(path, ".pdb"));
		ScriptDebugSymbols.Validate(image, symbols);
		using var provider = MetadataReaderProvider.FromPortablePdbStream(new MemoryStream(symbols, writable: false));
		MetadataReader metadata = provider.GetMetadataReader();
		string[] documents = metadata.Documents.Select(handle => metadata.GetString(metadata.GetDocument(handle).Name)).ToArray();
		Expect(documents.Any(document => document.EndsWith("ScriptDebugSymbolsTests.cs", StringComparison.Ordinal) &&
		                                File.Exists(document)), "Portable PDB does not resolve the original source file.");
		byte[] identity = metadata.DebugMetadataHeader!.Id.ToArray();
		int offset = symbols.AsSpan().IndexOf(identity);
		Expect(offset >= 0, "Portable PDB identity is missing.");
		byte[] mismatched = (byte[])symbols.Clone();
		mismatched[offset] ^= 1;
		ExpectRejected(image, mismatched);
		ExpectRejected(image, Array.Empty<byte>());
		ValidateFormalSources();
	}

	private static void ValidateFormalSources()
	{
		string? output = Environment.GetEnvironmentVariable("URSA_FORMAL_MANAGED_DIR");
		string? root = Environment.GetEnvironmentVariable("URSA_SOURCE_ROOT");
		if (string.IsNullOrEmpty(output) || string.IsNullOrEmpty(root)) return;
		string managedRoot = Path.GetFullPath(Path.Combine(root, "Managed")) + Path.DirectorySeparatorChar;
		string expectedTest = Path.GetFullPath(Path.Combine(root, "Tests", "Managed",
			"Ursa.ManagedHost.ProtocolTests", "ScriptDebugSymbolsTests.cs"));
		using (var own = MetadataReaderProvider.FromPortablePdbStream(File.OpenRead(
			Path.ChangeExtension(Assembly.GetExecutingAssembly().Location, ".pdb"))))
		{
			MetadataReader reader = own.GetMetadataReader();
			Expect(reader.Documents.Any(handle => string.Equals(reader.GetString(reader.GetDocument(handle).Name),
				expectedTest, StringComparison.OrdinalIgnoreCase)), "Managed tests still compile a source copy.");
		}
		foreach (string name in new[] { "Ursa.Bindings", "Ursa.Runtime", "Ursa.Rendering", "Ursa.Editor", "Ursa.ManagedHost" })
		{
			byte[] image = File.ReadAllBytes(Path.Combine(output, name + ".dll"));
			byte[] symbols = File.ReadAllBytes(Path.Combine(output, name + ".pdb"));
			ScriptDebugSymbols.Validate(image, symbols);
			using var provider = MetadataReaderProvider.FromPortablePdbStream(new MemoryStream(symbols, writable: false));
			MetadataReader reader = provider.GetMetadataReader();
			int originalSources = 0;
			foreach (DocumentHandle handle in reader.Documents)
			{
				Document document = reader.GetDocument(handle);
				string path = Path.GetFullPath(reader.GetString(document.Name));
				Expect(!path.Replace('\\', '/').Contains("/Managed/Source/", StringComparison.OrdinalIgnoreCase),
					"Formal PDB still points to staged source: " + path);
				if (!path.StartsWith(managedRoot, StringComparison.OrdinalIgnoreCase)) continue;
				Expect(!path.Replace('\\', '/').Contains("/obj/", StringComparison.OrdinalIgnoreCase),
					"Formal compilation included old intermediate source: " + path);
				byte[] source = File.ReadAllBytes(path);
				Guid algorithm = reader.GetGuid(document.HashAlgorithm);
				byte[] checksum = algorithm == new Guid("8829d00f-11b8-4213-878b-770e8597ac16")
					? SHA256.HashData(source) : SHA1.HashData(source);
				Expect(checksum.SequenceEqual(reader.GetBlobBytes(document.Hash)), "PDB source checksum differs: " + path);
				++originalSources;
			}
			Expect(originalSources > 0, "Formal PDB has no original framework sources: " + name);
		}
	}

	private static void ExpectRejected(byte[] image, byte[] symbols)
	{
		try { ScriptDebugSymbols.Validate(image, symbols); }
		catch (InvalidDataException) { return; }
		throw new InvalidOperationException("Invalid or mismatched Portable PDB was accepted.");
	}

	private static void Expect(bool condition, string message)
	{
		if (!condition) throw new InvalidOperationException(message);
	}
}
