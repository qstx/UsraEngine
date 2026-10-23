using System;
using Ursa.Rendering;

internal static class TemporaryRenderTargetScopeTests
{
	internal static void Run()
	{
		var descriptor = new RenderTextureDescriptor(32, 16);
		using (var commands = new CommandBuffer("temporary scope probe"))
		{
			var scope = commands.CreateTemporaryTargetScope();
			RenderTargetIdentifier target = scope.Allocate(descriptor);
			Expect(target.Id != 0 && target.Texture == null, "Temporary scope returned an invalid target identifier.");
			Reject<ArgumentNullException>(() => scope.Allocate(null!), "Temporary scope accepted a null descriptor.");
			Reject<ArgumentException>(() => scope.Release(default), "Temporary scope released an unowned target.");
			Reject<InvalidOperationException>(() => commands.BuildBatch(),
				"CommandBuffer delivered while a temporary target scope was active.");
			scope.Release(target);
			scope.Dispose();
			_ = commands.BuildBatch();
			Reject<ObjectDisposedException>(() => scope.Allocate(descriptor), "Disposed temporary scope allocated a target.");

			var clearedScope = commands.CreateTemporaryTargetScope();
			_ = clearedScope.Allocate(descriptor);
			commands.Clear();
			Reject<InvalidOperationException>(() => clearedScope.Allocate(descriptor),
				"Temporary scope continued recording after its CommandBuffer was cleared.");
			clearedScope.Dispose();
			using (var nextScope = commands.CreateTemporaryTargetScope())
			{
				RenderTargetIdentifier nextTarget = nextScope.Allocate(descriptor);
				nextScope.Release(nextTarget);
			}
			_ = commands.BuildBatch();
		}

		using (var commands = new CommandBuffer("temporary cleanup failure probe"))
		{
			var scope = commands.CreateTemporaryTargetScope();
			RenderTargetIdentifier externallyReleased = scope.Allocate(descriptor);
			_ = scope.Allocate(descriptor);
			commands.ReleaseTemporaryTarget(externallyReleased.Id);
			bool cleanupFailureReported = false;
			try { scope.Dispose(); }
			catch (AggregateException error)
			{
				cleanupFailureReported = error.InnerExceptions.Count == 1 &&
				                         error.InnerExceptions[0].Message.Contains("not active");
			}
			Expect(cleanupFailureReported, "Temporary scope did not report a release failure during cleanup.");
			_ = commands.BuildBatch();
		}

		var disposedCommands = new CommandBuffer("disposed scope probe");
		var disposedScope = disposedCommands.CreateTemporaryTargetScope();
		_ = disposedScope.Allocate(descriptor);
		disposedCommands.Dispose();
		disposedScope.Dispose();
	}

	private static void Reject<TException>(Action action, string message) where TException : Exception
	{
		try { action(); }
		catch (TException) { return; }
		throw new Exception(message);
	}

	private static void Expect(bool value, string message)
	{
		if (!value) throw new Exception(message);
	}
}
