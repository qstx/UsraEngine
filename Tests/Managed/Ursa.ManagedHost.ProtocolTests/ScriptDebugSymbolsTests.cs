using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
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
