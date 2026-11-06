using System;
using System.Diagnostics;
using System.Reflection;
using Ursa;

internal static class GuiDebuggerBoundaryTests
{
	private sealed class ExitProbe : Behaviour
	{
		internal int callbacks;
		public override void OnGUI()
		{
			callbacks++;
			throw new ExitGUIException();
		}
	}

	private sealed class FailureProbe : Behaviour
	{
		internal readonly InvalidOperationException failure = new("Real OnGUI failure");
		public override void OnGUI() => throw failure;
	}

	internal static void Run()
	{
		MethodInfo boundary = typeof(BehaviourRegistry).GetMethod("InvokeGui", BindingFlags.Static | BindingFlags.NonPublic)!;
		if (boundary.GetCustomAttribute<DebuggerDisableUserUnhandledExceptionsAttribute>() == null ||
		    (boundary.GetMethodImplementationFlags() & MethodImplAttributes.NoInlining) == 0)
			throw new InvalidOperationException("ExitGUI lost its debugger-aware, non-inlined catch boundary.");

		var exit = new ExitProbe();
		BehaviourRegistry.InvokeGui(exit);
		BehaviourRegistry.InvokeGui(exit);
		if (exit.callbacks != 2)
			throw new InvalidOperationException("Normal GUI exit prevented subsequent callbacks.");

		var failed = new FailureProbe();
		try
		{
			BehaviourRegistry.InvokeGui(failed);
			throw new Exception("The GUI exit boundary swallowed a real script failure.");
		}
		catch (InvalidOperationException exception) when (ReferenceEquals(exception, failed.failure)) { }
		BehaviourRegistry.InvokeGui(exit);
		if (exit.callbacks != 3)
			throw new InvalidOperationException("A real failure left the GUI exit boundary unusable.");
	}
}
