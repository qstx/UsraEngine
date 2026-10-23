using System;
using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;

internal static class UniversalPipelineFeatureTests
{
	private sealed class FeatureAsset : ScriptableRendererFeature
	{
		internal Func<RendererFeature> Factory = () => new RuntimeProbe();
		protected override RendererFeature CreateFeature() => Factory();
	}

	private sealed class RuntimeProbe : RendererFeature
	{
		private readonly Action? onDispose;
		internal RuntimeProbe(Action? dispose = null) => onDispose = dispose;
		protected override void Dispose(bool disposing) => onDispose?.Invoke();
	}

	internal static void Run()
	{
		const ulong session = 915;
		ScriptableObjectRegistry.SelectSession(session);
		try
		{
			var created = new List<RuntimeProbe>();
			int disposed = 0;
			FeatureAsset CreateFeature(string guid, ulong localId)
			{
				var feature = ScriptableObject.CreateInstance<FeatureAsset>();
				feature.BindAsset(new AssetReference(guid, localId));
				feature.Factory = () =>
				{
					var runtime = new RuntimeProbe(() => ++disposed);
					created.Add(runtime);
					return runtime;
				};
				return feature;
			}

			UniversalRendererData CreateRendererData(params ScriptableRendererFeature[] features)
			{
				var data = ScriptableObject.CreateInstance<UniversalRendererData>();
				data.Features = features;
				return data;
			}

			UniversalRenderPipelineAsset CreatePipelineAsset(UniversalRendererData data)
			{
				var asset = ScriptableObject.CreateInstance<UniversalRenderPipelineAsset>();
				asset.RendererData = data;
				asset.MainLightShadowsEnabled = false;
				return asset;
			}

			var first = CreateFeature("91500000-0000-4000-8000-000000000001", 1);
			var second = CreateFeature("91500000-0000-4000-8000-000000000002", 2);
			var disabled = CreateFeature("91500000-0000-4000-8000-000000000003", 3);
			disabled.Enabled = false;
			var data = CreateRendererData(first, second, disabled);
			var asset = CreatePipelineAsset(data);
			RenderPipelineManager.Reset();
			RenderPipelineManager.SetPipelineAsset(asset);
			RenderPipelineManager.ApplyPendingChanges();
			Expect(created.Count == 2 && !ReferenceEquals(created[0], created[1]),
			       "Universal pipeline did not create independent runtime instances for enabled feature assets.");
			RenderPipelineManager.Reset();
			Expect(disposed == 2 && created.TrueForAll(feature => feature.IsDisposed),
			       "Universal pipeline disposal did not dispose every runtime feature instance.");

			created.Clear();
			var duplicateData = CreateRendererData(first, first);
			var duplicateAsset = CreatePipelineAsset(duplicateData);
			RenderPipelineManager.SetPipelineAsset(duplicateAsset);
			bool duplicateRejected = false;
			try { RenderPipelineManager.ApplyPendingChanges(); }
			catch (InvalidOperationException error) { duplicateRejected = error.InnerException?.Message.Contains("more than once") == true; }
			Expect(duplicateRejected && created.Count == 1 && created[0].IsDisposed,
			       "Duplicate feature references were not rejected with cleanup of already-created instances.");
			RenderPipelineManager.Reset();

			var nullData = CreateRendererData(new ScriptableRendererFeature[] { null! });
			var nullAsset = CreatePipelineAsset(nullData);
			RenderPipelineManager.SetPipelineAsset(nullAsset);
			bool nullRejected = false;
			try { RenderPipelineManager.ApplyPendingChanges(); }
			catch (InvalidOperationException error) { nullRejected = error.InnerException?.Message.Contains("empty reference") == true; }
			Expect(nullRejected, "A null feature reference was accepted.");
			RenderPipelineManager.Reset();

			created.Clear();
			var failing = CreateFeature("91500000-0000-4000-8000-000000000004", 4);
			failing.Factory = () => throw new InvalidOperationException("feature factory probe");
			var failureData = CreateRendererData(first, failing);
			var failureAsset = CreatePipelineAsset(failureData);
			RenderPipelineManager.SetPipelineAsset(failureAsset);
			bool failureCleanedUp = false;
			try { RenderPipelineManager.ApplyPendingChanges(); }
			catch (InvalidOperationException error)
			{
				failureCleanedUp = error.InnerException?.Message.Contains("feature factory probe") == true &&
				                   created.Count == 1 && created[0].IsDisposed;
			}
			Expect(failureCleanedUp, "Feature factory failure did not dispose earlier runtime feature instances.");
			RenderPipelineManager.Reset();

			int disposeCallbacks = 0;
			var throwingDispose = CreateFeature("91500000-0000-4000-8000-000000000005", 5);
			throwingDispose.Factory = () => new RuntimeProbe(() =>
			{
				++disposeCallbacks;
				throw new InvalidOperationException("feature dispose probe");
			});
			var followingDispose = CreateFeature("91500000-0000-4000-8000-000000000006", 6);
			followingDispose.Factory = () => new RuntimeProbe(() => ++disposeCallbacks);
			var cleanupAsset = CreatePipelineAsset(CreateRendererData(throwingDispose, followingDispose));
			RenderPipelineManager.SetPipelineAsset(cleanupAsset);
			RenderPipelineManager.ApplyPendingChanges();
			bool cleanupContinued = false;
			try { RenderPipelineManager.Reset(); }
			catch (AggregateException error)
			{
				bool containsDisposeError = false;
				foreach (Exception inner in error.Flatten().InnerExceptions)
					containsDisposeError |= inner.Message.Contains("feature dispose probe");
				cleanupContinued = containsDisposeError && disposeCallbacks == 2;
			}
			Expect(cleanupContinued, "One throwing feature prevented later runtime feature instances from being disposed.");

			var emptyData = CreateRendererData();
			var emptyAsset = CreatePipelineAsset(emptyData);
			RenderPipelineManager.SetPipelineAsset(emptyAsset);
			RenderPipelineManager.ApplyPendingChanges();
			Expect(RenderPipelineManager.ActivePipeline is UniversalRenderPipeline,
			       "Universal pipeline rejected an empty renderer feature list.");
			RenderPipelineManager.Reset();

			int revisionCreates = 0;
			var revisionFeature = CreateFeature("91500000-0000-4000-8000-000000000007", 7);
			revisionFeature.Factory = () => { ++revisionCreates; return new RuntimeProbe(); };
			var revisionData = CreateRendererData(revisionFeature);
			var revisionAsset = CreatePipelineAsset(revisionData);
			RenderPipelineManager.SetPipelineAsset(revisionAsset);
			RenderPipelineManager.ApplyPendingChanges();
			RenderPipeline initialRevisionPipeline = RenderPipelineManager.ActivePipeline!;
			Expect(revisionCreates == 1, "Initial revision pipeline did not create its feature instance.");

			var unrelatedData = CreateRendererData();
			unrelatedData.ValidateFields();
			RenderPipelineManager.ApplyPendingChanges();
			Expect(ReferenceEquals(RenderPipelineManager.ActivePipeline, initialRevisionPipeline) && revisionCreates == 1,
			       "An unrelated renderer asset change rebuilt the active pipeline.");

			revisionFeature.ValidateFields();
			RenderPipelineManager.ApplyPendingChanges();
			RenderPipeline dependencyRevisionPipeline = RenderPipelineManager.ActivePipeline!;
			Expect(!ReferenceEquals(dependencyRevisionPipeline, initialRevisionPipeline) && initialRevisionPipeline.IsDisposed &&
			       revisionCreates == 2,
			       "A changed feature dependency did not rebuild the pipeline at the render boundary.");

			revisionFeature.Factory = () => throw new InvalidOperationException("revision factory failure probe");
			revisionFeature.ValidateFields();
			bool revisionFailurePreserved = false;
			try { RenderPipelineManager.ApplyPendingChanges(); }
			catch (InvalidOperationException error)
			{
				revisionFailurePreserved = error.InnerException?.Message.Contains("revision factory failure probe") == true &&
				                           ReferenceEquals(RenderPipelineManager.ActivePipeline, dependencyRevisionPipeline) &&
				                           !dependencyRevisionPipeline.IsDisposed;
			}
			Expect(revisionFailurePreserved,
			       "A failed dependency candidate replaced or disposed the last valid active pipeline.");
			RenderPipelineManager.Reset();

			foreach (ScriptableObject instance in new ScriptableObject[] { first, second, disabled, data, asset,
			             duplicateData, duplicateAsset, nullData, nullAsset, failing, failureData, failureAsset,
			             throwingDispose, followingDispose, cleanupAsset, emptyData, emptyAsset, revisionFeature,
			             revisionData, revisionAsset, unrelatedData })
				instance.Dispose();
		}
		finally
		{
			RenderPipelineManager.Reset();
			ScriptableObjectRegistry.ClearSession(session);
			ScriptableObjectRegistry.SelectSession(0);
		}
	}

	private static void Expect(bool value, string message)
	{
		if (!value) throw new Exception(message);
	}
}
