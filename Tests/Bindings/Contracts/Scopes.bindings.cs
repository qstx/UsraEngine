using Ursa.Bindings;

namespace BindingFixture;

[BindingModule("Dependent", Dependencies = new[] { "Fixture" })]
[NativeHeader("BindingFixture.hpp")]
internal static partial class DependentBindings
{
	[NativeMethod("Ursa::Tests::BindingFixture::Add")]
	internal static partial long Add(long left, long right);
}

[BindingModule("Scoped", CallScoped = true, Dependencies = new[] { "Dependent" })]
[NativeHeader("BindingFixture.hpp")]
internal static partial class ScopedBindings
{
	[NativeMethod("Ursa::Tests::BindingFixture::Add")]
	internal static partial long Add(long left, long right);
}
