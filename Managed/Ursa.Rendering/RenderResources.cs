using System;
using Ursa;

namespace Ursa.Rendering;

[Ursa.Bindings.WireValue]
public enum GraphicsFormat : uint
{
	R8G8B8A8_UNorm,
	R16G16B16A16_SFloat,
	D32_SFloat
}

public readonly struct RenderTargetIdentifier
{
	internal RenderTexture? Texture { get; }
	internal uint Id { get; }

	public RenderTargetIdentifier(RenderTexture texture)
	{
		Texture = texture ?? throw new ArgumentNullException(nameof(texture));
		Id = 0;
	}

	public RenderTargetIdentifier(uint temporaryId)
	{
		Texture = null;
		Id = temporaryId;
	}

	public static RenderTargetIdentifier CameraTarget => new(0u);
	public static implicit operator RenderTargetIdentifier(RenderTexture texture) => new(texture);
	public static implicit operator RenderTargetIdentifier(uint id) => new(id);
}

[Flags]
[Ursa.Bindings.WireValue]
public enum GraphicsBufferTarget : uint
{
	Vertex = 1,
	Index = 2,
	Structured = 4,
	Raw = 8,
	IndirectArguments = 16
}

public sealed class RenderTextureDescriptor
{
	public RenderTextureDescriptor(uint width, uint height, GraphicsFormat colorFormat = GraphicsFormat.R8G8B8A8_UNorm,
		uint depthBufferBits = 0, bool enableRandomWrite = false, uint msaaSamples = 1)
	{
		if (width == 0 || height == 0) throw new ArgumentOutOfRangeException(nameof(width));
		if (msaaSamples != 1) throw new ArgumentOutOfRangeException(nameof(msaaSamples), "Only single-sample render textures are supported.");
		if (colorFormat > GraphicsFormat.D32_SFloat) throw new ArgumentOutOfRangeException(nameof(colorFormat));
		if (depthBufferBits is not (0 or 32)) throw new ArgumentOutOfRangeException(nameof(depthBufferBits));
		Width = width;
		Height = height;
		ColorFormat = colorFormat;
		DepthBufferBits = depthBufferBits;
		EnableRandomWrite = enableRandomWrite;
		MsaaSamples = msaaSamples;
	}

	public uint Width { get; }
	public uint Height { get; }
	public GraphicsFormat ColorFormat { get; }
	public uint DepthBufferBits { get; }
	public bool EnableRandomWrite { get; }
	public uint MsaaSamples { get; }
}

public sealed class RenderTexture : RenderResource, IDisposable
{
	private bool released;
	private RenderTexture(NativeHandle handle) : base(handle) { }

	internal static RenderTexture? FromHandle(NativeHandle handle) => Wrap(handle, value => new RenderTexture(value));

	public static RenderTexture Create(RenderTextureDescriptor descriptor)
	{
		ArgumentNullException.ThrowIfNull(descriptor);
		return FromHandle(NativeRenderingBindings.CreateRenderTexture(new RenderTextureValue
		{
			Width = descriptor.Width, Height = descriptor.Height, ColorFormat = descriptor.ColorFormat,
			DepthBufferBits = descriptor.DepthBufferBits, EnableRandomWrite = descriptor.EnableRandomWrite,
			MsaaSamples = descriptor.MsaaSamples
		}))!;
	}

	public void Release()
	{
		if (released) return;
		NativeRenderingBindings.ReleaseRenderTexture(Handle);
		released = true;
	}
	public void Dispose() => Release();

	public byte[] GetRawData() => NativeRenderingBindings.ReadRenderTexture(Handle);
}

public sealed class GraphicsBuffer : RenderResource, IDisposable
{
	private bool released;
	private GraphicsBuffer(NativeHandle handle) : base(handle) { }

	internal static GraphicsBuffer? FromHandle(NativeHandle handle) => Wrap(handle, value => new GraphicsBuffer(value));

	public static GraphicsBuffer Create(GraphicsBufferTarget target, uint count, uint stride)
	{
		const GraphicsBufferTarget validTargets = GraphicsBufferTarget.Vertex | GraphicsBufferTarget.Index |
												 GraphicsBufferTarget.Structured | GraphicsBufferTarget.Raw |
												 GraphicsBufferTarget.IndirectArguments;
		if (target == 0 || (target & ~validTargets) != 0 || count == 0 || stride == 0)
			throw new ArgumentOutOfRangeException(nameof(target));
		return FromHandle(NativeRenderingBindings.CreateGraphicsBuffer(target, count, stride))!;
	}

	public void Release()
	{
		if (released) return;
		NativeRenderingBindings.ReleaseGraphicsBuffer(Handle);
		released = true;
	}
	public void Dispose() => Release();

	public byte[] GetRawData(ulong offset, ulong size) => NativeRenderingBindings.ReadGraphicsBuffer(Handle, offset, size);
}
