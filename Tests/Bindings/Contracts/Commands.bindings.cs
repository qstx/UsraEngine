using System;
using Ursa;
using Ursa.Bindings;

namespace BindingFixture;

[BindingModule("Fixture", Required = true)]
internal static partial class FixtureCommands
{
	[RecordedCommand]
	internal static partial void Capture(BindingCommandWriter writer, Packet packet, ReadOnlySpan<int> values, Matrix4x4 matrix);

	[RecordedCommand]
	internal static partial void Flag(BindingCommandWriter writer, bool value);

	[RecordedCommand]
	internal static partial void Values(BindingCommandWriter writer, ReadOnlySpan<int> values);

	[RecordedCommand(NativeOnly = true)]
	internal static partial void Reset(BindingCommandWriter writer);
}
