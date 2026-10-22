using System;
using System.Collections.Generic;

namespace Ursa;

[Ursa.Bindings.WireValue]
public enum PrimitiveType { Cube }

public sealed class GameObject
{
	private readonly NativeHandle handle;
	private Transform? transform;

	private GameObject(NativeHandle handle)
	{
		this.handle = handle;
	}

	internal NativeHandle Handle => handle;

	internal string PersistentId => EngineApi.GetGameObjectPersistentId(handle);

	public string Name
	{
		get => EngineApi.GetName(handle);
		set => EngineApi.SetName(handle, value ?? throw new ArgumentNullException(nameof(value)));
	}

	public bool ActiveSelf
	{
		get => EngineApi.GetActive(handle);
		set => EngineApi.SetActive(handle, value);
	}

	public int Layer
	{
		get => NativeRuntimeBindings.GetLayer(handle);
		set
		{
			if (value < 0 || value > 31) throw new ArgumentOutOfRangeException(nameof(value));
			NativeRuntimeBindings.SetLayer(handle, value);
		}
	}

	public bool ActiveInHierarchy
	{
		get
		{
			for (GameObject? current = this; current != null; current = current.Parent)
			{
				if (!current.ActiveSelf)
				{
					return false;
				}
			}
			return true;
		}
	}

	public GameObject? Parent
	{
		get
		{
			NativeHandle parent = EngineApi.GetParent(handle);
			return parent.sessionId == 0 ? null : FromHandle(parent);
		}
		set => EngineApi.SetParent(handle, value?.handle ?? default);
	}

	public void Destroy() => EngineApi.DestroyGameObject(handle);
	public GameObject[] GetChildren()
	{
		NativeHandle[] children = NativeRuntimeBindings.GetChildren(handle);
		var result = new GameObject[children.Length];
		for (int index = 0; index < children.Length; ++index) result[index] = FromHandle(children[index]);
		return result;
	}
	public T[] GetComponentsInChildren<T>(bool includeInactive = false) where T : Component
	{
		var found = new List<T>();
		void Visit(GameObject node)
		{
			if (!includeInactive && !node.ActiveInHierarchy) return;
			found.AddRange(node.GetComponents<T>());
			foreach (GameObject child in node.GetChildren()) Visit(child);
		}
		Visit(this);
		return found.ToArray();
	}

	public Transform Transform => transform ??= new Transform(handle);

	public T AddComponent<T>() where T : Component, new()
	{
		var component = new T();
		if (component is Behaviour behaviour)
		{
			behaviour.Attach(this);
			ulong token = BehaviourRegistry.Register(behaviour);
			try
			{
				NativeHandle scriptHandle = EngineApi.AddBehaviour(handle,
					typeof(T).AssemblyQualifiedName ?? throw new InvalidOperationException("Behaviour type has no assembly name."),
					token);
				behaviour.AttachNative(this, scriptHandle);
			}
			catch
			{
				BehaviourRegistry.Destroy(token);
				throw;
			}
			return component;
		}
		string typeId = Component.TypeIdFor<T>();
		component.AttachNative(this, EngineApi.AddComponent(handle, typeId));
		return component;
	}

	public T? GetComponent<T>() where T : Component
	{
		foreach (NativeHandle componentHandle in NativeRuntimeBindings.GetComponents(handle))
		{
			Component? component = Component.FromNativeHandle(componentHandle, typeof(T));
			if (component is T found) return found;
		}
		return null;
	}

	public T[] GetComponents<T>() where T : Component
	{
		var found = new List<T>();
		foreach (NativeHandle componentHandle in NativeRuntimeBindings.GetComponents(handle))
		{
			Component? component = Component.FromNativeHandle(componentHandle, typeof(T));
			if (component is T matching) found.Add(matching);
		}
		return found.ToArray();
	}

	public static GameObject CreatePrimitive(PrimitiveType type = PrimitiveType.Cube)
	{
		return FromHandle(NativeRuntimeBindings.CreatePrimitive(type));
	}

	public static GameObject Create(string name = "GameObject")
	{
		return EngineApi.CreateGameObject(name);
	}

	internal static GameObject FromHandle(NativeHandle handle)
	{
		if (handle.sessionId == 0 || handle.reserved != 0 || handle.kind != 1)
		{
			throw new InvalidOperationException("The native handle is not a valid GameObject handle.");
		}
		return new GameObject(handle);
	}

	internal static GameObject FromPersistentId(string persistentId)
	{
		return FromHandle(EngineApi.ResolveGameObjectPersistentId(persistentId));
	}
}
