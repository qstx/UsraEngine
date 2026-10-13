using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using Ursa.Rendering;

internal static class RenderCommandProtocolTests
{
	internal static void Run()
	{
		if (Marshal.SizeOf<RenderContextToken>() != 24) throw new Exception("Render context ABI changed.");
		if (Marshal.OffsetOf<RenderContextToken>("frameId").ToInt32() != 8 ||
		    Marshal.OffsetOf<RenderContextToken>("index").ToInt32() != 16 ||
		    Marshal.OffsetOf<RenderContextToken>("generation").ToInt32() != 20)
			throw new Exception("Render context field offsets changed.");
		byte[] batch = new byte[32];
		void Header(uint version, uint count, uint size)
		{
			BinaryPrimitives.WriteUInt32LittleEndian(batch, version);
			BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(4), 16);
			BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(8), count);
			BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(12), size);
		}
		void Record(uint opcode, uint size)
		{
			BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(16), opcode);
			BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(20), size);
		}
		void Reject()
		{
			try { RenderCommandProtocol.Validate(batch); }
			catch (InvalidOperationException) { return; }
			throw new Exception("Malformed render batch accepted.");
		}
		Header(1, 0, 16);
		RenderCommandProtocol.Validate(batch.AsSpan(0, 16));
		Header(1, 1, 32);
		Record(1, 16);
		RenderCommandProtocol.Validate(batch);
		Header(2, 1, 32);
		Reject();
		Header(1, 2, 32);
		Reject();
		Header(1, 1, 32);
		foreach (uint size in new uint[] {0, 7, 9, 24, 0xfffffff8}) { Record(1, size); Reject(); }
		foreach (uint opcode in new uint[] {0, 14, 0xffffffff}) { Record(opcode, 16); Reject(); }
		Record(1, 8);
		Reject();
	}
}
