using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Ursa.Bindings;

namespace Ursa.BindingsGenerator;

internal static class BuiltinAbi
{
	private static readonly Type[] types = { typeof(Utf8View), typeof(DataView), typeof(BindingOutcome), typeof(BindingModuleDescriptor), typeof(BindingModuleReference), typeof(BindingBootstrap) };
	internal static string Identity => "lease-context-v1\n" + string.Join("\n", types.Select(type =>
		type.Name + ":" + Marshal.SizeOf(type) + ":" + string.Join(";", type.GetFields(BindingFlags.Public | BindingFlags.Instance)
			.OrderBy(member => Marshal.OffsetOf(type, member.Name).ToInt64())
			.Select(member => member.Name + ":" + Marshal.OffsetOf(type, member.Name).ToInt64() + ":" + member.FieldType))));

	internal static string NativeAssertions()
	{
		var output = new StringBuilder();
		output.Append("static_assert(contextProtocolVersion == 1, \"Binding context protocol mismatch\");\n");
		foreach (Type type in types)
		{
			output.Append($"static_assert(sizeof({type.Name}) == {Marshal.SizeOf(type)}, \"Builtin binding ABI differs from C# {type.Name}\");\n");
			foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
			{
				string nativeName = char.ToLowerInvariant(field.Name[0]) + field.Name[1..];
				output.Append($"static_assert(offsetof({type.Name}, {nativeName}) == {Marshal.OffsetOf(type, field.Name).ToInt64()}, \"Builtin binding field differs from C# {type.Name}.{field.Name}\");\n");
			}
		}
		return output.ToString();
	}
}
