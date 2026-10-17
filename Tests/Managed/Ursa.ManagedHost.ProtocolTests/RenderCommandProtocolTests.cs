using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using Ursa.Bindings;
using Ursa.Rendering;
using CommandCodec = Ursa.Bindings.Generated.RenderingCommands;

internal static class RenderCommandProtocolTests
{
	internal static void Run()
	{
		if (Marshal.SizeOf<RenderContextToken>() != 24 || Marshal.OffsetOf<RenderContextToken>("frameId").ToInt32() != 8 ||
			Marshal.OffsetOf<RenderContextToken>("index").ToInt32() != 16 || Marshal.OffsetOf<RenderContextToken>("generation").ToInt32() != 20)
			throw new Exception("Render context field layout changed.");
		using var writer = new BindingCommandWriter(CommandCodec.Hash);
		RenderCommandProtocol.Validate(writer.ToArray());
		RenderCommands.SetRenderTarget(writer, new RenderTargetValue { Id = 5 });
		byte[] valid = writer.ToArray();
		RenderCommandProtocol.Validate(valid);
		void Reject(byte[] bytes)
		{
			try { RenderCommandProtocol.Validate(bytes); }
			catch (InvalidOperationException) { return; }
			throw new Exception("Malformed generated render batch accepted.");
		}
		foreach (int cut in new[] { 0, 47, 48, valid.Length - 1 }) Reject(valid[..cut]);
		foreach (int offset in new[] { 0, 8, 12, 16 })
		{
			byte[] bytes = (byte[])valid.Clone();
			bytes[offset] ^= 0xff;
			Reject(bytes);
		}
		foreach (uint size in new uint[] { 0, 7, 9, 8, 0xfffffff8 })
		{
			byte[] bytes = (byte[])valid.Clone();
			BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(BindingCommandWriter.HeaderSize + 4), size);
			Reject(bytes);
		}
		foreach (uint opcode in new uint[] { 0, uint.MaxValue })
		{
			byte[] bytes = (byte[])valid.Clone();
			BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(BindingCommandWriter.HeaderSize), opcode);
			Reject(bytes);
		}
		byte[] padding = (byte[])valid.Clone();
		padding[^1] = 1;
		Reject(padding);
		bool nativeOnlyRejected = false;
		try { RenderCommands.ResetCameraState(writer); }
		catch (NotSupportedException) { nativeOnlyRejected = true; }
		if (!nativeOnlyRejected) throw new Exception("Managed code recorded a native-only command.");
	}
}
