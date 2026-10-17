using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;

namespace Ursa.Bindings;

public sealed class BindingModuleRegistration
{
	internal readonly ulong Id;
	internal readonly string Name;
	internal readonly string Hash;
	internal readonly uint FunctionCount;
	internal readonly bool Required;
	internal readonly bool CallScoped;
	internal readonly ulong[] Dependencies;
	internal readonly Action<nint, nint> Connect;
	internal readonly Action Clear;
	internal readonly Func<ulong, BindingCallbackRegistration>? Callbacks;

	public BindingModuleRegistration(ulong id, string name, string hash, uint functionCount, bool required, bool callScoped,
		ulong[] dependencies, Action<nint, nint> connect, Action clear, Func<ulong, BindingCallbackRegistration>? callbacks)
	{
		Id = id;
		Name = name;
		Hash = hash;
		FunctionCount = functionCount;
		Required = required;
		CallScoped = callScoped;
		Dependencies = (ulong[])dependencies.Clone();
		Connect = connect;
		Clear = clear;
		Callbacks = callbacks;
	}

	internal void Validate(BindingModuleReference value)
	{
		BindingMemory.ValidateModule(value.Descriptor, Hash, FunctionCount, Name);
		BindingMemory.ValidateContext(value.Descriptor, value.Context, Name);
	}
}

public sealed class BindingSnapshot
{
	internal readonly BindingModuleReference[] Entries;
	internal readonly long Epoch;
	internal readonly ulong SessionIdentity;
	internal int Active = 1;
	internal BindingSnapshot(BindingModuleReference[] entries, long epoch, ulong sessionIdentity = 0)
	{
		Entries = entries;
		Epoch = epoch;
		SessionIdentity = sessionIdentity;
	}
}

public static class BindingRegistry
{
	private static readonly object metadataGate = new();
	private static readonly Dictionary<ulong, BindingModuleRegistration> providers = new();
	private static readonly Dictionary<ulong, BindingCallbackRegistration> callbacks = new();
	private static long epoch = 1;
	[ThreadStatic] private static BindingSnapshot? current;
	[ThreadStatic] private static Dictionary<ulong, CallScope>? scopes;

	public static void Register(BindingModuleRegistration module)
	{
		lock (metadataGate)
		{
			if (providers.TryGetValue(module.Id, out var previous))
			{
				if (previous.Name == module.Name && previous.Hash == module.Hash && previous.Connect.Method == module.Connect.Method)
					return;
				throw new InvalidOperationException($"Duplicate binding module '{module.Name}' ({module.Id:x16}).");
			}
			if (module.Id == 0 || module.Hash.Length != 64 || (module.Required && module.CallScoped))
				throw new InvalidOperationException($"Invalid binding module '{module.Name}' policy.");
			providers.Add(module.Id, module);
		}
	}

	private static BindingModuleRegistration[] Providers()
	{
		lock (metadataGate) return providers.Values.OrderBy(p => p.Id).ToArray();
	}

	private static BindingModuleRegistration Provider(ulong id)
	{
		lock (metadataGate)
			return providers.TryGetValue(id, out var provider) ? provider :
				throw new InvalidOperationException($"Binding module {id:x16} is not registered by the managed framework.");
	}

	internal static void ValidateRequirements(BindingRequirement[] requirements, bool editor)
	{
		var expected = Providers().Where(module => module.Required || (editor && module.CallScoped)).ToDictionary(module => module.Id);
		if (requirements.Length != expected.Count) throw new InvalidOperationException("Script binding requirements are incomplete; rebuild the project content.");
		var seen = new HashSet<ulong>();
		foreach (BindingRequirement required in requirements)
		{
			if (!seen.Add(required.Id) || !expected.TryGetValue(required.Id, out var module) || required.Name != module.Name ||
				required.Hash != module.Hash || required.FunctionCount != module.FunctionCount || required.CallScoped != module.CallScoped)
				throw new InvalidOperationException($"Script binding module '{required.Name}' differs from this Engine; rebuild the project content.");
		}
	}

	public static BindingSnapshot Capture() => current ??= new BindingSnapshot(Array.Empty<BindingModuleReference>(), Interlocked.Read(ref epoch));

	public static ulong CurrentSessionIdentity => current?.SessionIdentity ?? 0;

	public static BindingSnapshot Fork(ulong sessionIdentity = 0)
	{
		RequireCurrent();
		BindingSnapshot source = Capture();
		return new BindingSnapshot((BindingModuleReference[])source.Entries.Clone(), source.Epoch,
			sessionIdentity != 0 ? sessionIdentity : source.SessionIdentity);
	}

	public static void RequireCurrent()
	{
		if (current != null && (current.Epoch != Interlocked.Read(ref epoch) || Volatile.Read(ref current.Active) == 0))
		{
			Clear();
			throw new ObjectDisposedException(nameof(BindingSnapshot), "Binding view belongs to a closed host or script session.");
		}
	}

	public static void Bind(ReadOnlySpan<BindingModuleReference> references)
	{
		BindingModuleReference[] entries = references.ToArray();
		var incoming = new Dictionary<ulong, BindingModuleReference>();
		foreach (BindingModuleReference entry in entries)
		{
			BindingModuleRegistration provider = Provider(entry.Id);
			if (provider.CallScoped) throw new InvalidOperationException($"Module '{provider.Name}' requires a call scope.");
			if (!incoming.TryAdd(entry.Id, entry)) throw new InvalidOperationException($"Duplicate binding module '{provider.Name}' in an installation.");
			provider.Validate(entry);
		}
		foreach (BindingModuleRegistration provider in Providers())
		{
			if (provider.Required && !incoming.ContainsKey(provider.Id))
				throw new InvalidOperationException($"Missing required binding module '{provider.Name}'.");
			if (!incoming.ContainsKey(provider.Id)) continue;
			foreach (ulong dependency in provider.Dependencies)
				if (!incoming.ContainsKey(dependency))
					throw new InvalidOperationException($"Module '{provider.Name}' is missing dependency '{Provider(dependency).Name}'.");
		}
		Replace(new BindingSnapshot(entries, Interlocked.Read(ref epoch)));
	}

	private static void Install(BindingSnapshot snapshot)
	{
		if (snapshot.Epoch != Interlocked.Read(ref epoch) || Volatile.Read(ref snapshot.Active) == 0)
			throw new ObjectDisposedException(nameof(BindingSnapshot), "Binding view belongs to a closed host or script session.");
		foreach (BindingModuleRegistration provider in Providers()) provider.Clear();
		scopes?.Clear();
		current = null;
		try
		{
			foreach (BindingModuleReference entry in snapshot.Entries) Provider(entry.Id).Connect(entry.Descriptor, entry.Context);
			current = snapshot;
		}
		catch { Clear(); throw; }
	}

	private static void Replace(BindingSnapshot snapshot)
	{
		BindingSnapshot previous = Capture();
		try { Install(snapshot); }
		catch
		{
			try { Install(previous); }
			catch { Clear(); }
			throw;
		}
	}

	public static void Restore(BindingSnapshot snapshot)
	{
		ArgumentNullException.ThrowIfNull(snapshot);
		if (snapshot.Epoch != Interlocked.Read(ref epoch) || Volatile.Read(ref snapshot.Active) == 0)
			throw new ObjectDisposedException(nameof(BindingSnapshot), "Binding view belongs to a closed host or script session.");
		// Validate every native lease before replacing any current module view.
		foreach (BindingModuleReference entry in snapshot.Entries) Provider(entry.Id).Validate(entry);
		Replace(snapshot);
	}

	public static void Invalidate(BindingSnapshot snapshot)
	{
		ArgumentNullException.ThrowIfNull(snapshot);
		Interlocked.Exchange(ref snapshot.Active, 0);
		if (ReferenceEquals(current, snapshot)) Clear();
	}

	public static void Clear()
	{
		foreach (BindingModuleRegistration provider in Providers()) provider.Clear();
		scopes?.Clear();
		current = null;
	}

	public static IDisposable EnterCallScope(BindingModuleReference reference)
	{
		BindingModuleRegistration provider = Provider(reference.Id);
		if (!provider.CallScoped) throw new InvalidOperationException($"Module '{provider.Name}' is session scoped.");
		BindingSnapshot baseView = Capture();
		if (baseView.Epoch != Interlocked.Read(ref epoch) || Volatile.Read(ref baseView.Active) == 0)
			throw new ObjectDisposedException(nameof(BindingSnapshot));
		foreach (ulong dependency in provider.Dependencies)
			if (!baseView.Entries.Any(e => e.Id == dependency))
				throw new InvalidOperationException($"Call-scoped module '{provider.Name}' is missing dependency '{Provider(dependency).Name}'.");
		provider.Validate(reference);
		scopes ??= new();
		scopes.TryGetValue(reference.Id, out CallScope? previous);
		provider.Connect(reference.Descriptor, reference.Context);
		var scope = new CallScope(provider, reference, baseView, previous);
		scopes[reference.Id] = scope;
		return scope;
	}

	private sealed class CallScope(BindingModuleRegistration provider, BindingModuleReference reference, BindingSnapshot baseView,
		CallScope? previous) : IDisposable
	{
		private readonly int ownerThread = Environment.CurrentManagedThreadId;
		private bool disposed;
		internal BindingModuleReference Reference => reference;
		public void Dispose()
		{
			if (disposed) return;
			if (ownerThread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Binding call scope requires its owner thread.");
			if (scopes == null || !scopes.TryGetValue(reference.Id, out var top) || !ReferenceEquals(top, this))
			{
				if (!ReferenceEquals(current, baseView)) { disposed = true; return; }
				throw new InvalidOperationException("Binding call scopes must close in reverse order.");
			}
			disposed = true;
			if (!ReferenceEquals(current, baseView) || previous == null)
			{
				provider.Clear();
				scopes.Remove(reference.Id);
			}
			else
			{
				try { provider.Connect(previous.Reference.Descriptor, previous.Reference.Context); scopes[reference.Id] = previous; }
				catch { provider.Clear(); scopes.Remove(reference.Id); throw; }
			}
		}
	}

	public static BindingModuleReference[] DescribeCallbacks()
	{
		lock (metadataGate)
		{
			var created = new List<ulong>();
			try
			{
				var result = new List<BindingModuleReference>();
				foreach (BindingModuleRegistration provider in providers.Values.OrderBy(p => p.Id))
				{
					if (provider.Callbacks == null || provider.CallScoped) continue;
					if (!callbacks.TryGetValue(provider.Id, out var registration))
					{
						registration = provider.Callbacks(0);
						callbacks.Add(provider.Id, registration);
						created.Add(provider.Id);
					}
					result.Add(new() { Id = provider.Id, Descriptor = registration.Descriptor, Context = registration.Context });
				}
				return result.ToArray();
			}
			catch
			{
				foreach (ulong id in created) { callbacks[id].Dispose(); callbacks.Remove(id); }
				throw;
			}
		}
	}

	public static void Shutdown()
	{
		Clear();
		Interlocked.Increment(ref epoch);
		lock (metadataGate)
		{
			foreach (var registration in callbacks.Values) registration.Dispose();
			callbacks.Clear();
		}
	}
}
