using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Ursa;
using Ursa.Editor;

internal static class CustomEditorRegistrationTests
{
	// No identity attribute: registration must use the current session's type catalog.
	public sealed class CatalogBehaviour : Behaviour { }
	public sealed class GenericBehaviour<T> : Behaviour { }
	public class CatalogAsset : ScriptableObject { }
	public class DerivedAsset : CatalogAsset { }
	public sealed class DeepAsset : DerivedAsset { }
	public sealed class AssetInspector : Inspector { public override void OnInspectorGUI() { } }
	public sealed class DerivedInspector : Inspector { public override void OnInspectorGUI() { } }
	public sealed class ExactInspector : Inspector { public override void OnInspectorGUI() { } }
	[EditorWindowMenu("Window/State Probe")]
	public sealed class StateProbeWindow : EditorWindow
	{
		public static int LastDrawCount;
		private int drawCount;
		public override void OnGUI() => LastDrawCount = ++drawCount;
	}

	[CustomEditor(typeof(Material))]
	public sealed class MaterialInspector : Inspector { public override void OnInspectorGUI() { } }
	[CustomEditor(typeof(GameObject))]
	public sealed class PrefabInspector : Inspector { public override void OnInspectorGUI() { } }
	[CustomEditor(typeof(TextureImporter))]
	public sealed class ImporterInspector : Inspector { public override void OnInspectorGUI() { } }
	[CustomEditor(typeof(CatalogAsset))]
	public sealed class SourceAssetInspector : Inspector { public override void OnInspectorGUI() { } }

	[CustomEditor(typeof(CatalogBehaviour))]
	[CanEditMultipleObjects]
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
		var broad = new InspectorRegistration(typeof(AssetInspector), new(typeof(CatalogAsset), true));
		var near = new InspectorRegistration(typeof(DerivedInspector), new(typeof(DerivedAsset), true));
		var exact = new InspectorRegistration(typeof(ExactInspector), new(typeof(DeepAsset)));
		foreach (var registrations in new[] { new[] { broad, near, exact }, new[] { exact, near, broad } })
			if (InspectorTargetMatching.Select(typeof(DeepAsset), registrations) != typeof(ExactInspector))
				throw new InvalidOperationException("Exact asset Inspector did not win independently of discovery order.");
		if (InspectorTargetMatching.Select(typeof(DeepAsset), new[] { broad, near }) != typeof(DerivedInspector) ||
			InspectorTargetMatching.Select(typeof(DeepAsset), new[] { new InspectorRegistration(typeof(ExactInspector), new(typeof(CatalogAsset))) }) != null)
			throw new InvalidOperationException("Child Inspector selection ignored distance or opt-in.");
		try { InspectorTargetMatching.Select(typeof(DeepAsset), new[] { broad, new InspectorRegistration(typeof(ExactInspector), new(typeof(CatalogAsset), true)) });
			throw new Exception("Ambiguous asset Inspectors were accepted."); }
		catch (InvalidOperationException) { }
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
					? new ScriptIdentity(stableId, 1) : type == typeof(CatalogAsset) ? new ScriptIdentity("test.catalog-asset", 1) : throw new InvalidOperationException("Unexpected catalog lookup.");
				using var session = Create();
				EditorExtensionInfo[] descriptors = session.Describe();
				if (descriptors.Length != 7 || !descriptors.Any(item => item.Target == stableId && item.Id == "inspector:" + stableId && item.CanEditMultipleObjects) ||
					!descriptors.Any(item => item.Target == "ursa.Camera" && item.Id == "inspector:ursa.Camera") ||
					descriptors.Single(item => item.Target == "ursa.Camera").CanEditMultipleObjects ||
					!new[] { "asset:ursa.Material", "asset:ursa.Prefab", "asset:test.catalog-asset", "importer:ursa.Texture" }.All(target => descriptors.Any(item => item.Target == target)))
					throw new InvalidOperationException("Typed Inspectors did not resolve current script and native component identities.");
				var window = descriptors.Single(item => item.Kind == EditorExtensionKind.Window);
				StateProbeWindow.LastDrawCount = 0;
				var callbacks = (Dictionary<string, Action>)session.GetType()
					.GetField("callbacks", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(session)!;
				var drawWindow = callbacks[window.Id];
				drawWindow();
				var firstWindowCount = StateProbeWindow.LastDrawCount;
				drawWindow();
				if (firstWindowCount != 1 || StateProbeWindow.LastDrawCount != 2)
					throw new InvalidOperationException($"A managed EditorWindow did not retain instance state across draws (id={window.Id}, first={firstWindowCount}, second={StateProbeWindow.LastDrawCount}).");
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
