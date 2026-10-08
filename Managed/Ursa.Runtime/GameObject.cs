using System;
using System.Collections.Generic;

namespace Ursa;

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
		uint count = EngineApi.GetComponentCount(handle);
		for (uint index = 0; index < count; ++index)
		{
			Component? component = Component.FromNativeHandle(EngineApi.GetComponentAt(handle, index), typeof(T));
			if (component is T found) return found;
		}
		return null;
	}

	public T[] GetComponents<T>() where T : Component
	{
		uint count = EngineApi.GetComponentCount(handle);
		var found = new List<T>();
		for (uint index = 0; index < count; ++index)
		{
			Component? component = Component.FromNativeHandle(EngineApi.GetComponentAt(handle, index), typeof(T));
			if (component is T matching) found.Add(matching);
		}
		return found.ToArray();
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
