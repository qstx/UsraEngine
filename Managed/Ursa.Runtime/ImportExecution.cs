using System;
using System.Collections.Generic;
using Ursa.Bindings;

namespace Ursa;

[WireValue]
internal struct ImportBindingCapability
{
	public ulong ModuleId;
	public ulong DescriptorAddress;
	public ulong ContextToken;
}

[WireValue]
internal struct ImportScopeValue
{
	public ulong Session;
	public ulong Generation;
	public ulong Invocation;
	public uint Phase;
	public string AssetPath;
}

internal static partial class ImportExecution
{
	[ThreadStatic] private static ImportScopeValue? current;
	[ThreadStatic] private static IDisposable? capability;
	internal static bool Active => current.HasValue;
	internal static ImportScopeValue Current => current ?? throw new InvalidOperationException("No import invocation is active.");

	internal static void Begin(ulong session, ulong generation, ulong invocation, ImportBindingCapability binding)
	{
		ScriptableObjectRegistry.RequireThread();
		if (Active || session == 0 || generation == 0 || invocation == 0 || binding.ModuleId == 0 ||
			binding.DescriptorAddress == 0 || binding.ContextToken == 0)
			throw new InvalidOperationException("Invalid or reentrant import invocation.");
		capability = BindingRegistry.EnterCallScope(new BindingModuleReference
		{
			Id = binding.ModuleId, Descriptor = checked((nint)binding.DescriptorAddress), Context = checked((nint)binding.ContextToken)
		});
		try
		{
			NativeImportBindings.RequireInvocation(session, generation, invocation);
			current = NativeImportBindings.GetScope();
			NativeImportBindings.BeginInvocation();
			BeginReferences();
		}
		catch
		{
			current = null;
			ClearReferences();
			try { capability.Dispose(); }
			finally { capability = null; }
			throw;
		}
	}

	internal static void End(ulong session, ulong generation, ulong invocation)
	{
		ImportScopeValue scope = Current;
		if (scope.Session != session || scope.Generation != generation || scope.Invocation != invocation)
			throw new InvalidOperationException("Import invocation belongs to another session or generation.");
		try
		{
			var errors = new List<Exception>();
			try { ValidateReadOnlyAssets(); } catch (Exception error) { errors.Add(error); }
			errors.AddRange(ScriptableObjectRegistry.ClearSession(session));
			if (errors.Count != 0) throw new AggregateException("Import candidate cleanup failed.", errors);
		}
		finally
		{
			try { NativeImportBindings.EndInvocation(); }
			finally
			{
				ClearReferences();
				current = null;
				try { capability?.Dispose(); }
				finally { capability = null; }
			}
		}
	}
}
