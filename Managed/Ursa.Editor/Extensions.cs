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

[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
public sealed class CustomInspectorAttribute : Attribute
{
	public CustomInspectorAttribute(string stableType) { StableType = stableType; }

	public string StableType { get; }
}

public abstract class Inspector
{
	public SerializedObject SerializedObject { get; internal set; } = null!;

	public abstract void OnInspectorGUI();

	protected bool DrawDefaultInspector() => SerializedObject.DrawDefaultInspector();
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

	// Called through reflection by the stable host; no project delegate is exported to native code.
	internal static IEditorExtensionSession Create(Assembly[] assemblies)
	{
		var registry = new ExtensionRegistry();
		try
		{
			foreach (Assembly assembly in assemblies)
			foreach (Type type in assembly.GetTypes())
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
				if (typeof(EditorWindow).IsAssignableFrom(type))
				{
					EditorWindowMenuAttribute? menu = type.GetCustomAttribute<EditorWindowMenuAttribute>();
					if (menu == null) continue;
					var window = (EditorWindow)Activator.CreateInstance(type)!;
					registry.Add(new() { Id = "window:" + type.FullName, Kind = EditorExtensionKind.Window, Path = menu.Path, Target = "", Title = window.Title }, window.OnGUI);
				}
				if (typeof(Inspector).IsAssignableFrom(type))
				{
					foreach (CustomInspectorAttribute attribute in type.GetCustomAttributes<CustomInspectorAttribute>())
					{
						var inspector = (Inspector)Activator.CreateInstance(type)!;
						registry.inspectors.Add(inspector);
						registry.Add(new() { Id = "inspector:" + attribute.StableType, Kind = EditorExtensionKind.Inspector, Path = "", Target = attribute.StableType, Title = "" },
							() => { inspector.SerializedObject = new SerializedObject(); inspector.OnInspectorGUI(); });
					}
				}
			}
			return registry;
		}
		catch { registry.Dispose(); throw; }
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

	public void Dispose()
	{
		callbacks.Clear();
		descriptors.Clear();
		inspectors.Clear();
		disabled.Clear();
	}
}
