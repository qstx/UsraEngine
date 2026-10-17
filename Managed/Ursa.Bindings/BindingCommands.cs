using System;
using System.Buffers.Binary;
using System.IO;
using System.Text;

namespace Ursa.Bindings;

// Primitive storage for generated codecs. Command identities and payload fields belong to the contract.
public sealed class BindingCommandWriter : IDisposable
{
	public const uint Version = 1;
	public const int HeaderSize = 48;
	public const int MaximumBytes = 64 * 1024 * 1024;
	private static readonly UTF8Encoding utf8 = new(false, true);
	private readonly MemoryStream stream = new();
	private readonly BinaryWriter writer;
	private readonly string identity;
	private readonly byte[] fingerprint;
	private long recordStart = -1;
	private uint count;
	private bool disposed;

	public BindingCommandWriter(string contractIdentity)
	{
		fingerprint = Convert.FromHexString(contractIdentity);
		if (fingerprint.Length != 32) throw new ArgumentException("A command contract requires a SHA-256 identity.", nameof(contractIdentity));
		identity = contractIdentity;
		writer = new BinaryWriter(stream, utf8, leaveOpen: true);
		Clear();
	}

	public void RequireIdentity(string expected)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		if (!string.Equals(identity, expected, StringComparison.Ordinal))
			throw new InvalidOperationException("The command writer belongs to a different binding contract.");
	}

	public void BeginRecord(uint opcode)
	{
		Ensure(8);
		if (recordStart >= 0) throw new InvalidOperationException("Command records cannot be nested.");
		recordStart = stream.Position;
		try { writer.Write(opcode); writer.Write(0u); }
		catch { CancelRecord(); throw; }
	}

	public void EndRecord()
	{
		if (recordStart < 0) throw new InvalidOperationException("No command record is open.");
		while ((stream.Position & 7) != 0) Write((byte)0);
		uint bytes = checked((uint)(stream.Position - recordStart));
		long end = stream.Position;
		stream.Position = recordStart + 4;
		writer.Write(bytes);
		stream.Position = end;
		count = checked(count + 1);
		recordStart = -1;
	}

	public void CancelRecord()
	{
		if (recordStart < 0) return;
		stream.SetLength(recordStart);
		stream.Position = recordStart;
		recordStart = -1;
	}

	public void Clear()
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		stream.SetLength(0);
		writer.Write(Version);
		writer.Write((uint)HeaderSize);
		writer.Write(0u);
		writer.Write((uint)HeaderSize);
		writer.Write(fingerprint);
		count = 0;
		recordStart = -1;
	}

	public byte[] ToArray()
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		if (recordStart >= 0) throw new InvalidOperationException("A command record is still open.");
		long end = stream.Position;
		stream.Position = 8;
		writer.Write(count);
		writer.Write(checked((uint)end));
		stream.Position = end;
		return stream.ToArray();
	}

	private void Ensure(int bytes)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		if (bytes < 0 || bytes > MaximumBytes - stream.Position)
			throw new InvalidOperationException("The command batch exceeds its transfer budget.");
	}

	public void Write(byte value) { Ensure(1); writer.Write(value); }
	public void Write(sbyte value) { Ensure(1); writer.Write(value); }
	public void Write(short value) { Ensure(2); writer.Write(value); }
	public void Write(ushort value) { Ensure(2); writer.Write(value); }
	public void Write(int value) { Ensure(4); writer.Write(value); }
	public void Write(uint value) { Ensure(4); writer.Write(value); }
	public void Write(long value) { Ensure(8); writer.Write(value); }
	public void Write(ulong value) { Ensure(8); writer.Write(value); }
	public void Write(float value) { Ensure(4); writer.Write(value); }
	public void Write(double value) { Ensure(8); writer.Write(value); }

	public void WriteCount(long length, bool present, int minimumElementBytes)
	{
		if (minimumElementBytes <= 0 || length < 0 || (!present && length != 0) ||
			length > (MaximumBytes - stream.Position - 12) / minimumElementBytes)
			throw new InvalidOperationException("The command collection exceeds its transfer budget.");
		Write(present ? 1u : 0u);
		Write((ulong)length);
	}

	public void WriteString(string? value, bool nullable)
	{
		if (value == null && !nullable) throw new ArgumentNullException(nameof(value));
		int byteCount = value == null ? 0 : utf8.GetByteCount(value);
		WriteCount(byteCount, value != null, 1);
		Ensure(byteCount);
		if (byteCount != 0) writer.Write(utf8.GetBytes(value!));
	}

	public void Dispose()
	{
		if (disposed) return;
		writer.Dispose();
		stream.Dispose();
		disposed = true;
	}
}

public ref struct BindingCommandReader
{
	private static readonly UTF8Encoding utf8 = new(false, true);
	private readonly ReadOnlySpan<byte> bytes;
	private readonly uint count;
	private uint records;
	private int offset;
	private int recordEnd;

	public BindingCommandReader(ReadOnlySpan<byte> batch, string identity)
	{
		this = default;
		if (batch.Length < BindingCommandWriter.HeaderSize || batch.Length > BindingCommandWriter.MaximumBytes ||
			BinaryPrimitives.ReadUInt32LittleEndian(batch) != BindingCommandWriter.Version ||
			BinaryPrimitives.ReadUInt32LittleEndian(batch[4..]) != BindingCommandWriter.HeaderSize ||
			BinaryPrimitives.ReadUInt32LittleEndian(batch[12..]) != batch.Length ||
			!batch.Slice(16, 32).SequenceEqual(Convert.FromHexString(identity)))
			throw new InvalidOperationException("The command batch range, version or contract identity is invalid.");
		bytes = batch;
		count = BinaryPrimitives.ReadUInt32LittleEndian(batch[8..]);
		if (count > (batch.Length - BindingCommandWriter.HeaderSize) / 8)
			throw new InvalidOperationException("The command batch record count is invalid.");
		offset = BindingCommandWriter.HeaderSize;
		recordEnd = offset;
	}

	public bool Next(out uint opcode)
	{
		if (offset != recordEnd) throw new InvalidOperationException("The previous command record was not consumed.");
		if (records == count)
		{
			if (offset != bytes.Length) throw new InvalidOperationException("The command batch contains trailing bytes.");
			opcode = 0;
			return false;
		}
		if (bytes.Length - offset < 8) throw new InvalidOperationException("The command record header is truncated.");
		opcode = BinaryPrimitives.ReadUInt32LittleEndian(bytes[offset..]);
		uint length = BinaryPrimitives.ReadUInt32LittleEndian(bytes[(offset + 4)..]);
		if (length < 8 || length % 8 != 0 || length > bytes.Length - offset)
			throw new InvalidOperationException("The command record range is invalid.");
		recordEnd = offset + checked((int)length);
		offset += 8;
		++records;
		return true;
	}

	public void EndRecord()
	{
		if (recordEnd - offset > 7) throw new InvalidOperationException("The command payload has trailing data.");
		while (offset < recordEnd)
			if (bytes[offset++] != 0) throw new InvalidOperationException("The command padding must be zero.");
	}

	private ReadOnlySpan<byte> Take(int length)
	{
		if (length < 0 || length > recordEnd - offset) throw new InvalidOperationException("The command payload is truncated.");
		ReadOnlySpan<byte> value = bytes.Slice(offset, length);
		offset += length;
		return value;
	}

	public byte ReadByte() => Take(1)[0];
	public sbyte ReadSByte() => unchecked((sbyte)ReadByte());
	public short ReadInt16() => BinaryPrimitives.ReadInt16LittleEndian(Take(2));
	public ushort ReadUInt16() => BinaryPrimitives.ReadUInt16LittleEndian(Take(2));
	public int ReadInt32() => BinaryPrimitives.ReadInt32LittleEndian(Take(4));
	public uint ReadUInt32() => BinaryPrimitives.ReadUInt32LittleEndian(Take(4));
	public long ReadInt64() => BinaryPrimitives.ReadInt64LittleEndian(Take(8));
	public ulong ReadUInt64() => BinaryPrimitives.ReadUInt64LittleEndian(Take(8));
	public float ReadSingle() => BinaryPrimitives.ReadSingleLittleEndian(Take(4));
	public double ReadDouble() => BinaryPrimitives.ReadDoubleLittleEndian(Take(8));
	public bool ReadPresence() => BindingMemory.Bool(ReadUInt32());

	public int ReadCount(bool nullable, int minimumElementBytes)
	{
		bool present = ReadPresence();
		ulong length = ReadUInt64();
		if ((!present && (!nullable || length != 0)) || minimumElementBytes <= 0 ||
			length > (ulong)((recordEnd - offset) / minimumElementBytes))
			throw new InvalidOperationException("The command collection range or presence is invalid.");
		return checked((int)length);
	}

	public void SkipString(bool nullable)
	{
		int length = ReadCount(nullable, 1);
		utf8.GetCharCount(Take(length));
	}
}
