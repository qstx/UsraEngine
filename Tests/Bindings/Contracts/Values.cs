using Ursa.Bindings;

namespace BindingFixture;

[WireValue]
internal enum FixtureFlags : uint
{
	None = 0,
	Visible = 1,
	Selected = 2
}

[WireValue]
internal enum WideFlags : ulong { Maximum = ulong.MaxValue }

[WireValue]
internal enum SignedFlags : long { Minimum = long.MinValue }

[WireValue]
internal struct Scalars
{
	public sbyte Small;
	public byte Byte;
	public short Signed16;
	public ushort Unsigned16;
	public int Signed32;
	public uint Unsigned32;
	public long Signed64;
	public ulong Unsigned64;
	public float Float;
	public double Double;
	public FixtureFlags Flags;
	public WideFlags Wide;
	public SignedFlags Signed;
}

[WireValue]
internal struct FixtureHandle
{
	public ulong Session;
	public uint Index;
	public uint Generation;
	public uint Kind;
	public uint Reserved;
}

[WireValue]
internal struct Point
{
	public float X;
	public double Y;
	public bool Enabled;
}

[WireValue]
internal struct MathPacket
{
	public Ursa.Vector2 Uv;
	public Ursa.Vector3 Position;
	public Ursa.Vector4 Tangent;
}

[WireValue]
internal sealed class Packet
{
	public string? Name { get; set; }
	public Point[] Values { get; set; } = System.Array.Empty<Point>();
	public ulong Identity { get; set; }
}
