using System;
using System.Collections.Generic;
using System.Linq;

namespace Ursa.Editor;

internal sealed partial class ImportRegistry
{
	private readonly Dictionary<string, (Type Type, AssetPostprocessorInfo Info)> postprocessors = new(StringComparer.Ordinal);
	private readonly Dictionary<string, AssetPostprocessor> processorInstances = new(StringComparer.Ordinal);
	private ulong processorInvocation;
	private void RegisterPostprocessor(Type type)
	{
		if (!type.IsVisible || type.ContainsGenericParameters || type.GetConstructor(Type.EmptyTypes) == null)
			throw new InvalidOperationException($"AssetPostprocessor '{type.FullName}' must be public, concrete, non-generic and have a public parameterless constructor.");
		ScriptIdentity identity = BehaviourRegistry.IdentityFor(type);
		if (!Guid.TryParse(identity.SourceGuid, out Guid guid) || guid == Guid.Empty || identity.LocalFileId == 0)
			throw new InvalidOperationException($"AssetPostprocessor '{type.FullName}' has no stable source asset identity.");
		bool Overrides(string name)
		{
			var expected = typeof(AssetPostprocessor).GetMethod(name)!;
			var method = type.GetMethod(name, expected.GetParameters().Select(parameter => parameter.ParameterType).ToArray())!;
			return method.DeclaringType != typeof(AssetPostprocessor) && method.GetBaseDefinition() == expected;
		}
		uint targets = (Overrides(nameof(AssetPostprocessor.OnPreprocessTexture)) || Overrides(nameof(AssetPostprocessor.OnPostprocessTexture)) ? 1u : 0u) |
			(Overrides(nameof(AssetPostprocessor.OnPreprocessModel)) || Overrides(nameof(AssetPostprocessor.OnPostprocessModel)) ? 2u : 0u);
		if (targets == 0) throw new InvalidOperationException($"AssetPostprocessor '{type.FullName}' does not override a texture or model callback.");
		var processor = (AssetPostprocessor)Activator.CreateInstance(type)!;
		string stableId = $"script:{guid:D}:{identity.LocalFileId}";
		var info = new AssetPostprocessorInfo
		{
			SourceGuid = guid.ToString("D"), LocalFileId = identity.LocalFileId,
			ClrTypeName = type.FullName + ", " + type.Assembly.GetName().Name,
			Version = processor.GetVersion(), Order = processor.GetPostprocessOrder(), Targets = targets
		};
		if (!postprocessors.TryAdd(stableId, (type, info))) throw new InvalidOperationException($"Duplicate postprocessor identity '{stableId}'.");
	}
	public AssetPostprocessorInfo[] DescribePostprocessors()
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		return postprocessors.OrderBy(entry => entry.Value.Info.Order).ThenBy(entry => entry.Key, StringComparer.Ordinal)
			.Select(entry => entry.Value.Info).ToArray();
	}
	public void ProcessAsset(string identity)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		ImportExecution.RequireActive();
		var scope = NativeImportBindings.GetScope();
		if (!postprocessors.TryGetValue(identity, out var registered)) throw new InvalidOperationException($"Required AssetPostprocessor '{identity}' is missing.");
		if (scope.Phase is not (2 or 3 or 4 or 5) || scope.AssetPath.StartsWith("ursa:", StringComparison.Ordinal))
			throw new InvalidOperationException("Postprocessor requires a project texture or model phase.");
		uint target = scope.Phase is 2 or 3 ? 1u : 2u;
		if ((registered.Info.Targets & target) == 0) throw new InvalidOperationException("Postprocessor does not apply to this import type.");
		if (processorInvocation != scope.Invocation) { EndInvocation(); processorInvocation = scope.Invocation; }
		if (!processorInstances.TryGetValue(identity, out var processor))
		{
			processor = (AssetPostprocessor)Activator.CreateInstance(registered.Type)!;
			processorInstances.Add(identity, processor);
		}
		var context = new AssetImportContext();
		if (target == 1) processor.Bind(context, new TextureImporter(context));
		else processor.Bind(context, new ModelImporter(context));
		if (scope.Phase == 2) processor.OnPreprocessTexture();
		else if (scope.Phase == 3) processor.OnPostprocessTexture(Texture2D.FromImportHandle(NativeImportBindings.GetPostprocessObject()));
		else if (scope.Phase == 4) processor.OnPreprocessModel();
		else processor.OnPostprocessModel(GameObject.FromHandle(NativeImportBindings.GetPostprocessObject()));
	}
	public void EndInvocation() { processorInstances.Clear(); processorInvocation = 0; }
}
