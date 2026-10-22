using System;
using Ursa.Bindings;

namespace Ursa.Editor;

internal static class EditorApi
{
	[ThreadStatic] private static bool active;
	[ThreadStatic] private static EditorScopeValue scope;
	[ThreadStatic] private static IDisposable? runtimeScope;
	[ThreadStatic] private static IDisposable? objects;
	[ThreadStatic] private static Func<NativeHandle, Type, Component?>? previousComponentFactory;
	[ThreadStatic] private static Func<int, int, bool, Texture2D.EditorPixels>? previousTextureFactory;
	internal static ulong Generation => active ? scope.Generation : throw new InvalidOperationException("Editor API is available only inside a main-thread extension callback.");
	internal static ulong Revision { get { _ = Generation; return scope.Revision; } }
	internal static void Begin()
	{
		if (active) throw new InvalidOperationException("Reentrant Editor callback is not supported.");
		EditorScopeValue candidate = NativeEditorBindings.GetScope();
		if (candidate.Generation == 0 || candidate.Session == 0) throw new InvalidOperationException("Invalid Editor callback identity.");
		scope = candidate;
		active = true;
		previousTextureFactory = Texture2D.EditorFactory;
		Texture2D.EditorFactory = PreviewLifetime.CreateTexture;
	}
	internal static void EnsureRuntimeScope()
	{
		_ = Generation;
		if (runtimeScope != null) return;
		var capability = NativeEditorBindings.GetRuntimeCapability();
		runtimeScope = BindingRegistry.EnterSessionScope(new BindingModuleReference {
			Id = capability.ModuleId, Descriptor = checked((nint)capability.DescriptorAddress), Context = checked((nint)capability.ContextToken)
		});
		try
		{
			objects = ScriptableObjectRegistry.BeginEditorSnapshot();
			previousComponentFactory = Component.EditorFactory;
			Component.EditorFactory = InspectorTargetScope.WrapComponent;
		}
		catch { runtimeScope.Dispose(); runtimeScope = null; throw; }
	}
	internal static void End()
	{
		try { objects?.Dispose(); }
		finally
		{
			Component.EditorFactory = previousComponentFactory;
			previousComponentFactory = null;
			objects = null;
			Texture2D.EditorFactory = previousTextureFactory;
			previousTextureFactory = null;
			try { runtimeScope?.Dispose(); }
			finally { runtimeScope = null; active = false; scope = default; }
		}
	}
	internal static void RequireGeneration(ulong expected)
	{
		if (Generation != expected) throw new InvalidOperationException("Serialized editor target belongs to a previous callback.");
	}
}
