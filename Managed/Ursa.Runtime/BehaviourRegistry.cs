using System;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;

namespace Ursa;

internal readonly record struct ScriptIdentity(string Id, uint Version, string SourceGuid = "", ulong LocalFileId = 0);

internal static class BehaviourRegistry
{
	[ThreadStatic] private static Dictionary<ulong, Behaviour>? ownerInstances;
	[ThreadStatic] private static Dictionary<ulong, ulong>? ownerInstanceSessions;
	private static Dictionary<ulong, Behaviour> instances => ownerInstances ??= new();
	private static Dictionary<ulong, ulong> instanceSessions => ownerInstanceSessions ??= new();
	private static long nextToken;
	[ThreadStatic] internal static ulong currentSession;
	[ThreadStatic] internal static Func<string, Type?>? typeResolver;
	[ThreadStatic] internal static Func<IEnumerable<Assembly>>? assemblyResolver;
	[ThreadStatic] internal static Func<Type, ScriptIdentity>? identityResolver;

	internal static ScriptIdentity DeclaredIdentity(Type type)
	{
		UrsaScriptTypeAttribute identity = type.GetCustomAttribute<UrsaScriptTypeAttribute>() ??
			throw new InvalidOperationException($"Script '{type.FullName}' has no identity in the session type catalog.");
		return new ScriptIdentity(identity.Id, identity.Version);
	}

	internal static ScriptIdentity IdentityFor(Type type) => identityResolver != null ? identityResolver(type) : DeclaredIdentity(type);

	internal static void ResolveSessionReferences(ulong id)
	{
		foreach (ulong token in new List<ulong>(instances.Keys))
			if (instanceSessions[token] == id) ScriptFieldSchema.ResolvePendingReferences(instances[token]);
	}

	internal static Type? ResolveType(string typeName) => typeResolver != null
		? typeResolver(typeName) : Type.GetType(typeName, throwOnError: false);

	internal static IEnumerable<Assembly> Assemblies => assemblyResolver != null
		? assemblyResolver() : AppDomain.CurrentDomain.GetAssemblies();

	internal static void ReleaseSession(ulong session)
	{
		foreach (ulong token in new List<ulong>(instances.Keys))
		{
			if (instanceSessions[token] == session)
			{
				instances.Remove(token);
				instanceSessions.Remove(token);
			}
		}
	}

	private static Behaviour RequireInstance(ulong token)
	{
		if (!instances.TryGetValue(token, out Behaviour? instance) || instanceSessions[token] != currentSession)
		{
			throw new InvalidOperationException("Managed Behaviour token belongs to a closed or different session.");
		}
		return instance;
	}

	internal static ulong Register(Behaviour behaviour)
	{
		if (behaviour == null)
		{
			throw new ArgumentNullException(nameof(behaviour));
		}
		ulong token = checked((ulong)Interlocked.Increment(ref nextToken));
		if (token == 0 || !instances.TryAdd(token, behaviour))
		{
			throw new InvalidOperationException("Managed Behaviour instance token space is exhausted.");
		}
		instanceSessions.Add(token, currentSession);
		return token;
	}

	internal static ulong Create(string typeName, GameObject owner)
	{
		Type? behaviorType = ResolveType(typeName);
		if (behaviorType == null)
		{
			int assemblySeparator = typeName.IndexOf(',');
			string qualifiedTypeName = assemblySeparator < 0 ? typeName : typeName[..assemblySeparator].Trim();
			string? assemblyName = assemblySeparator < 0 ? null :
				typeName[(assemblySeparator + 1)..].Split(',')[0].Trim();
			foreach (Assembly assembly in Assemblies)
			{
				if (assemblyName != null && !string.Equals(assembly.GetName().Name, assemblyName, StringComparison.Ordinal))
				{
					continue;
				}
				behaviorType = assembly.GetType(qualifiedTypeName, throwOnError: false, ignoreCase: false);
				if (behaviorType == null)
				{
					foreach (Type candidate in assembly.GetTypes())
					{
						if (string.Equals(candidate.Name, qualifiedTypeName, StringComparison.Ordinal))
						{
							behaviorType = candidate;
							break;
						}
					}
				}
				if (behaviorType != null)
				{
					break;
				}
			}
		}
		if (behaviorType == null)
		{
			var loadedAssemblies = new List<string>();
			foreach (Assembly assembly in Assemblies)
			{
				loadedAssemblies.Add(assembly.GetName().Name ?? "<unnamed>");
			}
			throw new InvalidOperationException(
			    $"Managed Behaviour type '{typeName}' was not found in loaded assemblies: {string.Join(", ", loadedAssemblies)}");
		}
		if (behaviorType.IsAbstract || !typeof(Behaviour).IsAssignableFrom(behaviorType))
		{
			throw new InvalidOperationException(
			    $"'{typeName}' is not a concrete Ursa Behaviour type. Base={behaviorType.BaseType?.AssemblyQualifiedName}; " +
			    $"Expected={typeof(Behaviour).AssemblyQualifiedName}.");
		}
		if (Activator.CreateInstance(behaviorType) is not Behaviour behaviour)
		{
			throw new InvalidOperationException($"Could not construct Behaviour '{typeName}'.");
		}
		behaviour.Attach(owner);
		return Register(behaviour);
	}

	internal static void BindNative(ulong token, NativeHandle component)
	{
		Behaviour behaviour = RequireInstance(token);
		if (component.kind != 2)
		{
			throw new InvalidOperationException("Managed Behaviour was bound to a non-Component handle.");
		}
		behaviour.AttachNative(behaviour.GameObject, component);
	}

	internal static void Invoke(ulong token, uint eventId)
	{
		Behaviour behaviour = RequireInstance(token);
		if (eventId == 0)
		{
			ScriptFieldSchema.ResolvePendingReferences(behaviour);
		}
		switch (eventId)
		{
			case 0: behaviour.Awake(); break;
			case 1: behaviour.OnEnable(); break;
			case 2: behaviour.Start(); break;
			case 3: behaviour.FixedUpdate(); break;
			case 4: behaviour.Update(); break;
			case 5: behaviour.LateUpdate(); break;
			case 6: behaviour.OnDisable(); break;
			case 7: behaviour.OnDestroy(); break;
			default: throw new ArgumentOutOfRangeException(nameof(eventId));
		}
	}

	internal static Behaviour FindByNativeHandle(NativeHandle handle, Type expectedType)
	{
		foreach (Behaviour behaviour in instances.Values)
		{
			if (instanceSessions[TokenFor(behaviour)] == currentSession && expectedType.IsInstanceOfType(behaviour) && SameHandle(behaviour.NativeHandle, handle))
			{
				return behaviour;
			}
		}
		throw new InvalidOperationException("Managed Behaviour reference does not resolve in this script session.");
	}

	internal static ulong TokenFor(Behaviour expectedBehaviour)
	{
		foreach (KeyValuePair<ulong, Behaviour> entry in instances)
		{
			if (ReferenceEquals(entry.Value, expectedBehaviour))
			{
				return entry.Key;
			}
		}
		throw new InvalidOperationException("Managed Behaviour instance is not registered.");
	}

	internal static void Destroy(ulong token)
	{
		RequireInstance(token);
		instanceSessions.Remove(token);
		if (!instances.Remove(token))
		{
			throw new InvalidOperationException("Managed Behaviour instance token is stale.");
		}
	}

	internal static string Serialize(ulong token)
	{
		Behaviour behaviour = RequireInstance(token);
		return ScriptFieldSchema.Serialize(behaviour);
	}

	internal static void Deserialize(ulong token, string payload)
	{
		Behaviour behaviour = RequireInstance(token);
		ScriptFieldSchema.Deserialize(behaviour, payload);
	}

	private static bool SameHandle(NativeHandle left, NativeHandle right)
	{
		return left.sessionId == right.sessionId && left.index == right.index &&
		       left.generation == right.generation && left.kind == right.kind;
	}
}
