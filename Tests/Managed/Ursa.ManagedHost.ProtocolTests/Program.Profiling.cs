using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Ursa.Bindings;
using Ursa.Bindings.Generated;
using Ursa.Profiling;

internal static unsafe partial class Program
{
	private static readonly ProfilerMarker preBindingMarker = new("Managed.Test.BeforeBinding");
	private static bool fakeCpuMarkersEnabled = true;
	private static int cpuCapabilityQueries;
	private static int cpuRegistrations;
	private static int cpuBegins;
	private static int cpuEnds;
	private static ulong nextCpuToken;
	[ThreadStatic] private static List<ulong>? threadCpuScopes;
	private static List<ulong> cpuScopes => threadCpuScopes ??= new();
	private static readonly List<string> cpuMetadata = new();
	private static readonly HashSet<int> cpuThreads = new();

	private static void TestCpuProfilerMarkers()
	{
#if !URSA_ENABLE_PROFILER
		long before = GC.GetAllocatedBytesForCurrentThread();
		for (int i = 0; i < 1000; ++i)
		{
			var marker = new ProfilerMarker("Publish.Noop");
			using var scope = marker.Auto();
		}
		Expect(GC.GetAllocatedBytesForCurrentThread() == before && cpuCapabilityQueries == 0 && cpuBegins == 0,
			"Compiled-out markers allocated or accessed native bindings.");
#else
		// This declaration and use precede all native bindings, and must remain usable after installation.
		using (preBindingMarker.Auto()) { }
		Expect(cpuCapabilityQueries == 0, "Unbound marker called native code.");
		using var registration = InstallRuntimeBindings();
		using (preBindingMarker.Auto()) { }
		Expect(cpuMetadata[0].Contains("Managed.Test.BeforeBinding|", StringComparison.Ordinal) &&
			cpuMetadata[0].Contains("Program.Profiling.cs", StringComparison.Ordinal), "Source metadata was lost.");
		var outer = new ProfilerMarker("Managed.Test.Outer");
		var inner = new ProfilerMarker("Managed.Test.Inner");
		using (outer.Auto())
		{
			try
			{
				using var child = inner.Auto();
				throw new ApplicationException("script failure");
			}
			catch (ApplicationException) { }
			Expect(cpuScopes.Count == 1, "Exception did not close the child scope.");
		}
		ReturnFromCpuScope(inner);
		Expect(cpuScopes.Count == 0 && cpuBegins == cpuEnds, "Return/exception left an open scope.");
		int registered = cpuRegistrations;
		int queried = cpuCapabilityQueries;
		for (int i = 0; i < 100; ++i) { using var scope = inner.Auto(); }
		Expect(cpuRegistrations == registered && cpuCapabilityQueries == queried, "Marker metadata/capability was not cached.");
		long before = GC.GetAllocatedBytesForCurrentThread();
		for (int i = 0; i < 1000; ++i) { using var scope = inner.Auto(); }
		Expect(GC.GetAllocatedBytesForCurrentThread() == before, "Warm CPU scopes allocated managed objects.");
		var parentScope = outer.Auto();
		var childScope = inner.Auto();
		bool rejected = false;
		try { parentScope.Dispose(); }
		catch (InvalidOperationException) { rejected = true; }
		Expect(rejected, "An out of order end reached the event stream.");
		childScope.Dispose();
		parentScope.Dispose();
		parentScope.Dispose();
		var changedContextScope = outer.Auto();
		using (var other = InstallRuntimeBindings())
		{
			rejected = false;
			try { changedContextScope.Dispose(); }
			catch (InvalidOperationException) { rejected = true; }
			Expect(rejected, "Scope accepted a changed native context.");
		}
		RuntimeModule.Bind(registration.Descriptor, registration.Context);
		changedContextScope.Dispose();
		BindingSnapshot original = BindingRegistry.Capture();
		var snapshotScope = outer.Auto();
		BindingRegistry.Restore(BindingRegistry.Fork());
		RuntimeModule.Bind(registration.Descriptor, registration.Context);
		rejected = false;
		try { snapshotScope.Dispose(); }
		catch (InvalidOperationException) { rejected = true; }
		Expect(rejected, "Scope accepted a different binding snapshot.");
		using (inner.Auto()) { }
		BindingRegistry.Restore(original);
		RuntimeModule.Bind(registration.Descriptor, registration.Context);
		snapshotScope.Dispose();
		Expect(cpuScopes.Count == 0, "Rejected context end damaged the active scopes.");
		BindingRegistry.Clear();
		fakeCpuMarkersEnabled = false;
		RuntimeModule.Bind(registration.Descriptor, registration.Context);
		using (inner.Auto()) { }
		queried = cpuCapabilityQueries;
		registered = cpuRegistrations;
		int began = cpuBegins;
		before = GC.GetAllocatedBytesForCurrentThread();
		for (int i = 0; i < 1000; ++i) { using var scope = inner.Auto(); }
		Expect(GC.GetAllocatedBytesForCurrentThread() == before && cpuBegins == began &&
			cpuRegistrations == registered && cpuCapabilityQueries == queried, "Disabled markers called native code or allocated.");
		fakeCpuMarkersEnabled = true;
		BindingRegistry.Clear();
		RuntimeModule.Bind(registration.Descriptor, registration.Context);
		using (preBindingMarker.Auto()) { }
		Expect(cpuBegins == began + 1 && cpuEnds == cpuBegins, "Marker failed to reinitialize in a new session.");
		BindingRegistry.Clear();
	#endif
	}

	private static void ReturnFromCpuScope(ProfilerMarker marker)
	{
		using var scope = marker.Auto();
		return;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundCpuMarkersEnabled(nint context, uint* result, BindingOutcome* outcome)
	{
		*outcome = default;
		++cpuCapabilityQueries;
		*result = fakeCpuMarkersEnabled ? 1u : 0u;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundRegisterCpuMarker(nint context, Utf8View* name, Utf8View* file, Utf8View* member,
		int line, ulong* result, BindingOutcome* outcome)
	{
		*outcome = default;
		lock (cpuMetadata)
		{
			cpuMetadata.Add($"{BindingMemory.String(*name, false)}|{BindingMemory.String(*file, false)}|{BindingMemory.String(*member, false)}|{line}");
			*result = (ulong)++cpuRegistrations;
		}
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundBeginCpuMarker(nint context, ulong marker, ulong* result, BindingOutcome* outcome)
	{
		*outcome = default;
		lock (cpuMetadata)
		{
			*result = ++nextCpuToken;
			cpuThreads.Add(Environment.CurrentManagedThreadId);
		}
		cpuScopes.Add(*result);
		System.Threading.Interlocked.Increment(ref cpuBegins);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundEndCpuMarker(nint context, ulong token, BindingOutcome* outcome)
	{
		*outcome = default;
		if (cpuScopes.Count == 0 || cpuScopes[^1] != token)
			return BindingMemory.Fail(new InvalidOperationException("Invalid CPU scope order."), outcome, "Profiling.End", context);
		cpuScopes.RemoveAt(cpuScopes.Count - 1);
		System.Threading.Interlocked.Increment(ref cpuEnds);
		return 0;
	}
}
