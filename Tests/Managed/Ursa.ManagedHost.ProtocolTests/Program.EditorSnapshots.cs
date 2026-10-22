using System;
using Ursa;

internal static unsafe partial class Program
{
	private sealed class EditorSnapshotProbe : ScriptableObject
	{
		internal static int enables;
		internal static int destroys;
		protected override void OnEnable() => ++enables;
		protected override void OnDestroy() => ++destroys;
	}
	private static void TestEditorObjectSnapshots()
	{
		const ulong session = 50003;
		ScriptableObjectRegistry.SelectSession(session);
		var original = ScriptableObject.CreateInstance<EditorSnapshotProbe>();
		int enabled = EditorSnapshotProbe.enables;
		int destroyed = EditorSnapshotProbe.destroys;
		ScriptableObject snapshot;
		ScriptableObject explicitObject;
		using (ScriptableObjectRegistry.BeginEditorSnapshot())
		{
			Expect(!original.IsValid, "Editor object snapshot reused the original session's asset cache.");
			snapshot = ScriptableObject.CreateForLoad(typeof(EditorSnapshotProbe));
			snapshot.Activate();
			Expect(EditorSnapshotProbe.enables == enabled, "An Editor field snapshot invoked OnEnable.");
			explicitObject = ScriptableObject.CreateInstance<EditorSnapshotProbe>();
			Expect(EditorSnapshotProbe.enables == enabled + 1, "Explicit CreateInstance lost its normal initialization callback.");
		}
		Expect(original.IsValid && !snapshot.IsValid && !explicitObject.IsValid &&
		       EditorSnapshotProbe.destroys == destroyed + 1,
		       "Editor snapshot cleanup invoked user snapshot callbacks, retained scoped objects or damaged the original session.");
		Expect(ScriptableObjectRegistry.ClearSession(session).Count == 0, "Original snapshot fixture session did not close.");
		ScriptableObjectRegistry.SelectSession(0);
	}
}
