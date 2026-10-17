using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Threading;

namespace Ursa.Bindings;

public sealed unsafe class BindingCallbackRegistration : IDisposable
{
	private static readonly ConcurrentDictionary<nint, BindingCallbackRegistration> registrations = new();
	private static long nextContext;
	private readonly int ownerThread = Environment.CurrentManagedThreadId;
	private nint memory;
	private readonly ulong sessionIdentity;
	public nint Context { get; }
	public nint Descriptor => memory != 0 ? memory : throw new ObjectDisposedException(nameof(BindingCallbackRegistration));
	public nint Functions => ((BindingModuleDescriptor*)Descriptor)->Functions;

	public BindingCallbackRegistration(string hash, uint functionCount, int tableSize, ulong sessionIdentity = 0)
	{
		this.sessionIdentity = sessionIdentity;
		if (tableSize < sizeof(nint) || hash.Length != 64) throw new ArgumentException("Invalid callback module descriptor.");
		memory = (nint)NativeMemory.AllocZeroed(checked((nuint)(sizeof(BindingModuleDescriptor) + tableSize)));
		var descriptor = (BindingModuleDescriptor*)memory;
		descriptor->Version = 1;
		descriptor->Size = (uint)sizeof(BindingModuleDescriptor);
		descriptor->PointerSize = (uint)sizeof(nint);
		descriptor->FunctionCount = functionCount;
		descriptor->Functions = memory + sizeof(BindingModuleDescriptor);
		descriptor->ValidateContext = &ValidateContext;
		try
		{
			for (int index = 0; index < 32; ++index) descriptor->Hash[index] = Convert.ToByte(hash.Substring(index * 2, 2), 16);
			Context = checked((nint)Interlocked.Increment(ref nextContext));
			if (Context == 0 || !registrations.TryAdd(Context, this)) throw new InvalidOperationException("Callback context identity exhausted.");
		}
		catch { NativeMemory.Free((void*)memory); memory = 0; throw; }
	}

	public static void Require(nint context, uint policy)
	{
		if (!registrations.TryGetValue(context, out var registration) || registration.memory == 0)
			throw new ObjectDisposedException(nameof(BindingCallbackRegistration), "Callback belongs to a closed registration.");
		if (policy > 1 || (policy == 0 && registration.ownerThread != Environment.CurrentManagedThreadId))
			throw new InvalidOperationException("Callback requires its owner thread.");
	}

	public static string Describe(nint context) => registrations.TryGetValue(context, out var registration)
		? $"session {registration.sessionIdentity}, callback generation {context}"
		: $"closed callback generation {context}";

	public static ulong SessionIdentity(nint context) => registrations.TryGetValue(context, out var registration)
		? registration.sessionIdentity != 0 ? registration.sessionIdentity : BindingRegistry.CurrentSessionIdentity : 0;

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
	private static int ValidateContext(nint context, BindingOutcome* outcome)
	{
		if (outcome == null) return 1;
		*outcome = default;
		try
		{
			Require(context, 0);
			outcome->SessionIdentity = SessionIdentity(context);
			return 0;
		}
		catch (Exception error) { return BindingMemory.Fail(error, outcome, "callback.context", context); }
	}

	public void Dispose()
	{
		nint owned = Interlocked.Exchange(ref memory, 0);
		if (owned == 0) return;
		registrations.TryRemove(Context, out _);
		NativeMemory.Free((void*)owned);
	}
}
