using System;
using Ursa.Bindings;

namespace Ursa.Rendering;

[BindingModule("Rendering", Required = true, Dependencies = new[] { "Runtime" })]
[NativeHeader("Scripting/Bindings/RenderingServices.hpp")]
internal static partial class NativeRenderingBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::Begin")]
	internal static partial RenderContextToken Begin(ulong frameId, float aspect);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::End")]
	internal static partial void End(RenderContextToken context);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::Abort")]
	internal static partial void Abort(RenderContextToken context);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::Submit")]
	internal static partial void Submit(RenderContextToken context);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::Cameras")]
	internal static partial CameraFrameValue[] Cameras(RenderContextToken context);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::SetupCamera")]
	internal static partial CameraFrameValue SetupCamera(RenderContextToken context, string identity);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::Cull")]
	internal static partial RenderResultValue Cull(RenderContextToken context, string identity);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::CreateRendererList")]
	internal static partial RenderResultValue CreateRendererList(RenderContextToken context, RenderResultValue culling,
		DrawingSettingsValue drawing, FilteringSettings filtering);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::EnqueueBatch")]
	internal static partial void EnqueueBatch(RenderContextToken context, ReadOnlySpan<byte> batch, string name);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::CreateRenderTexture")]
	internal static partial NativeHandle CreateRenderTexture(RenderTextureValue descriptor);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::ReleaseRenderTexture")]
	internal static partial void ReleaseRenderTexture([HandleKind(6)] NativeHandle texture);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::ReadRenderTexture")]
	internal static partial byte[] ReadRenderTexture([HandleKind(6)] NativeHandle texture);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::CreateGraphicsBuffer")]
	internal static partial NativeHandle CreateGraphicsBuffer(GraphicsBufferTarget target, uint count, uint stride);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::ReleaseGraphicsBuffer")]
	internal static partial void ReleaseGraphicsBuffer([HandleKind(7)] NativeHandle buffer);
	[NativeMethod("Ursa::Scripting::Bindings::RenderingServices::ReadGraphicsBuffer")]
	internal static partial byte[] ReadGraphicsBuffer([HandleKind(7)] NativeHandle buffer, ulong offset, ulong size);
}

[WireValue]
internal struct RenderResultValue
{
	public RenderContextToken Context;
	public uint Index;
	public uint Generation;
}

[WireValue]
internal struct DrawingSettingsValue
{
	public string[] ShaderTags;
	public SortingCriteria Sorting;
	public Vector3? CameraPosition;
	public string ActivePipeline;
	public int OverrideMaterialPass;
	public NativeHandle? OverrideMaterial;
}

[WireValue]
internal struct RenderTextureValue
{
	public uint Width;
	public uint Height;
	public GraphicsFormat ColorFormat;
	public uint DepthBufferBits;
	public bool EnableRandomWrite;
	public uint MsaaSamples;
}
