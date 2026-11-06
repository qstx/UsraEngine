using System;
using System.Runtime.CompilerServices;
using System.Text;
using Ursa.Bindings;
using Ursa.Bindings.Generated;

namespace Ursa.Profiling;

/// <summary>A reusable CPU marker for synchronous code within a profiling execution context.</summary>
public readonly struct ProfilerMarker
{
#if URSA_ENABLE_PROFILER
	private readonly MarkerState? state;
#endif

	/// <summary>Declares metadata without requiring an initialized native host.</summary>
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public ProfilerMarker(string name, [CallerFilePath] string file = "",
		[CallerMemberName] string member = "", [CallerLineNumber] int line = 0)
	{
#if URSA_ENABLE_PROFILER
		state = new MarkerState(name, file, member, line);
#endif
	}

	/// <summary>Begins a synchronous CPU zone. Publish builds return an empty scope.</summary>
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public AutoScope Auto()
	{
#if URSA_ENABLE_PROFILER
		if (state != null) return state.Auto();
#endif
		return default;
	}

#if URSA_ENABLE_PROFILER
	private sealed class MarkerState
	{
		private readonly string name;
		private readonly string file;
		private readonly string member;
		private readonly int line;
		private ulong marker;
		[ThreadStatic] private static BindingSnapshot? capabilityView;
		[ThreadStatic] private static nint capabilityContext;
		[ThreadStatic] private static bool enabled;

		/// <summary>Declares metadata without requiring an initialized native host.</summary>
		internal MarkerState(string name, string file, string member, int line)
		{
			ArgumentException.ThrowIfNullOrWhiteSpace(name);
			ValidateText(name, 1024, nameof(name));
			ValidateText(file, 4096, nameof(file));
			ValidateText(member, 1024, nameof(member));
			ArgumentOutOfRangeException.ThrowIfNegative(line);
			this.name = name;
			this.file = file;
			this.member = member;
			this.line = line;
		}

		/// <summary>Begins a CPU zone; dispose it before leaving the thread or script binding context.</summary>
		public AutoScope Auto()
		{
			BindingRegistry.RequireCurrent();
			nint context = RuntimeModule.AvailableContext(1);
			if (context == 0) return default;
			BindingSnapshot view = BindingRegistry.Capture();
			if (!ReferenceEquals(capabilityView, view) || capabilityContext != context)
			{
				bool available = NativeRuntimeBindings.CpuMarkersEnabled();
				capabilityView = view;
				capabilityContext = context;
				enabled = available;
			}
			if (!enabled) return default;
			ulong identity = System.Threading.Volatile.Read(ref marker);
			if (identity == 0)
			{
				lock (this)
				{
					identity = marker;
					if (identity == 0)
					{
						identity = NativeRuntimeBindings.RegisterCpuMarker(name, file, member, line);
						System.Threading.Volatile.Write(ref marker, identity);
					}
				}
			}
			ulong token = NativeRuntimeBindings.BeginCpuMarker(identity);
			return new AutoScope(token, view, context);
		}

		private static void ValidateText(string value, int maximum, string parameter)
		{
			ArgumentNullException.ThrowIfNull(value, parameter);
			if (value.Contains('\0') || Encoding.UTF8.GetByteCount(value) > maximum)
				throw new ArgumentOutOfRangeException(parameter, $"Marker metadata must fit within {maximum} UTF-8 bytes and contain no null character.");
		}
	}
#endif

	/// <summary>A stack-owned CPU zone. Use with using; do not copy it or keep it across await/yield.</summary>
	public ref struct AutoScope
	{
#if URSA_ENABLE_PROFILER
		private ulong token;
		private readonly BindingSnapshot? view;
		private readonly nint context;
		private readonly int thread;

		internal AutoScope(ulong token, BindingSnapshot view, nint context)
		{
			this.token = token;
			this.view = view;
			this.context = context;
			thread = Environment.CurrentManagedThreadId;
		}
#endif

		/// <summary>Ends this zone once, in reverse order of creation.</summary>
		public void Dispose()
		{
#if URSA_ENABLE_PROFILER
			if (token == 0) return;
			if (thread != Environment.CurrentManagedThreadId)
				throw new InvalidOperationException("CPU scope must end on its starting thread.");
			BindingRegistry.RequireCurrent();
			if (!ReferenceEquals(view, BindingRegistry.Capture()) || RuntimeModule.AvailableContext(1) != context)
				throw new InvalidOperationException("CPU scope must end in its starting script binding context.");
			NativeRuntimeBindings.EndCpuMarker(token);
			token = 0;
#endif
		}
	}
}
