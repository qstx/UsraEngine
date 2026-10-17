using System;
using Ursa.Bindings;
using Ursa;

namespace BindingFixture;

[BindingModule("Fixture", Required = true)]
[NativeHeader("BindingFixture.hpp")]
internal static partial class FixtureBindings
{
	[NativeMethod("Ursa::Tests::BindingFixture::EchoMath")]
	internal static partial MathPacket EchoMath(MathPacket value);

	[NativeMethod("Ursa::Tests::BindingFixture::CaptureInput")]
	internal static partial void CaptureInput(ReadOnlySpan<int> values);

	[NativeMethod("Ursa::Tests::BindingFixture::ReadCaptured")]
	internal static partial int[] ReadCaptured();

	[ManagedCallback("BindingFixture.Callbacks.Capture")]
	internal static partial void ManagedCapture(int[] values);

	[ManagedCallback("BindingFixture.Callbacks.ReadCaptured")]
	internal static partial int[] ManagedReadCaptured();

	[NativeMethod("Ursa::Tests::BindingFixture::MultiplyMatrices")]
	internal static partial Matrix4x4 MultiplyMatrices(Matrix4x4 left, Matrix4x4 right);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoQuaternion")]
	internal static partial Quaternion EchoQuaternion(Quaternion value);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoColor")]
	internal static partial Color EchoColor(Color value);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoScalars")]
	internal static partial Scalars EchoScalars(Scalars value);

	[NativeMethod("Ursa::Tests::BindingFixture::ReadObject")]
	internal static partial uint ReadObject([HandleKind(1)] FixtureHandle handle);

	[NativeMethod("Ursa::Tests::BindingFixture::Add")]
	internal static partial long Add(long left, long right);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoPoint")]
	internal static partial Point EchoPoint(Point value);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoText")]
	internal static partial string? EchoText(string? value);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoValues")]
	internal static partial Point[] EchoValues(Point[] values);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoOptional")]
	internal static partial Point? EchoOptional(Point? value);

	[NativeMethod("Ursa::Tests::BindingFixture::EchoPacket")]
	internal static partial Packet EchoPacket(Packet value);

	[NativeMethod("Ursa::Tests::BindingFixture::Sum")]
	internal static partial long Sum(ReadOnlySpan<int> values);

	[NativeMethod("Ursa::Tests::BindingFixture::Mutate")]
	internal static partial string Mutate(string name);

	[NativeMethod("Ursa::Tests::BindingFixture::GetCount")]
	internal static partial uint GetCount();

	[NativeMethod("Ursa::Tests::BindingFixture::Fail")]
	internal static partial void Fail(int code);

	[NativeMethod("Ursa::Tests::BindingFixture::InvalidOutput")]
	internal static partial string InvalidOutput();

	[NativeMethod("Ursa::Tests::BindingFixture::BorrowedOutput")]
	internal static partial string BorrowedOutput(string value);

	[ManagedCallback("BindingFixture.Callbacks.Scale")]
	internal static partial Point ManagedScale(Point value);

	[ManagedCallback("BindingFixture.Callbacks.Fail")]
	internal static partial void ManagedFail();
}
