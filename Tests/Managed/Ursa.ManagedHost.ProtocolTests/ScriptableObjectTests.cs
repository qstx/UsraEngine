using System;
using System.Threading;
using Ursa;

internal static class ScriptableObjectTests
{
	private sealed class Probe : ScriptableObject
	{
		public int count = 7;
		public int enabledCount;
		public int disabledCount;
		public int destroyedCount;
		public bool failDisable;

		protected override void OnEnable() => ++enabledCount;

		protected override void OnDisable()
		{
			++disabledCount;
			if (failDisable) throw new InvalidOperationException("Disable probe");
		}

		protected override void OnDestroy() => ++destroyedCount;
	}

	private abstract class AbstractProbe : ScriptableObject { }

	private sealed class GenericProbe<T> : ScriptableObject { }

	private sealed class FailedProbe : ScriptableObject
	{
		protected override void OnEnable() => throw new InvalidOperationException("Enable probe");
	}

	private static void Expect(bool condition, string message)
	{
		if (!condition) throw new InvalidOperationException(message);
	}

	private static void Reject(Action action, string message)
	{
		try { action(); } catch (Exception error) when (error is ArgumentException or InvalidOperationException) { return; }
		throw new InvalidOperationException(message);
	}

	internal static void Run()
	{
		ScriptableObjectRegistry.SelectSession(901);
		Probe first = ScriptableObject.CreateInstance<Probe>();
		Probe second = ScriptableObject.CreateInstance<Probe>();
		first.count = 19;
		Expect(second.count == 7 && first.InstanceId != second.InstanceId && first.enabledCount == 1,
			"ScriptableObject instances did not retain independent fields and identity.");
		Reject(() => ScriptableObject.CreateInstance(typeof(AbstractProbe)), "Abstract asset type was accepted.");
		Reject(() => ScriptableObject.CreateInstance(typeof(GenericProbe<>)), "Open asset type was accepted.");
		Reject(() => ScriptableObject.CreateInstance(typeof(string)), "Non-asset type was accepted.");
		Reject(() => ScriptableObject.CreateInstance<FailedProbe>(), "Failed OnEnable returned an asset.");
		var reference = new AssetReference("77777777-7777-4777-8777-777777777777", 3);
		first.BindAsset(reference);
		first.BindAsset(reference);
		Expect(first.IsPersistent && first.AssetReference!.Value.LocalFileId == 3, "Asset identity was not attached.");
		Reject(() => first.BindAsset(new AssetReference("88888888-8888-4888-8888-888888888888")), "Asset identity changed.");
		ScriptableObjectRegistry.SelectSession(902);
		Expect(!first.IsValid, "Cross-session asset remained accessible.");
		Reject(() => first.RequireValid(), "Cross-session access was accepted.");
		ScriptableObjectRegistry.SelectSession(901);
		Exception? threadError = null;
		var worker = new Thread(() => { try { first.RequireValid(); } catch (Exception error) { threadError = error; } });
		worker.Start();
		worker.Join();
		Expect(threadError is InvalidOperationException, "Background-thread asset access was accepted.");
		first.Dispose();
		first.Dispose();
		Expect(!first.IsValid && first.disabledCount == 1 && first.destroyedCount == 1, "Dispose lifecycle was not idempotent.");
		Reject(() => first.RequireValid(), "Disposed asset remained accessible.");
		second.failDisable = true;
		Expect(ScriptableObjectRegistry.ClearSession(901).Count == 1 && !second.IsValid && second.destroyedCount == 1,
			"Session teardown did not finish after an asset callback error.");
		ScriptableObjectRegistry.SelectSession(0);
	}
}
