using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Ursa.Bindings;

namespace Ursa.ManagedHost;

public static unsafe partial class Bootstrap
{
	// The only hand-authored binding entry: hostfxr's component entry convention.
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int InitializeBindings(nint arguments, int argumentSize)
	{
		if (arguments == 0 || argumentSize != sizeof(BindingBootstrap)) return 1;
		BindingBootstrap* request = (BindingBootstrap*)arguments;
		if (request->Version != 1 || request->Size != sizeof(BindingBootstrap)) return 2;
		request->Outcome = default;
		request->CallbackCount = 0;
		try
		{
			// Referenced framework assemblies need not otherwise have run their module initializers yet.
			RuntimeHelpers.RunModuleConstructor(typeof(Ursa.Behaviour).Module.ModuleHandle);
			RuntimeHelpers.RunModuleConstructor(typeof(Ursa.Rendering.RenderContextToken).Module.ModuleHandle);
			BindingMemory.Validate(request->Modules, request->ModuleCount, (ulong)sizeof(BindingModuleReference), 1, 0);
			BindingMemory.Validate(request->Callbacks, request->CallbackCapacity, (ulong)sizeof(BindingModuleReference), 1, 0);
			BindingModuleReference[] callbacks = BindingRegistry.DescribeCallbacks();
			if ((ulong)callbacks.Length > request->CallbackCapacity) throw new InvalidOperationException("Binding callback exchange capacity is too small.");
			BindingRegistry.Bind(new ReadOnlySpan<BindingModuleReference>(request->Modules, checked((int)request->ModuleCount)));
			callbacks.CopyTo(new Span<BindingModuleReference>(request->Callbacks, callbacks.Length));
			request->CallbackCount = (ulong)callbacks.Length;
			return 0;
		}
		catch (Exception error) { return BindingMemory.Fail(error, &request->Outcome, "Bootstrap.InitializeBindings"); }
	}
}
