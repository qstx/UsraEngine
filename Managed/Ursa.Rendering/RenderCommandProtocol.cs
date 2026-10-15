using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace Ursa.Rendering;

internal enum RenderCommandOpcode : uint
{
	SetRenderTarget = 1,
	ClearTarget,
	SetViewport,
	DrawRendererList,
	DrawMesh,
	Blit,
	CopyBuffer,
	DispatchCompute,
	SetGlobalParameters,
	GetTemporaryTarget,
	ReleaseTemporaryTarget,
	SetScissor,
	CopyTexture,
	DrawFullscreen,
	SetRenderTargets,
	SetComputeParameters
}

[StructLayout(LayoutKind.Sequential)]
internal struct RenderContextToken
{
	public ulong sessionId;
	public ulong frameId;
	public uint index;
	public uint generation;
}

internal static class RenderCommandProtocol
{
	internal const uint version = 2;
	internal const int batchHeaderSize = 16;
	internal const int recordHeaderSize = 8;
	internal const int maxByteSize = 64 * 1024 * 1024;

	internal static void Validate(ReadOnlySpan<byte> batch)
	{
		if (!BitConverter.IsLittleEndian || batch.Length < batchHeaderSize || batch.Length > maxByteSize)
			throw new InvalidOperationException("Render batch byte range or platform is invalid.");
		uint readVersion = BinaryPrimitives.ReadUInt32LittleEndian(batch);
		uint headerSize = BinaryPrimitives.ReadUInt32LittleEndian(batch[4..]);
		uint count = BinaryPrimitives.ReadUInt32LittleEndian(batch[8..]);
		uint byteSize = BinaryPrimitives.ReadUInt32LittleEndian(batch[12..]);
		if (readVersion != version || headerSize != batchHeaderSize || byteSize != batch.Length ||
			count > (uint)((batch.Length - batchHeaderSize) / recordHeaderSize))
			throw new InvalidOperationException("Render batch version, size or count is invalid.");
		int offset = batchHeaderSize;
		for (uint index = 0; index < count; ++index)
		{
			if (batch.Length - offset < recordHeaderSize)
				throw new InvalidOperationException("Render command record is truncated.");
			uint opcode = BinaryPrimitives.ReadUInt32LittleEndian(batch[offset..]);
			uint size = BinaryPrimitives.ReadUInt32LittleEndian(batch[(offset + 4)..]);
			if (opcode < (uint)RenderCommandOpcode.SetRenderTarget || opcode > (uint)RenderCommandOpcode.SetComputeParameters ||
				size < recordHeaderSize || size % 8 != 0 || size > batch.Length - offset)
				throw new InvalidOperationException("Render command opcode or record size is invalid.");
			int payloadSize = checked((int)size) - recordHeaderSize;
			int expectedSize = opcode switch
			{
				(uint)RenderCommandOpcode.SetRenderTarget when size == Align(4) || size == Align(24) || size == Align(32) => checked((int)size),
				(uint)RenderCommandOpcode.SetRenderTarget => 0,
				(uint)RenderCommandOpcode.ClearTarget => Align(28),
				(uint)RenderCommandOpcode.SetViewport => Align(24),
				(uint)RenderCommandOpcode.DrawRendererList => Align(32),
				(uint)RenderCommandOpcode.DrawMesh => Align(116),
				(uint)RenderCommandOpcode.Blit when size == Align(76) || size == Align(92) => checked((int)size),
				(uint)RenderCommandOpcode.Blit => 0,
				(uint)RenderCommandOpcode.CopyBuffer => Align(72),
				(uint)RenderCommandOpcode.SetGlobalParameters => GlobalParameterRecordSize(batch.Slice(offset + recordHeaderSize, payloadSize)),
				(uint)RenderCommandOpcode.SetComputeParameters => GlobalParameterRecordSize(batch.Slice(offset + recordHeaderSize, payloadSize), 28),
				(uint)RenderCommandOpcode.GetTemporaryTarget => Align(28),
				(uint)RenderCommandOpcode.ReleaseTemporaryTarget => Align(4),
				(uint)RenderCommandOpcode.SetScissor => Align(16),
				(uint)RenderCommandOpcode.CopyTexture when size == Align(48) || size == Align(64) => checked((int)size),
				(uint)RenderCommandOpcode.DrawFullscreen => Align(28),
				(uint)RenderCommandOpcode.SetRenderTargets => AttachmentRecordSize(batch.Slice(offset + recordHeaderSize, payloadSize)),
				(uint)RenderCommandOpcode.DispatchCompute => Align(40),
				_ => 0
			};
			if (expectedSize != size)
				throw new InvalidOperationException("Render command payload size or value kind is invalid.");
			offset += checked((int)size);
		}
		if (offset != batch.Length) throw new InvalidOperationException("Render batch contains trailing bytes.");
	}

	private static int GlobalParameterRecordSize(ReadOnlySpan<byte> payload, int prefix = 0)
	{
		if (payload.Length < prefix + 8) return 0;
		uint kind = BinaryPrimitives.ReadUInt32LittleEndian(payload[prefix..]);
		uint nameBytes = BinaryPrimitives.ReadUInt32LittleEndian(payload[(prefix + 4)..]);
		int valueBytes = kind switch { 1 => 4, 2 => 16, 3 => 24, 4 => 32, 5 => 64, 6 => 4, 7 => 24, _ => 0 };
		if (valueBytes == 0 || nameBytes == 0 || nameBytes > 1024 || nameBytes > payload.Length - prefix - 8 ||
			valueBytes > payload.Length - prefix - 8 - nameBytes)
			return 0;
		return Align(checked(prefix + (int)(8 + nameBytes + (uint)valueBytes)));
	}

	private static int AttachmentRecordSize(ReadOnlySpan<byte> payload)
	{
		if (payload.Length < 8) return 0;
		uint count = BinaryPrimitives.ReadUInt32LittleEndian(payload);
		uint depth = BinaryPrimitives.ReadUInt32LittleEndian(payload[4..]);
		if (count > 8 || depth > 1 || count + depth == 0) return 0;
		return Align(checked(8 + (int)(count + depth) * 32));
	}

	private static int Align(int payloadBytes) => (recordHeaderSize + payloadBytes + 7) & ~7;

	internal static void Submit(RenderContextToken context, ReadOnlySpan<byte> batch)
	{
		Validate(batch);
		Span<byte> token = stackalloc byte[24];
		BinaryPrimitives.WriteUInt64LittleEndian(token, context.sessionId);
		BinaryPrimitives.WriteUInt64LittleEndian(token[8..], context.frameId);
		BinaryPrimitives.WriteUInt32LittleEndian(token[16..], context.index);
		BinaryPrimitives.WriteUInt32LittleEndian(token[20..], context.generation);
		Ursa.EngineApi.SubmitRenderBatch(token, batch);
	}
}
