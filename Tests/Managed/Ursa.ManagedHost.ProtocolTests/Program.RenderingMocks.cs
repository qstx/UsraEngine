using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Ursa.Bindings;
using Ursa.Bindings.Generated;
using Ursa.Rendering;
using WireRenderToken = Ursa.Bindings.Generated.RenderingModule.Value_Ursa_Rendering_RenderContextToken;
using WireRenderResult = Ursa.Bindings.Generated.RenderingModule.Value_Ursa_Rendering_RenderResultValue;
using WireCameraFrame = Ursa.Bindings.Generated.RenderingModule.Value_Ursa_Rendering_CameraFrameValue;
using WireDrawing = Ursa.Bindings.Generated.RenderingModule.Value_Ursa_Rendering_DrawingSettingsValue;
using WireFiltering = Ursa.Bindings.Generated.RenderingModule.Value_Ursa_Rendering_FilteringSettings;

internal static unsafe partial class Program
{
	private static readonly HashSet<ulong> boundRenderContexts = new();

	private static BindingCallbackRegistration InstallRenderingBindings()
	{
		var registration = new BindingCallbackRegistration(RenderingModule.Hash, (uint)(sizeof(RenderingModule.Table) / sizeof(nint)), sizeof(RenderingModule.Table));
		var table = (RenderingModule.Table*)registration.Functions;
		table->NativeRenderingBindings_Begin = &BoundRenderBegin;
		table->NativeRenderingBindings_End = &BoundRenderEnd;
		table->NativeRenderingBindings_Abort = &BoundRenderEnd;
		table->NativeRenderingBindings_Submit = &BoundRenderSubmit;
		table->NativeRenderingBindings_SetupCamera = &BoundRenderSetupCamera;
		table->NativeRenderingBindings_Cull = &BoundRenderCull;
		table->NativeRenderingBindings_CreateRendererList = &BoundCreateRendererList;
		table->NativeRenderingBindings_EnqueueBatch = &BoundEnqueueBatch;
		RenderingModule.Bind(registration.Descriptor, registration.Context);
		return registration;
	}

	private static void BoundRenderRequire(WireRenderToken token)
	{
		if (token.F_index != 1 || token.F_generation != 1 || token.F_frameId == 0 || !boundRenderContexts.Contains(token.F_sessionId))
			throw new InvalidOperationException("Stale protocol fixture render context.");
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundRenderBegin(nint context, ulong frame, float aspect, WireRenderToken* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ =>
		{
			if (frame == 0 || !float.IsFinite(aspect) || aspect <= 0) throw new InvalidOperationException("Invalid render frame.");
			*result = new() { F_sessionId = ++renderSessionIdentity, F_frameId = frame, F_index = 1, F_generation = 1 };
			boundRenderContexts.Add(result->F_sessionId);
		});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundRenderEnd(nint context, WireRenderToken* token, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => { BoundRenderRequire(*token); boundRenderContexts.Remove(token->F_sessionId); });

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundRenderSubmit(nint context, WireRenderToken* token, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => BoundRenderRequire(*token));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundRenderSetupCamera(nint context, WireRenderToken* token, Utf8View* identity, WireCameraFrame* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena =>
		{
			BoundRenderRequire(*token);
			*result = new() { Present = 1, F_Identity = arena.String(BindingMemory.String(*identity, false), false), F_Width = 64, F_Height = 64,
				F_Settings = new() { Present = 1, F_CullingMask = uint.MaxValue, F_Background = new() { F_R = 0.05f, F_G = 0.05f, F_B = 0.05f, F_A = 1 },
					F_Viewport = new() { F_Width = 1, F_Height = 1 }, F_TargetWidth = 64, F_TargetHeight = 64 } };
		});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundRenderCull(nint context, WireRenderToken* token, Utf8View* identity, WireRenderResult* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => { BoundRenderRequire(*token); *result = new() { F_Context = *token, F_Generation = 1 }; });

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundCreateRendererList(nint context, WireRenderToken* token, WireRenderResult* culling, WireDrawing* drawing, WireFiltering* filtering, WireRenderResult* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ =>
		{
			BoundRenderRequire(*token);
			if (culling->F_Context.F_sessionId != token->F_sessionId || culling->F_Generation == 0)
				throw new InvalidOperationException("Culling results belong to another context.");
			*result = new() { F_Context = *token, F_Generation = 1 };
		});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundEnqueueBatch(nint context, WireRenderToken* token, DataView* batch, Utf8View* name,
		BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ =>
		{
			BoundRenderRequire(*token);
			string? commandName = BindingMemory.String(*name, false);
			if (commandName == null) throw new InvalidOperationException("Render batch name is missing.");
			var bytes = BindingMemory.Array<byte, byte>(*batch, static value => value, false)!;
			RenderCommandProtocol.Validate(bytes);
			submittedRenderBatches.Add(bytes);
			++renderBatchSubmissions;
		});
}
