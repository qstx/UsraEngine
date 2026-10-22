using System;
using System.Linq;
using Ursa;
using Ursa.Editor;

internal static unsafe partial class Program
{
	private static void TestPostprocessorRegistration()
	{
		var previous = BehaviourRegistry.identityResolver;
		BehaviourRegistry.identityResolver = type => new ScriptIdentity("test.processor", 1,
			type == typeof(MissingProcessorIdentity) ? "" : "12345678-1234-4234-8234-123456789abc",
			type == typeof(EarlyProcessor) ? 3ul : type == typeof(BothProcessor) ? 10ul : 2ul);
		try
		{
			Type[] types = { typeof(ModelProcessor), typeof(EarlyProcessor), typeof(BothProcessor) };
			using var first = ImportRegistry.CreateForTypes(types, Array.Empty<string>());
			using var reversed = ImportRegistry.CreateForTypes(types.Reverse(), Array.Empty<string>());
			var descriptors = first.DescribePostprocessors();
			Expect(descriptors.Select(item => item.LocalFileId).SequenceEqual(new ulong[] { 3, 10, 2 }),
			       "Postprocessor priority or ordinal stable-identity tie break differs.");
			Expect(descriptors.Select(item => item.LocalFileId).SequenceEqual(reversed.DescribePostprocessors().Select(item => item.LocalFileId)),
			       "Postprocessor discovery order changed the chain.");
			Expect(descriptors[0].Version == 7 && descriptors[0].Targets == 1 && descriptors[1].Targets == 3 && descriptors[2].Targets == 2,
			       "Postprocessor version or callback applicability was not described.");
			descriptors[0].Version = 99;
			Expect(first.DescribePostprocessors()[0].Version == 7, "Postprocessor descriptor escaped as a mutable alias.");
			using var renamed = ImportRegistry.CreateForTypes(new[] { typeof(RenamedModelProcessor) }, Array.Empty<string>());
			Expect(renamed.DescribePostprocessors()[0].LocalFileId == descriptors[2].LocalFileId, "CLR rename changed processor identity.");
			foreach (Type[] invalid in new[] { new[] { typeof(ModelProcessor), typeof(RenamedModelProcessor) },
				new[] { typeof(EmptyProcessor) }, new[] { typeof(MissingProcessorIdentity) }, new[] { typeof(PrivateConstructorProcessor) }, new[] { typeof(InvalidProcessorAttribute) } })
			{
				bool rejected = false;
				try { using var unused = ImportRegistry.CreateForTypes(invalid, Array.Empty<string>()); }
				catch (InvalidOperationException) { rejected = true; }
				Expect(rejected, "Invalid postprocessor declaration was accepted.");
			}
			first.Dispose();
			bool closed = false;
			try { first.DescribePostprocessors(); } catch (ObjectDisposedException) { closed = true; }
			Expect(closed, "Disposed processor registry retained its project types.");
		}
		finally { BehaviourRegistry.identityResolver = previous; }
	}
}

public sealed class EarlyProcessor : AssetPostprocessor
{
	public override uint GetVersion() => 7;
	public override int GetPostprocessOrder() => -1;
	public override void OnPreprocessTexture() { }
}
public sealed class BothProcessor : AssetPostprocessor
{
	public override void OnPostprocessTexture(Texture2D texture) { }
	public override void OnPreprocessModel() { }
}
public sealed class ModelProcessor : AssetPostprocessor { public override void OnPostprocessModel(GameObject root) { } }
public sealed class RenamedModelProcessor : AssetPostprocessor { public override void OnPostprocessModel(GameObject root) { } }
public sealed class MissingProcessorIdentity : AssetPostprocessor { public override void OnPreprocessTexture() { } }
public sealed class EmptyProcessor : AssetPostprocessor { }
[ScriptedImporter(1, "invalid-processor")]
public sealed class InvalidProcessorAttribute : AssetPostprocessor { public override void OnPreprocessModel() { } }
public sealed class PrivateConstructorProcessor : AssetPostprocessor
{
	private PrivateConstructorProcessor() { }
	public override void OnPreprocessModel() { }
}
