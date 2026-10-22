using System;
using System.Collections.Generic;
using System.Text.Json;

namespace Ursa.Editor;

internal sealed class InspectorTargetScope : IDisposable
{
	[ThreadStatic] private static InspectorTargetScope? current;
	private readonly ulong generation = EditorApi.Generation;
	private readonly object target;
	private string? baseline;
	private sealed class ComponentSnapshot(Component value)
	{
		internal Component Value = value;
		internal string Baseline = string.Empty;
	}
	private readonly Dictionary<NativeHandle, ComponentSnapshot> components = new();
	private InspectorTargetScope(object target, bool initializeBaseline = true)
	{
		this.target = target;
		current = this;
		baseline = initializeBaseline && (target is ScriptableObject or Behaviour) ? Fields(target) : null;
	}
	internal object Target { get { EditorApi.RequireGeneration(generation); if (target is ScriptableObject asset) asset.RequireValid(); return target; } }
	internal static InspectorTargetScope Create(Type type)
	{
		EditorApi.EnsureRuntimeScope();
		if (!typeof(ScriptableObject).IsAssignableFrom(type))
		{
			var handle = NativeEditorBindings.GetTargetHandle();
			if (type == typeof(Material)) return new(Material.FromHandle(handle)!);
			if (type == typeof(Mesh)) return new(Mesh.FromHandle(handle)!);
			if (type == typeof(Texture2D)) return new(Texture2D.FromEditorHandle(EditorContext.Target?.AssetReference ?? default, handle));
			if (type == typeof(GameObject)) return new(GameObject.FromHandle(handle));
			if (typeof(Component).IsAssignableFrom(type))
			{
				ulong generation = EditorApi.Generation;
				var component = Component.CreateEditorProxy(type, handle, () => EditorApi.RequireGeneration(generation));
				var scope = new InspectorTargetScope(component, initializeBaseline: false);
				try
				{
					if (component is Behaviour)
					{
						scope.components.Add(handle, new(component));
						ScriptFieldSchema.Deserialize(component, NativeEditorBindings.GetTargetPayload());
						scope.baseline = Fields(component);
					}
					return scope;
				}
				catch { scope.Dispose(); throw; }
			}
			throw new InvalidOperationException("This Inspector target does not expose a typed object adapter.");
		}
		var reference = EditorContext.Target?.AssetReference ?? throw new InvalidOperationException("No source asset Inspector target is active.");
		var target = ScriptableObject.CreateEditorProxy(type, reference);
		try
		{
			ScriptFieldSchema.Deserialize(target, NativeEditorBindings.GetTargetPayload());
			return new(target);
		}
		catch { target.ReleaseEditorProxy(); throw; }
	}
	internal static Component? WrapComponent(NativeHandle handle, Type actual)
	{
		if (current == null) return null;
		if (current.components.TryGetValue(handle, out var existing)) return existing.Value;
		ulong generation = EditorApi.Generation;
		var component = Component.CreateEditorProxy(actual, handle, () => EditorApi.RequireGeneration(generation));
		var snapshot = new ComponentSnapshot(component);
		current.components.Add(handle, snapshot);
		try
		{
			ScriptFieldSchema.Deserialize(component, NativeEditorBindings.GetComponentPayload(handle));
			snapshot.Baseline = Fields(component);
			return component;
		}
		catch { current.components.Remove(handle); throw; }
	}
	private static string Fields(object target)
	{
		using var payload = JsonDocument.Parse(ScriptFieldSchema.Serialize(target));
		return payload.RootElement.GetProperty("Fields").GetRawText();
	}
	internal static void SynchronizeCurrent() => current?.Synchronize();
	internal void Synchronize()
	{
		EditorApi.RequireGeneration(generation);
		if (baseline != null)
		{
			string fields = Fields(target);
			var changes = ChangedFields(baseline, fields);
			if (changes.Count != 0) NativeEditorBindings.ApplyTargetFields(JsonSerializer.Serialize(changes));
			baseline = fields;
		}
		foreach (var entry in new List<KeyValuePair<NativeHandle, ComponentSnapshot>>(components))
		{
			if (ReferenceEquals(entry.Value.Value, target)) continue;
			string fields = Fields(entry.Value.Value);
			var changes = ChangedFields(entry.Value.Baseline, fields);
			if (changes.Count != 0) NativeEditorBindings.ApplyComponentFields(entry.Key, JsonSerializer.Serialize(changes));
			entry.Value.Baseline = fields;
		}
	}
	private static Dictionary<string, JsonElement> ChangedFields(string before, string after)
	{
		using var previous = JsonDocument.Parse(before);
		using var updated = JsonDocument.Parse(after);
		var changed = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
		foreach (var field in updated.RootElement.EnumerateObject())
			if (!previous.RootElement.TryGetProperty(field.Name, out var old) || old.GetRawText() != field.Value.GetRawText()) changed.Add(field.Name, field.Value.Clone());
		return changed;
	}
	public void Dispose()
	{
		if (ReferenceEquals(current, this)) current = null;
		components.Clear();
		if (target is ScriptableObject asset) asset.ReleaseEditorProxy();
	}
}
