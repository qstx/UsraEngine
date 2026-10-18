using System;
using System.Linq;
using System.Reflection;
using Ursa;
using Ursa.Editor;

internal static class CustomEditorRegistrationTests
{
	// No identity attribute: registration must use the current session's type catalog.
	public sealed class CatalogBehaviour : Behaviour { }
	public sealed class GenericBehaviour<T> : Behaviour { }

	[CustomEditor(typeof(CatalogBehaviour))]
	public sealed class BehaviourInspector : Inspector
	{
		public override void OnInspectorGUI() { }
	}

	[CustomEditor(typeof(CameraComponent))]
	public sealed class NativeInspector : Inspector
	{
		public override void OnInspectorGUI() { }
	}

	internal static void Run()
	{
		ExpectRejected(() => new CustomEditorAttribute(null!), "Null CustomEditor target was accepted.");
		ExpectRejected(() => new CustomEditorAttribute(typeof(string)), "Non-component CustomEditor target was accepted.");
		ExpectRejected(() => new CustomEditorAttribute(typeof(GenericBehaviour<>)), "Open generic CustomEditor target was accepted.");
		if (typeof(Inspector).Assembly.GetType("Ursa.Editor.CustomInspectorAttribute") != null)
			throw new InvalidOperationException("The removed string-based Inspector attribute is still exposed.");

		MethodInfo factory = typeof(Inspector).Assembly.GetType("Ursa.Editor.ExtensionRegistry", throwOnError: true)!
			.GetMethod("Create", BindingFlags.Static | BindingFlags.NonPublic)!;
		IEditorExtensionSession Create() => (IEditorExtensionSession)factory.Invoke(null,
			new object[] { new[] { typeof(CustomEditorRegistrationTests).Assembly } })!;
		var previousResolver = BehaviourRegistry.identityResolver;
		try
		{
			foreach (string stableId in new[] { "test.catalog-first", "test.catalog-reloaded" })
			{
				BehaviourRegistry.identityResolver = type => type == typeof(CatalogBehaviour)
					? new ScriptIdentity(stableId, 1) : throw new InvalidOperationException("Unexpected catalog lookup.");
				using var session = Create();
				EditorExtensionInfo[] descriptors = session.Describe();
				if (descriptors.Length != 2 || !descriptors.Any(item => item.Target == stableId && item.Id == "inspector:" + stableId) ||
					!descriptors.Any(item => item.Target == "ursa.Camera" && item.Id == "inspector:ursa.Camera"))
					throw new InvalidOperationException("Typed Inspectors did not resolve current script and native component identities.");
			}

			BehaviourRegistry.identityResolver = _ => new ScriptIdentity("ursa.Camera", 1);
			ExpectRegistrationRejected(Create, "Two typed Inspectors resolved to the same target without a diagnostic.");
			BehaviourRegistry.identityResolver = _ => throw new InvalidOperationException("Missing script identity.");
			ExpectRegistrationRejected(Create, "A script absent from the type catalog registered an Inspector.");
		}
		finally { BehaviourRegistry.identityResolver = previousResolver; }
	}

	private static void ExpectRejected(Action action, string message)
	{
		try { action(); }
		catch (ArgumentException) { return; }
		throw new InvalidOperationException(message);
	}

	private static void ExpectRegistrationRejected(Func<IEditorExtensionSession> create, string message)
	{
		try { using var session = create(); }
		catch (TargetInvocationException error) when (error.InnerException is InvalidOperationException) { return; }
		throw new InvalidOperationException(message);
	}
}
