using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Ursa.Bindings;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct Utf8View
{
	public byte* Data;
	public ulong Length;
	public uint Present;
	public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct DataView
{
	public void* Data;
	public ulong Length;
	public uint Present;
	public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct BindingOutcome
{
	public Utf8View Error;
	public nint Owner;
	public delegate* unmanaged[Cdecl]<nint, void> Release;
	public ulong SessionIdentity;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct BindingModuleDescriptor
{
	public uint Version;
	public uint Size;
	public uint PointerSize;
	public uint FunctionCount;
	public nint Functions;
	public delegate* unmanaged[Cdecl]<nint, BindingOutcome*, int> ValidateContext;
	public fixed byte Hash[32];
}

[StructLayout(LayoutKind.Sequential)]
public struct BindingModuleReference
{
	public ulong Id;
	public nint Descriptor;
	public nint Context;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct BindingBootstrap
{
	public uint Version;
	public uint Size;
	public BindingModuleReference* Modules;
	public ulong ModuleCount;
	public BindingModuleReference* Callbacks;
	public ulong CallbackCapacity;
	public ulong CallbackCount;
	public BindingOutcome Outcome;
}

public sealed unsafe class BindingArena : IDisposable
{
	private readonly List<nint> allocations = new();
	private ulong allocated;
	private bool disposed;
	private static long currentAllocations;
	public const ulong MaximumBytes = 512ul * 1024 * 1024;
	public static long CurrentAllocations => Interlocked.Read(ref currentAllocations);

	public void* Allocate(ulong count, ulong stride)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		ulong bytes = checked(count * stride);
		if (bytes > MaximumBytes - allocated) throw new InvalidOperationException("Binding transfer capacity exceeded.");
		if (bytes == 0) return null;
		void* pointer = NativeMemory.AllocZeroed(checked((nuint)bytes));
		try { allocations.Add((nint)pointer); }
		catch { NativeMemory.Free(pointer); throw; }
		Interlocked.Increment(ref currentAllocations);
		allocated += bytes;
		return pointer;
	}

	public Utf8View String(string? value, bool nullable)
	{
		if (value == null)
		{
			if (!nullable) throw new ArgumentNullException(nameof(value));
			return default;
		}
		int bytes = BindingMemory.Utf8.GetByteCount(value);
		byte* destination = (byte*)Allocate((ulong)bytes, 1);
		BindingMemory.Utf8.GetBytes(value.AsSpan(), new Span<byte>(destination, bytes));
		return new() { Data = destination, Length = (ulong)bytes, Present = 1 };
	}

	public DataView Array<T, TWire>(T[]? values, Func<T, BindingArena, TWire> convert, bool nullable)
		where TWire : unmanaged
	{
		if (values == null)
		{
			if (!nullable) throw new ArgumentNullException(nameof(values));
			return default;
		}
		return Span<T, TWire>(values, convert);
	}

	public DataView Span<T, TWire>(ReadOnlySpan<T> values, Func<T, BindingArena, TWire> convert) where TWire : unmanaged
	{
		TWire* destination = (TWire*)Allocate((ulong)values.Length, (ulong)sizeof(TWire));
		for (int index = 0; index < values.Length; ++index) destination[index] = convert(values[index], this);
		return new() { Data = destination, Length = (ulong)values.Length, Present = 1 };
	}

	public void Transfer(ref BindingOutcome outcome)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		if (allocations.Count == 0) return;
		GCHandle handle = GCHandle.Alloc(this);
		outcome.Owner = GCHandle.ToIntPtr(handle);
		outcome.Release = &ReleaseTransferred;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static void ReleaseTransferred(nint value)
	{
		try
		{
			GCHandle handle = GCHandle.FromIntPtr(value);
			try { ((BindingArena)handle.Target!).Dispose(); }
			finally { handle.Free(); }
		}
		catch { /* No managed exception may cross the release callback. */ }
	}

	public void Dispose()
	{
		if (disposed) return;
		disposed = true;
		foreach (nint allocation in allocations)
		{
			NativeMemory.Free((void*)allocation);
			Interlocked.Decrement(ref currentAllocations);
		}
		allocations.Clear();
	}
}

public static unsafe class BindingMemory
{
	internal static readonly UTF8Encoding Utf8 = new(false, true);
	public static bool Bool(uint value) => value switch
	{
		0 => false,
		1 => true,
		_ => throw new InvalidOperationException("Invalid binding boolean.")
	};

	public static void Validate(void* data, ulong count, ulong stride, uint present, uint reserved = 0)
	{
		if (present > 1 || reserved != 0 || (present == 0 && (data != null || count != 0)) ||
			(count != 0 && data == null) || stride == 0 || count > BindingArena.MaximumBytes / stride || count > int.MaxValue)
			throw new InvalidOperationException("Invalid binding buffer range or presence marker.");
	}

	public static string? String(Utf8View value, bool nullable)
	{
		Validate(value.Data, value.Length, 1, value.Present, value.Reserved);
		if (value.Present == 0)
			return nullable ? null : throw new InvalidOperationException("Required binding string is null.");
		return Utf8.GetString(new ReadOnlySpan<byte>(value.Data, checked((int)value.Length)));
	}

	public static T[]? Array<TWire, T>(DataView value, Func<TWire, T> convert, bool nullable) where TWire : unmanaged
	{
		Validate(value.Data, value.Length, (ulong)sizeof(TWire), value.Present, value.Reserved);
		if (value.Present == 0)
			return nullable ? null : throw new InvalidOperationException("Required binding array is null.");
		var result = new T[checked((int)value.Length)];
		var source = (TWire*)value.Data;
		for (int index = 0; index < result.Length; ++index) result[index] = convert(source[index]);
		return result;
	}

	public static void ThrowIfFailed(int status, BindingOutcome outcome, string method)
	{
		if (status == 0) return;
		string? detail = String(outcome.Error, true);
		throw new InvalidOperationException($"Binding '{method}', session {outcome.SessionIdentity} failed ({status}): {detail ?? "no error text available"}");
	}

	public static void Release(ref BindingOutcome outcome)
	{
		BindingOutcome owned = outcome;
		outcome = default;
		if (owned.Owner != 0 && owned.Release != null) owned.Release(owned.Owner);
	}

	public static int Fail(Exception error, BindingOutcome* outcome, string method = "", nint context = 0, ulong requestedSession = 0)
	{
		if (outcome == null) return 1;
		*outcome = default;
		ulong sessionIdentity = 0;
		BindingArena? arena = null;
		try
		{
			sessionIdentity = requestedSession != 0 ? requestedSession : BindingCallbackRegistration.SessionIdentity(context);
			outcome->SessionIdentity = sessionIdentity;
			arena = new();
			outcome->Error = arena.String($"Callback '{method}', session {sessionIdentity}, {BindingCallbackRegistration.Describe(context)} failed: {error}", false);
			arena.Transfer(ref *outcome);
			return 1;
		}
		catch
		{
			arena?.Dispose();
			*outcome = default;
			outcome->SessionIdentity = sessionIdentity;
			return 2;
		}
	}

	public static void ValidateModule(nint descriptor, string hash, uint functionCount, string name)
	{
		if (descriptor == 0) throw new InvalidOperationException($"Missing binding module '{name}'.");
		var value = (BindingModuleDescriptor*)descriptor;
		if (value->Version != 1 || value->Size != sizeof(BindingModuleDescriptor) || value->PointerSize != sizeof(nint) ||
			value->FunctionCount != functionCount || (functionCount != 0 && value->Functions == 0) || value->ValidateContext == null)
			throw new InvalidOperationException($"Binding module '{name}' layout/version mismatch.");
		for (int index = 0; index < 32; ++index)
			if (value->Hash[index] != Convert.ToByte(hash.Substring(index * 2, 2), 16))
				throw new InvalidOperationException($"Binding module '{name}' contract mismatch; expected {hash}, received {Convert.ToHexStringLower(new ReadOnlySpan<byte>(value->Hash, 32))}.");
	}

	public static void ValidateContext(nint descriptor, nint context, string name)
	{
		var module = (BindingModuleDescriptor*)descriptor;
		BindingOutcome outcome = default;
		try
		{
			int status = module->ValidateContext(context, &outcome);
			ThrowIfFailed(status, outcome, name + ".context");
		}
		finally { Release(ref outcome); }
	}
}
