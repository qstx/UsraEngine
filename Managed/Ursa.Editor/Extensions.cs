using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Ursa;

namespace Ursa.Editor;

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class MenuItemAttribute : Attribute
{
	public MenuItemAttribute(string path) { Path = path; }

	public string Path { get; }
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false)]
public sealed class EditorWindowMenuAttribute : Attribute
{
	public EditorWindowMenuAttribute(string path) { Path = path; }

	public string Path { get; }
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class CustomEditorAttribute : Attribute
{
	public CustomEditorAttribute(Type inspectedType, bool editorForChildClasses = false)
	{
		ArgumentNullException.ThrowIfNull(inspectedType);
		if (inspectedType.ContainsGenericParameters || !InspectorTargetMatching.Supports(inspectedType))
			throw new ArgumentException("CustomEditor target must be a closed Component, GameObject, ScriptableObject, resource or AssetImporter type.", nameof(inspectedType));
		InspectedType = inspectedType;
		EditorForChildClasses = editorForChildClasses;
	}

	public Type InspectedType { get; }
	public bool EditorForChildClasses { get; }
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
internal sealed class BuiltinEditorAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CanEditMultipleObjectsAttribute : Attribute
{
}

public abstract class Inspector
{
	public SerializedObject SerializedObject { get; internal set; } = null!;
	internal Type TargetType { get; set; } = null!;
	internal PreviewLifetime PreviewLifetime { get; } = new();
	private InspectorTargetScope? targetScope;
	public virtual object Target => (targetScope ??= InspectorTargetScope.Create(TargetType)).Target;
	public virtual object[] Targets => (targetScope ??= InspectorTargetScope.Create(TargetType)).Targets;
	internal virtual void SynchronizeTarget() => targetScope?.Synchronize();
	internal void ReleaseTarget() { targetScope?.Dispose(); targetScope = null; }

	public virtual void OnInspectorGUI() => DrawDefaultInspector();
	public virtual void OnTransformActionsGUI() { }
	public virtual void OnEnable() { }
	public virtual void OnDisable() { }
	public virtual bool HasPreviewGUI() => false;
	public virtual void OnPreviewSettings() { }
	public virtual void OnPreviewGUI(Rect rect) { }
	public virtual void OnInteractivePreviewGUI(Rect rect) => OnPreviewGUI(rect);
	public virtual string GetInfoString() => string.Empty;
	public virtual bool RequiresConstantRepaint() => false;

	protected bool DrawDefaultInspector() => SerializedObject.DrawDefaultInspectorManaged();
}

public abstract class EditorWindow
{
	public virtual string Title => GetType().Name;

	public abstract void OnGUI();
}

internal sealed class ExtensionRegistry : IEditorExtensionSession
{
	private readonly List<EditorExtensionInfo> descriptors = new();
	private readonly Dictionary<string, Action> callbacks = new(StringComparer.Ordinal);
	private readonly HashSet<string> disabled = new(StringComparer.Ordinal);
	private readonly List<Inspector> inspectors = new();
	private readonly Dictionary<string, (Type Inspector, Type Target)> inspectorTypes = new(StringComparer.Ordinal);
	private readonly Dictionary<ulong, (string Id, Inspector Value)> previews = new();
	private readonly HashSet<ulong> failedPreviews = new();
	private static SerializedObject CreateSerializedObject(Inspector inspector)
		=> inspector is AssetImporterEditor
			? new SerializedObject(Array.Empty<object>())
			: new SerializedObject(inspector.Targets);

	// Called through reflection by the stable host; no project delegate is exported to native code.
	internal static IEditorExtensionSession Create(Assembly[] assemblies)
		=> CreateForTypes(assemblies.SelectMany(assembly => assembly.GetTypes()).Distinct().ToArray());

	internal static IEditorExtensionSession CreateForTypes(Type[] types)
	{
		var registry = new ExtensionRegistry();
		try
		{
			foreach (Type type in types)
			{
				foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.DeclaredOnly))
				{
					MenuItemAttribute? menu = method.GetCustomAttribute<MenuItemAttribute>();
					if (menu == null) continue;
					if (method.ReturnType != typeof(void) || method.GetParameters().Length != 0 || method.ContainsGenericParameters)
						throw new InvalidOperationException($"Menu method '{type.FullName}.{method.Name}' must be a static void method without parameters.");
					registry.Add(new() { Id = "menu:" + menu.Path, Kind = EditorExtensionKind.Menu, Path = menu.Path, Target = "", Title = "" },
						(Action)method.CreateDelegate(typeof(Action)));
				}
				if (type.IsAbstract || type.ContainsGenericParameters) continue;
				CustomEditorAttribute? customEditor = type.GetCustomAttribute<CustomEditorAttribute>(inherit: false);
				if (customEditor != null && !typeof(Inspector).IsAssignableFrom(type))
					throw new InvalidOperationException($"CustomEditor '{type.FullName}' must derive from Inspector.");
				if (typeof(EditorWindow).IsAssignableFrom(type))
				{
					EditorWindowMenuAttribute? menu = type.GetCustomAttribute<EditorWindowMenuAttribute>();
					if (menu == null) continue;
					var window = (EditorWindow)Activator.CreateInstance(type)!;
					registry.Add(new() { Id = "window:" + type.FullName, Kind = EditorExtensionKind.Window, Path = menu.Path, Target = "", Title = window.Title }, window.OnGUI);
				}
			}
			registry.AddInspectors(types);
			return registry;
		}
		catch { registry.Dispose(); throw; }
	}

	private void AddInspectors(Type[] types)
	{
		var registrations = types.Where(type => !type.IsAbstract && !type.ContainsGenericParameters && typeof(Inspector).IsAssignableFrom(type))
			.Select(type => (Inspector: type, Declaration: type.GetCustomAttribute<CustomEditorAttribute>(inherit: false),
				Builtin: type.GetCustomAttribute<BuiltinEditorAttribute>(inherit: false) != null))
			.Where(item => item.Declaration != null).ToArray();
		var projectRegistrations = registrations.Where(item => !item.Builtin)
			.Select(item => new InspectorRegistration(item.Inspector, item.Declaration!)).ToArray();
		var builtinRegistrations = registrations.Where(item => item.Builtin)
			.Select(item => new InspectorRegistration(item.Inspector, item.Declaration!)).ToArray();
		var targets = registrations.Select(item => item.Declaration!.InspectedType)
			.Concat(types).Concat(typeof(Component).Assembly.GetTypes()).Concat(new[] { typeof(TextureImporter), typeof(ModelImporter) })
			.Where(type => !type.IsAbstract && !type.ContainsGenericParameters && InspectorTargetMatching.Supports(type))
			.Where(type => !typeof(Texture).IsAssignableFrom(type) || type == typeof(Texture2D)).Distinct();
		foreach (Type actual in targets.OrderBy(type => type.FullName, StringComparer.Ordinal))
		{
			// Project Inspectors override the editor's built-in defaults for the same target.
			Type? projectInspector = InspectorTargetMatching.Select(actual, projectRegistrations);
			Type? builtinInspector = InspectorTargetMatching.Select(actual, builtinRegistrations);
			if (builtinInspector == typeof(DefaultComponentInspector) &&
			    actual == typeof(SkinnedMeshRendererComponent))
			{
				builtinInspector = null;
			}
			Type? selected = projectInspector ?? builtinInspector;
			if (selected == null) continue;
			string target = InspectorTargetMatching.Identity(actual);
			var inspector = (Inspector)Activator.CreateInstance(selected)!;
			inspector.TargetType = actual;
			inspectors.Add(inspector);
			if (!inspectorTypes.TryAdd("inspector:" + target, (selected, actual)))
				throw new InvalidOperationException($"Duplicate editor target identity '{target}'.");
			Add(new()
			{
				Id = "inspector:" + target,
				Kind = EditorExtensionKind.Inspector,
				Path = "",
				Target = target,
				Title = "",
				CanEditMultipleObjects = selected.GetCustomAttribute<CanEditMultipleObjectsAttribute>(inherit: false) != null
			},
				() =>
				{
					inspector.SerializedObject = CreateSerializedObject(inspector);
					try { inspector.OnInspectorGUI(); inspector.SynchronizeTarget(); }
					finally { inspector.ReleaseTarget(); }
				});
			callbacks.Add("inspector-actions:" + target, () =>
			{
				inspector.SerializedObject = CreateSerializedObject(inspector);
				try { inspector.OnTransformActionsGUI(); inspector.SynchronizeTarget(); }
				finally { inspector.ReleaseTarget(); }
			});
		}
	}

	private void Add(EditorExtensionInfo descriptor, Action callback)
	{
		if (descriptor.Kind != EditorExtensionKind.Inspector && (string.IsNullOrWhiteSpace(descriptor.Path) ||
		    descriptor.Path.Split('/').Any(segment => string.IsNullOrWhiteSpace(segment))))
			throw new InvalidOperationException("Editor menu path contains an empty segment.");
		if (descriptor.Kind == EditorExtensionKind.Inspector && string.IsNullOrWhiteSpace(descriptor.Target))
			throw new InvalidOperationException("Inspector target identity is empty.");
		if (!callbacks.TryAdd(descriptor.Id, callback)) throw new InvalidOperationException($"Duplicate editor extension '{descriptor.Id}'.");
		descriptors.Add(descriptor);
	}

	public EditorExtensionInfo[] Describe() => descriptors.Where(descriptor => !disabled.Contains(descriptor.Id)).ToArray();

	public bool Invoke(string id)
	{
		if (disabled.Contains(id) || !callbacks.TryGetValue(id, out Action? callback)) return false;
		try
		{
			EditorApi.Begin();
			callback();
			return true;
		}
		catch (Exception error)
		{
			disabled.Add(id);
			Debug.LogError($"编辑器扩展 '{id}' 已停用：{error}");
			return false;
		}
		finally { EditorApi.End(); }
	}

	public EditorPreviewResult Preview(string id, ulong instance, EditorPreviewPhase phase, Rect rect)
	{
		if (instance == 0) throw new ArgumentOutOfRangeException(nameof(instance));
		if (phase != EditorPreviewPhase.Release && failedPreviews.Contains(instance))
			return new() { Diagnostic = $"Preview '{id}' is disabled for this target after an exception.", Info = string.Empty };
		try
		{
			EditorApi.Begin();
			if (phase == EditorPreviewPhase.Release)
			{
				EditorApi.EnsureRuntimeScope();
				ReleasePreview(instance);
				failedPreviews.Remove(instance);
				return new() { Success = true, Info = string.Empty, Diagnostic = string.Empty };
			}
			if (!inspectorTypes.TryGetValue(id, out var types))
				return new() { Success = true, Info = string.Empty, Diagnostic = string.Empty };
			if (!previews.TryGetValue(instance, out var entry))
			{
				var value = (Inspector)Activator.CreateInstance(types.Inspector)!;
				value.TargetType = types.Target;
				entry = (id, value);
				previews.Add(instance, entry);
				PreviewLifetime.Current = value.PreviewLifetime;
				value.SerializedObject = CreateSerializedObject(value);
				EditorApi.EnsureRuntimeScope();
				value.OnEnable();
			}
			if (entry.Id != id) throw new InvalidOperationException("Preview instance cannot change its registered target type.");
			Inspector inspector = entry.Value;
			PreviewLifetime.Current = inspector.PreviewLifetime;
			if (phase != EditorPreviewPhase.Inspector) EditorApi.EnsureRuntimeScope();
			inspector.SerializedObject = CreateSerializedObject(inspector);
			try
			{
				if (phase == EditorPreviewPhase.Inspector) { inspector.OnInspectorGUI(); inspector.SynchronizeTarget(); }
				bool hasPreview = inspector.HasPreviewGUI();
				if (hasPreview && phase == EditorPreviewPhase.Settings) inspector.OnPreviewSettings();
				if (hasPreview && phase == EditorPreviewPhase.Draw) inspector.OnInteractivePreviewGUI(rect);
				return new() { Success = true, HasPreview = hasPreview, ConstantRepaint = hasPreview && inspector.RequiresConstantRepaint(),
					Info = hasPreview ? inspector.GetInfoString() ?? string.Empty : string.Empty, Diagnostic = string.Empty };
			}
			finally { inspector.ReleaseTarget(); }
		}
		catch (Exception error)
		{
			failedPreviews.Add(instance);
			try { ReleasePreview(instance); } catch (Exception cleanup) { error = new AggregateException(error, cleanup); }
			return new() { Info = string.Empty, Diagnostic = $"Preview '{id}' ({instance}) failed: {error}" };
		}
		finally { PreviewLifetime.Current = null; EditorApi.End(); }
	}

	private void ReleasePreview(ulong instance)
	{
		if (!previews.Remove(instance, out var entry)) return;
		PreviewLifetime.Current = entry.Value.PreviewLifetime;
		try { entry.Value.OnDisable(); }
		finally { entry.Value.PreviewLifetime.Valid = false; entry.Value.ReleaseTarget(); PreviewLifetime.Current = null; }
	}

	public void Dispose()
	{
		foreach (ulong instance in previews.Keys.ToArray())
		{
			try { ReleasePreview(instance); }
			catch (Exception error) { System.Diagnostics.Trace.TraceError($"Preview cleanup failed: {error}"); }
		}
		inspectorTypes.Clear();
		failedPreviews.Clear();
		callbacks.Clear();
		descriptors.Clear();
		inspectors.Clear();
		disabled.Clear();
	}
}
