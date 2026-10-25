using System;
using System.Collections.Generic;
using System.Text.Json;

namespace Ursa.Editor;

internal sealed class InspectorTargetScope : IDisposable
{
	[ThreadStatic] private static InspectorTargetScope? current;
	private readonly ulong generation = EditorApi.Generation;
	private readonly object target;
	private readonly object[] targets;
	private readonly List<(int Index, object Target, string Baseline)> targetBaselines = new();
	private sealed class ComponentSnapshot(Component value)
	{
		internal Component Value = value;
		internal string Baseline = string.Empty;
	}
	private readonly Dictionary<NativeHandle, ComponentSnapshot> components = new();
	private InspectorTargetScope(object[] targets, int primaryIndex)
	{
		if (targets.Length == 0 || (uint)primaryIndex >= (uint)targets.Length)
			throw new InvalidOperationException("Inspector target group is empty or has an invalid primary target.");
		this.targets = targets;
		target = targets[primaryIndex];
		current = this;
		for (int index = 0; index < targets.Length; index++)
			if (targets[index] is ScriptableObject or Behaviour)
				targetBaselines.Add((index, targets[index], Fields(targets[index])));
	}
	internal object Target { get { EditorApi.RequireGeneration(generation); if (target is ScriptableObject asset) asset.RequireValid(); return target; } }
	internal object[] Targets
	{
		get
		{
			EditorApi.RequireGeneration(generation);
			foreach (object item in targets)
				if (item is ScriptableObject asset) asset.RequireValid();
			return (object[])targets.Clone();
		}
	}
	internal static InspectorTargetScope Create(Type type)
	{
		EditorApi.EnsureRuntimeScope();
		if (typeof(ScriptableObject).IsAssignableFrom(type))
		{
			var reference = EditorContext.Target?.AssetReference ?? throw new InvalidOperationException("No source asset Inspector target is active.");
			var asset = ScriptableObject.CreateEditorProxy(type, reference);
			try
			{
				ScriptFieldSchema.Deserialize(asset, NativeEditorBindings.GetTargetPayload());
				return new(new object[] { asset }, 0);
			}
			catch { asset.ReleaseEditorProxy(); throw; }
		}
		NativeHandle[] handles = NativeEditorBindings.GetTargetHandles();
		int primaryIndex = checked((int)NativeEditorBindings.GetTargetPrimaryIndex());
		if (handles.Length == 0 || (uint)primaryIndex >= (uint)handles.Length)
			throw new InvalidOperationException("Native Inspector returned an invalid target group.");
		object[] targets = new object[handles.Length];
		ulong generation = EditorApi.Generation;
		for (int index = 0; index < handles.Length; index++)
		{
			NativeHandle handle = handles[index];
			if (type == typeof(Material)) targets[index] = Material.FromHandle(handle)!;
			else if (type == typeof(Mesh)) targets[index] = Mesh.FromHandle(handle)!;
			else if (type == typeof(Texture2D)) targets[index] = Texture2D.FromEditorHandle(EditorContext.Target?.AssetReference ?? default, handle);
			else if (type == typeof(GameObject)) targets[index] = GameObject.FromHandle(handle);
			else if (typeof(Component).IsAssignableFrom(type))
			{
				var component = Component.CreateEditorProxy(type, handle, () => EditorApi.RequireGeneration(generation));
				targets[index] = component;
				if (component is Behaviour)
				{
					ScriptFieldSchema.Deserialize(component, NativeEditorBindings.GetComponentPayload(handle));
				}
			}
			else throw new InvalidOperationException("This Inspector target does not expose a typed object adapter.");
		}
		var scope = new InspectorTargetScope(targets, primaryIndex);
		try
		{
			for (int index = 0; index < handles.Length; index++)
				if (targets[index] is Component component)
					scope.components[handles[index]] = new(component) { Baseline = component is Behaviour ? Fields(component) : string.Empty };
			return scope;
		}
		catch { scope.Dispose(); throw; }
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
	var targetChanges = new Dictionary<string, JsonElement>[targets.Length];
	for (int index = 0; index < targetChanges.Length; index++) targetChanges[index] = new(StringComparer.Ordinal);
		bool targetChanged = false;
		foreach (var item in targetBaselines)
		{
			string fields = Fields(item.Target);
			var changes = ChangedFields(item.Baseline, fields);
			targetChanges[item.Index] = changes;
			targetChanged |= changes.Count != 0;
		}
		if (targetChanged)
		{
			if (targets.Length == 1)
			{
				NativeEditorBindings.ApplyTargetFields(JsonSerializer.Serialize(targetChanges[0]));
			}
			else
			{
				for (int index = 0; index < targetChanges.Length; index++)
				NativeEditorBindings.ApplyTargetFields(JsonSerializer.Serialize(new { Targets = targetChanges }));
			}
			for (int index = 0; index < targetBaselines.Count; index++)
			{
				var item = targetBaselines[index];
				targetBaselines[index] = (item.Index, item.Target, Fields(item.Target));
			}
		}
		foreach (var entry in new List<KeyValuePair<NativeHandle, ComponentSnapshot>>(components))
		{
			if (Array.IndexOf(targets, entry.Value.Value) >= 0) continue;
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
		foreach (object item in targets)
			if (item is ScriptableObject asset) asset.ReleaseEditorProxy();
	}
}
