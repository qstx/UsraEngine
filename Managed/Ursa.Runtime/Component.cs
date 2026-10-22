using System;
using System.Reflection;

namespace Ursa;

public abstract class Component
{
	[ThreadStatic] internal static Func<NativeHandle, Type, Component?>? EditorFactory;
	private GameObject? gameObject;
	private NativeHandle handle;
	private bool nativeBound;
	private Action? editorValidation;
	internal void RequireSerializedAccess() => editorValidation?.Invoke();
	internal static Component CreateEditorProxy(Type type, NativeHandle handle, Action requireValid)
	{
		if (Activator.CreateInstance(type) is not Component component)
			throw new InvalidOperationException("Cannot construct the typed Editor component target.");
		component.AttachNative(GameObject.FromHandle(EngineApi.GetComponentOwner(handle)), handle);
		component.editorValidation = requireValid;
		return component;
	}

	public GameObject GameObject => gameObject ?? throw new InvalidOperationException("Component is not attached.");

	public Transform Transform => GameObject.Transform;

	public bool Enabled
	{
		get => EngineApi.GetComponentEnabled(RequireNativeHandle());
		set => EngineApi.SetComponentEnabled(RequireNativeHandle(), value);
	}

	public bool IsActiveAndEnabled => EngineApi.IsComponentActive(RequireNativeHandle());

	public string TypeId => EngineApi.GetComponentType(RequireNativeHandle());

	internal NativeHandle NativeHandle => RequireNativeHandle();

	internal string PersistentId => EngineApi.GetComponentPersistentId(RequireNativeHandle());

	public void Remove()
	{
		EngineApi.RemoveComponent(RequireNativeHandle());
	}

	internal void Attach(GameObject owner)
	{
		if (gameObject != null && !ReferenceEquals(gameObject, owner))
		{
			throw new InvalidOperationException("A component cannot be attached to two GameObjects.");
		}
		gameObject = owner;
	}

	internal static string TypeIdFor<T>() where T : Component
	{
		return TypeIdFor(typeof(T));
	}

	internal static string TypeIdFor(Type type)
	{
		if (typeof(Behaviour).IsAssignableFrom(type))
		{
			return type.AssemblyQualifiedName ??
			       throw new InvalidOperationException($"{type.FullName} has no managed type name.");
		}
		NativeComponentTypeAttribute? attribute = type.GetCustomAttribute<NativeComponentTypeAttribute>();
		return attribute?.StableTypeId ??
		       throw new InvalidOperationException($"{type.FullName} has no native component type ID.");
	}

	internal static Component FromPersistentId(string persistentId, Type expectedType)
	{
		if (!typeof(Component).IsAssignableFrom(expectedType))
		{
			throw new ArgumentException("Component reference type must derive from Component.", nameof(expectedType));
		}
		NativeHandle componentHandle = EngineApi.ResolveComponentPersistentId(persistentId);
		return FromNativeHandle(componentHandle, expectedType) ??
			throw new InvalidOperationException($"Persistent Component '{persistentId}' does not match expected type '{expectedType.FullName}'.");
	}

	internal static Component? FromNativeHandle(NativeHandle componentHandle, Type expectedType)
	{
		string typeId = EngineApi.GetComponentType(componentHandle);
		Type? actualType = FindConcreteType(typeId);
		if (actualType == null || !expectedType.IsAssignableFrom(actualType)) return null;
		if (typeof(Behaviour).IsAssignableFrom(actualType))
			return EditorFactory?.Invoke(componentHandle, actualType) ?? BehaviourRegistry.FindByNativeHandle(componentHandle, actualType);
		NativeHandle ownerHandle = EngineApi.GetComponentOwner(componentHandle);
		if (Activator.CreateInstance(actualType) is not Component component)
			throw new InvalidOperationException($"Could not construct Component wrapper '{actualType.FullName}'.");
		component.AttachNative(GameObject.FromHandle(ownerHandle), componentHandle);
		return component;
	}

	private static Type? FindConcreteType(string typeId)
	{
		Type? managedType = BehaviourRegistry.ResolveType(typeId);
		if (managedType != null && typeof(Component).IsAssignableFrom(managedType))
		{
			return managedType;
		}
		foreach (Assembly assembly in BehaviourRegistry.Assemblies)
		{
			try
			{
				foreach (Type candidate in assembly.GetTypes())
				{
					if (typeof(Component).IsAssignableFrom(candidate) && !candidate.IsAbstract &&
					    candidate.GetCustomAttribute<NativeComponentTypeAttribute>()?.StableTypeId == typeId)
					{
						return candidate;
					}
				}
			}
			catch (ReflectionTypeLoadException)
			{
				// One unrelated plugin type does not prevent resolving this Component wrapper.
			}
		}
		return null;
	}

	internal void AttachNative(GameObject owner, NativeHandle componentHandle)
	{
		Attach(owner);
		handle = componentHandle;
		nativeBound = true;
	}

	private NativeHandle RequireNativeHandle()
	{
		editorValidation?.Invoke();
		if (!nativeBound)
		{
			throw new InvalidOperationException("This Component is not attached to a native Scene instance.");
		}
		return handle;
	}
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class NativeComponentTypeAttribute : Attribute
{
	public NativeComponentTypeAttribute(string typeId)
	{
		StableTypeId = string.IsNullOrWhiteSpace(typeId)
		                   ? throw new ArgumentException("Type ID is required.", nameof(typeId))
		                   : typeId;
	}

	public string StableTypeId { get; }
}
