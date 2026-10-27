using System;
using System.Collections.Generic;
using System.Linq;
using Ursa;

namespace Ursa.Editor;

internal readonly record struct InspectorRegistration(Type Inspector, CustomEditorAttribute Declaration,
	bool Builtin = false, bool CanEditMultipleObjects = false);

internal static class InspectorTargetMatching
{
	internal static bool Supports(Type type) => typeof(Component).IsAssignableFrom(type) || type == typeof(GameObject) ||
		typeof(ScriptableObject).IsAssignableFrom(type) || typeof(AssetImporter).IsAssignableFrom(type) ||
		typeof(Material).IsAssignableFrom(type) || typeof(Mesh).IsAssignableFrom(type) || typeof(Texture).IsAssignableFrom(type);

	internal static Type? Select(Type actual, IEnumerable<InspectorRegistration> registrations, bool multiEdit = false)
	{
		var selected = new List<InspectorRegistration>();
		int nearest = int.MaxValue;
		foreach (InspectorRegistration registration in registrations)
		{
			if (multiEdit && !registration.CanEditMultipleObjects) continue;
			Type expected = registration.Declaration.InspectedType;
			int distance = 0;
			for (Type? current = actual; current != null && current != expected; current = current.BaseType) ++distance;
			if (expected != actual && (!registration.Declaration.EditorForChildClasses || !expected.IsAssignableFrom(actual))) continue;
			if (distance > nearest) continue;
			if (distance < nearest) selected.Clear();
			nearest = distance;
			if (selected.Count > 0 && selected[0].Builtin != registration.Builtin)
			{
				if (registration.Builtin) continue;
				selected.Clear();
			}
			selected.Add(registration);
		}
		if (selected.Count > 1)
			throw new InvalidOperationException(
				$"Ambiguous CustomEditor for '{actual.FullName}': {string.Join(", ", selected.Select(item => item.Inspector.FullName).Order(StringComparer.Ordinal))}.");
		return selected.Count == 0 ? null : selected[0].Inspector;
	}

	internal static string Identity(Type actual)
	{
		if (typeof(Behaviour).IsAssignableFrom(actual)) return BehaviourRegistry.IdentityFor(actual).Id;
		if (typeof(Component).IsAssignableFrom(actual)) return Component.TypeIdFor(actual);
		if (typeof(ScriptedImporter).IsAssignableFrom(actual))
		{
			ScriptIdentity identity = BehaviourRegistry.IdentityFor(actual);
			if (!Guid.TryParse(identity.SourceGuid, out Guid guid) || guid == Guid.Empty || identity.LocalFileId == 0)
				throw new InvalidOperationException($"Importer Inspector target '{actual.FullName}' has no stable source identity.");
			return $"importer:script:{guid:D}:{identity.LocalFileId}";
		}
		if (actual == typeof(TextureImporter)) return "importer:ursa.Texture";
		if (actual == typeof(ModelImporter)) return "importer:ursa.Model";
		if (typeof(ScriptableObject).IsAssignableFrom(actual)) return "asset:" + BehaviourRegistry.IdentityFor(actual).Id;
		if (actual == typeof(GameObject)) return "scene:ursa.GameObject";
		if (typeof(Material).IsAssignableFrom(actual)) return "asset:ursa.Material";
		if (typeof(Mesh).IsAssignableFrom(actual)) return "asset:ursa.Mesh";
		if (typeof(Texture).IsAssignableFrom(actual)) return "asset:ursa.Texture";
		throw new InvalidOperationException($"Unsupported Inspector target '{actual.FullName}'.");
	}
}
