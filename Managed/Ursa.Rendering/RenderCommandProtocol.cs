using System;
using System.Runtime.InteropServices;
using Ursa.Bindings;
using Ursa.Bindings.Generated;

namespace Ursa.Rendering;

[StructLayout(LayoutKind.Sequential)]
[WireValue]
internal struct RenderContextToken
{
	public ulong sessionId;
	public ulong frameId;
	public uint index;
	public uint generation;
}

internal static class RenderCommandProtocol
{
	internal const uint version = BindingCommandWriter.Version;
	internal const int batchHeaderSize = BindingCommandWriter.HeaderSize;
	internal const int maxByteSize = BindingCommandWriter.MaximumBytes;
	internal static void Validate(ReadOnlySpan<byte> batch) => RenderingCommands.Validate(batch);
	internal static void Submit(RenderContextToken context, ReadOnlySpan<byte> batch, string name)
	{
		Validate(batch);
		NativeRenderingBindings.EnqueueBatch(context, batch, name);
	}
}
