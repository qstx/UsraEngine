using System;
using System.Text.Json;

namespace Ursa;

internal static partial class EngineApi
{
	internal static readonly JsonSerializerOptions ResourceJson = new()
	{
		IncludeFields = true,
		PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
		PropertyNameCaseInsensitive = true
	};

	internal static bool ResourceValid(NativeHandle handle)
	{
		try { return NativeResourceBindings.IsValid(handle); }
		catch (InvalidOperationException) { return false; }
	}

	internal static JsonElement SnapshotResourceReference(RenderResource resource)
	{
		ResourceReferenceArchive archive = resource switch
		{
			Mesh => NativeSerializationBindings.SnapshotMesh(resource.Handle),
			Material => NativeSerializationBindings.SnapshotMaterial(resource.Handle),
			_ => throw new InvalidOperationException("Only Mesh and Material have serialized resource references.")
		};
		if (archive.Version != 1) throw new InvalidOperationException("Unsupported resource reference archive version.");
		using JsonDocument document = JsonDocument.Parse(archive.Json);
		return document.RootElement.Clone();
	}

	internal static NativeHandle RestoreMeshReference(JsonElement value) =>
		NativeSerializationBindings.RestoreMesh(new() { Version = 1, Json = value.GetRawText() });

	internal static NativeHandle RestoreMaterialReference(JsonElement value) =>
		NativeSerializationBindings.RestoreMaterial(new() { Version = 1, Json = value.GetRawText() });
}
