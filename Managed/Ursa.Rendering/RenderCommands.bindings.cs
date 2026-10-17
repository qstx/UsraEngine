using System;
using Ursa.Bindings;

namespace Ursa.Rendering;

[BindingModule("Rendering", Required = true, Dependencies = new[] { "Runtime" })]
internal static partial class RenderCommands
{
	[RecordedCommand]
	internal static partial void SetRenderTarget(BindingCommandWriter writer, RenderTargetValue target);
	[RecordedCommand]
	internal static partial void SetRenderTargets(BindingCommandWriter writer, RenderTargetValue[] colors, RenderTargetValue? depth);
	[RecordedCommand]
	internal static partial void ClearTarget(BindingCommandWriter writer, uint flags, Color color, float depth, uint stencil);
	[RecordedCommand]
	internal static partial void SetViewport(BindingCommandWriter writer, float x, float y, float width, float height, float minimumDepth, float maximumDepth);
	[RecordedCommand]
	internal static partial void SetScissor(BindingCommandWriter writer, int x, int y, uint width, uint height);
	[RecordedCommand]
	internal static partial void DrawRendererList(BindingCommandWriter writer, RenderResultValue list);
	[RecordedCommand]
	internal static partial void DrawMesh(BindingCommandWriter writer, [HandleKind(4)] NativeHandle mesh,
		[HandleKind(5)] NativeHandle material, Matrix4x4 localToWorld, uint subMesh, int pass);
	[RecordedCommand]
	internal static partial void Blit(BindingCommandWriter writer, RenderTargetValue source, RenderTargetValue destination,
		[HandleKind(5)] NativeHandle material, int pass);
	[RecordedCommand]
	internal static partial void CopyTexture(BindingCommandWriter writer, RenderTargetValue source, RenderTargetValue destination);
	[RecordedCommand]
	internal static partial void DrawFullscreen(BindingCommandWriter writer, [HandleKind(5)] NativeHandle material, int pass);
	[RecordedCommand]
	internal static partial void CopyBuffer(BindingCommandWriter writer, [HandleKind(7)] NativeHandle source,
		[HandleKind(7)] NativeHandle destination, ulong size, ulong sourceOffset, ulong destinationOffset);
	[RecordedCommand]
	internal static partial void GetTemporaryTarget(BindingCommandWriter writer, uint id, RenderTextureValue descriptor);
	[RecordedCommand]
	internal static partial void ReleaseTemporaryTarget(BindingCommandWriter writer, uint id);

	[RecordedCommand]
	internal static partial void SetGlobalFloat(BindingCommandWriter writer, string name, float value);
	[RecordedCommand]
	internal static partial void SetGlobalInt(BindingCommandWriter writer, string name, int value);
	[RecordedCommand]
	internal static partial void SetGlobalVector(BindingCommandWriter writer, string name, Vector4 value);
	[RecordedCommand]
	internal static partial void SetGlobalMatrix(BindingCommandWriter writer, string name, Matrix4x4 value);
	[RecordedCommand]
	internal static partial void SetGlobalTexture(BindingCommandWriter writer, string name, RenderTargetValue value);
	[RecordedCommand]
	internal static partial void SetGlobalBuffer(BindingCommandWriter writer, string name, [HandleKind(7)] NativeHandle value);

	[RecordedCommand]
	internal static partial void SetComputeFloat(BindingCommandWriter writer, NativeAssetReference shader, int kernel, string name, float value);
	[RecordedCommand]
	internal static partial void SetComputeInt(BindingCommandWriter writer, NativeAssetReference shader, int kernel, string name, int value);
	[RecordedCommand]
	internal static partial void SetComputeVector(BindingCommandWriter writer, NativeAssetReference shader, int kernel, string name, Vector4 value);
	[RecordedCommand]
	internal static partial void SetComputeMatrix(BindingCommandWriter writer, NativeAssetReference shader, int kernel, string name, Matrix4x4 value);
	[RecordedCommand]
	internal static partial void SetComputeTexture(BindingCommandWriter writer, NativeAssetReference shader, int kernel, string name, RenderTargetValue value);
	[RecordedCommand]
	internal static partial void SetComputeBuffer(BindingCommandWriter writer, NativeAssetReference shader, int kernel, string name, [HandleKind(7)] NativeHandle value);
	[RecordedCommand]
	internal static partial void DispatchCompute(BindingCommandWriter writer, NativeAssetReference shader, int kernel, uint groupsX, uint groupsY, uint groupsZ);

	[RecordedCommand(NativeOnly = true)]
	internal static partial void ResetCameraState(BindingCommandWriter writer);
}

[WireValue]
internal enum RenderTargetKind : uint { Temporary, Texture }

[WireValue]
internal struct RenderTargetValue
{
	public RenderTargetKind Kind;
	public uint Id;
	public NativeHandle? Texture;
}
