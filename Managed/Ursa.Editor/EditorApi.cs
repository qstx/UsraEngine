using System;

namespace Ursa.Editor;

internal static class EditorApi
{
	[ThreadStatic] private static bool active;
	[ThreadStatic] private static EditorScopeValue scope;
	internal static ulong Generation => active ? scope.Generation : throw new InvalidOperationException("Editor API is available only inside a main-thread extension callback.");
	internal static ulong Revision { get { _ = Generation; return scope.Revision; } }
	internal static void Begin()
	{
		if (active) throw new InvalidOperationException("Reentrant Editor callback is not supported.");
		EditorScopeValue candidate = NativeEditorBindings.GetScope();
		if (candidate.Generation == 0 || candidate.Session == 0) throw new InvalidOperationException("Invalid Editor callback identity.");
		scope = candidate;
		active = true;
	}
	internal static void End() { active = false; scope = default; }
	internal static void RequireGeneration(ulong expected)
	{
		if (Generation != expected) throw new InvalidOperationException("Serialized editor target belongs to a previous callback.");
	}
}
