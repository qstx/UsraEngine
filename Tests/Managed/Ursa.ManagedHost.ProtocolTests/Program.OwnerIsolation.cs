using System;
using System.Threading;
using Ursa;
using Ursa.Bindings;

internal static unsafe partial class Program
{
	private static void TestOwnerThreadIsolation()
	{
		bool textureOutsideImportRejected = false;
		try { _ = new Texture2D(1, 1); }
		catch (InvalidOperationException) { textureOutsideImportRejected = true; }
		Expect(textureOutsideImportRejected, "Texture2D candidate creation escaped its Import invocation.");
		const ulong mainSession = 50001;
		const ulong importSession = 50002;
		ScriptableObjectRegistry.SelectSession(mainSession);
		Ursa.Rendering.RenderPipelineManager.SelectSession(mainSession);
		var mainAsset = ScriptableObject.CreateInstance<PipelineProbeAsset>();
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(mainAsset);
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
		BindingModuleReference[] mainCallbacks = BindingRegistry.DescribeCallbacks();
		Exception? failure = null;
		var worker = new Thread(() =>
		{
			try
			{
				bool unboundRejected = false;
				try { ScriptableObject.CreateInstance<PipelineProbeAsset>(); }
				catch (InvalidOperationException) { unboundRejected = true; }
				Expect(unboundRejected, "An uninitialized thread could create managed assets.");
				using var runtime = InstallRuntimeBindings();
				using var rendering = InstallRenderingBindings();
				ScriptableObjectRegistry.SelectSession(importSession);
				Ursa.Rendering.RenderPipelineManager.SelectSession(importSession);
				Expect(!mainAsset.IsValid && Ursa.Rendering.RenderPipelineManager.ActiveAsset == null,
				       "Import owner observed another owner's asset or render pipeline.");
				var asset = ScriptableObject.CreateInstance<PipelineProbeAsset>();
				Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(asset);
				Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
				BindingModuleReference[] callbacks = BindingRegistry.DescribeCallbacks();
				Expect(callbacks.Length == mainCallbacks.Length, "Owner callback inventories differ.");
				for (int index = 0; index < callbacks.Length; ++index)
					Expect(callbacks[index].Context != mainCallbacks[index].Context,
					       "Managed callback registration was shared between owner threads.");
				Ursa.Rendering.RenderPipelineManager.ReleaseSession(importSession);
				Expect(ScriptableObjectRegistry.ClearSession(importSession).Count == 0 && !asset.IsValid,
				       "Import owner failed to release its managed objects.");
				ScriptableObjectRegistry.SelectSession(0);
				BindingRegistry.ShutdownThread();
				bool closedOwnerRejected = false;
				try { ScriptableObject.CreateInstance<PipelineProbeAsset>(); }
				catch (InvalidOperationException) { closedOwnerRejected = true; }
				Expect(closedOwnerRejected, "A closed owner could construct managed assets.");
				foreach (var callback in callbacks)
				{
					bool retired = false;
					try { BindingCallbackRegistration.Require(callback.Context, 0); }
					catch (ObjectDisposedException) { retired = true; }
					Expect(retired, "Closed owner callback context remained usable.");
				}
			}
			catch (Exception error) { failure = error; }
		});
		worker.Start();
		worker.Join();
		if (failure != null) throw new InvalidOperationException("Import owner isolation failed.", failure);
		Expect(mainAsset.IsValid && ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActiveAsset, mainAsset),
		       "Closing an import owner invalidated the Editor owner.");
		foreach (var callback in mainCallbacks) BindingCallbackRegistration.Require(callback.Context, 0);
		Ursa.Rendering.RenderPipelineManager.ReleaseSession(mainSession);
		Expect(ScriptableObjectRegistry.ClearSession(mainSession).Count == 0, "Editor owner cleanup failed.");
		ScriptableObjectRegistry.SelectSession(0);
		Ursa.Rendering.RenderPipelineManager.SelectSession(0);
	}
}
