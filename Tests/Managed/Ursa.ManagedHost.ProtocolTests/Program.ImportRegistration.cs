using System;
using Ursa;
using Ursa.Editor;

internal static unsafe partial class Program
{
	private static void TestImporterRegistration()
	{
		var previous = BehaviourRegistry.identityResolver;
		BehaviourRegistry.identityResolver = type => new ScriptIdentity("test.importer", 1,
			type == typeof(MissingIdentityImporter) ? "" : "12345678-1234-4234-8234-123456789abc",
			type == typeof(RegistrationImporter) || type == typeof(RenamedRegistrationImporter) ? 1ul : 2ul);
		try
		{
			using var registry = ImportRegistry.CreateForTypes(new[] { typeof(RegistrationImporter) }, new[] { ".png", ".fbx" });
			var first = registry.DescribeImporters();
			Expect(first.Length == 1 && first[0].Version == 3 && first[0].Extensions[0] == ".ursareg" && first[0].LocalFileId == 1,
			       "ScriptedImporter registration lost its normalized format, version or stable identity.");
			first[0].Extensions[0] = ".mutated";
			Expect(registry.DescribeImporters()[0].Extensions[0] == ".ursareg", "Importer descriptors were mutable aliases.");
			using var ordered = ImportRegistry.CreateForTypes(new[] { typeof(RegistrationImporter), typeof(SecondUniqueImporter) }, Array.Empty<string>());
			using var reversed = ImportRegistry.CreateForTypes(new[] { typeof(SecondUniqueImporter), typeof(RegistrationImporter) }, Array.Empty<string>());
			Expect(ordered.DescribeImporters()[0].LocalFileId == reversed.DescribeImporters()[0].LocalFileId &&
			       ordered.DescribeImporters()[1].Extensions[0] == reversed.DescribeImporters()[1].Extensions[0], "Importer discovery order changed registrations.");
			using var renamed = ImportRegistry.CreateForTypes(new[] { typeof(RenamedRegistrationImporter) }, Array.Empty<string>());
			Expect(renamed.DescribeImporters()[0].SourceGuid == first[0].SourceGuid && renamed.DescribeImporters()[0].LocalFileId == first[0].LocalFileId,
			       "CLR rename changed the importer identity.");
			void Reject(Type[] types, string[] reserved)
			{
				bool rejected = false;
				try { using var invalid = ImportRegistry.CreateForTypes(types, reserved); }
				catch (InvalidOperationException) { rejected = true; }
				Expect(rejected, "Invalid or conflicting importer declaration was accepted.");
			}
			Reject(new[] { typeof(RegistrationImporter), typeof(DuplicateRegistrationImporter) }, Array.Empty<string>());
			Reject(new[] { typeof(DuplicateRegistrationImporter), typeof(RegistrationImporter) }, Array.Empty<string>());
			Reject(new[] { typeof(NativeCollisionImporter) }, new[] { ".png" });
			Reject(new[] { typeof(ZeroVersionImporter) }, Array.Empty<string>());
			Reject(new[] { typeof(MalformedExtensionImporter) }, Array.Empty<string>());
			Reject(new[] { typeof(MissingIdentityImporter) }, Array.Empty<string>());
			Reject(new[] { typeof(UndeclaredImporter) }, Array.Empty<string>());
			Reject(new[] { typeof(WrongImporterBase) }, Array.Empty<string>());
			using var removed = ImportRegistry.CreateForTypes(Array.Empty<Type>(), Array.Empty<string>());
			Expect(removed.DescribeImporters().Length == 0, "Removed importer survived in another registration snapshot.");
			registry.Dispose();
			bool disposed = false;
			try { registry.DescribeImporters(); } catch (ObjectDisposedException) { disposed = true; }
			Expect(disposed, "Closed importer registry retained project types.");
		}
		finally { BehaviourRegistry.identityResolver = previous; }
	}
}

[ScriptedImporter(3, ".URSAREG")]
public sealed class RegistrationImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
[ScriptedImporter(3, "ursareg")]
public sealed class RenamedRegistrationImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
[ScriptedImporter(1, "ursareg")]
public sealed class DuplicateRegistrationImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
[ScriptedImporter(1, "png")]
public sealed class NativeCollisionImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
[ScriptedImporter(0, "zero")]
public sealed class ZeroVersionImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
[ScriptedImporter(1, "foo.bar")]
public sealed class MalformedExtensionImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
[ScriptedImporter(1, "missing")]
public sealed class MissingIdentityImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
public sealed class UndeclaredImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
[ScriptedImporter(1, "wrong")]
public sealed class WrongImporterBase { }
[ScriptedImporter(1, "anotherfmt")]
public sealed class SecondUniqueImporter : ScriptedImporter { public override void OnImportAsset(AssetImportContext context) { } }
