using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Ursa.BindingsGenerator;

internal static partial class BindingEmitter
{
	private static BindingMethod[] Commands(BindingModule module) => module.Methods.Where(m => m.Direction == BindingDirection.RecordedCommand).ToArray();
	private static WireType[] CommandTypes(BindingModule module)
	{
		var types = new HashSet<WireType>();
		void Collect(WireType type)
		{
			if (!types.Add(type)) return;
			if (type.Element != null) Collect(type.Element);
			foreach (WireField field in type.Fields) Collect(field.Type);
		}
		foreach (BindingParameter parameter in Commands(module).SelectMany(m => m.Parameters)) Collect(parameter.Type);
		return OrderedTypes(types.OrderBy(t => t.Key, StringComparer.Ordinal).ToArray()).ToArray();
	}

	private static int MinimumCommandBytes(WireType type) => type.Kind switch
	{
		"string" or "array" or "span" => 12,
		"optional" => 4,
		"struct" when type.IsClass => 4,
		"struct" => type.Fields.Sum(f => MinimumCommandBytes(f.Type)),
		_ => type.Size
	};

	private static string CodecName(WireType type) => ContractReader.Name(type.Key);
	private static string ScalarReader(WireType type) => (type.Kind == "enum" ? type.Element!.Key : type.Key) switch
	{
		"bool" => "ReadPresence", "byte" => "ReadByte", "sbyte" => "ReadSByte", "short" => "ReadInt16",
		"ushort" => "ReadUInt16", "int" => "ReadInt32", "uint" => "ReadUInt32", "long" => "ReadInt64",
		"ulong" => "ReadUInt64", "float" => "ReadSingle", "double" => "ReadDouble",
		_ => throw new BindingDiagnostic("UBG0210", "Unsupported command scalar: " + type.Key)
	};

	private static string EmitManagedCommands(BindingModule module)
	{
		string owner = module.Name + "Commands";
		var output = new StringBuilder(banner).Append("#nullable enable\nusing System;\nusing Ursa.Bindings;\nnamespace Ursa.Bindings.Generated\n{\n");
		output.Append($"internal static class {owner}\n{{\n\tinternal const string Hash = {Literal(module.CommandHash)};\n");
		foreach (BindingMethod method in Commands(module))
			output.Append($"\tinternal const uint Opcode_{ContractReader.Slot(method)} = 0x{method.Opcode:x8}u;\n");
		foreach (WireType type in CommandTypes(module))
		{
			string codec = CodecName(type);
			output.Append($"\tprivate static void Write_{codec}(BindingCommandWriter writer, {type.CsName} value)\n\t{{\n");
			if (type.Kind is "scalar" or "enum")
				output.Append("\t\twriter.Write(" + (type.Key == "bool" ? "value ? 1u : 0u" : type.Kind == "enum" ? "(" + type.Element!.CsName + ")value" : "value") + ");\n");
			else if (type.Kind == "string") output.Append($"\t\twriter.WriteString(value, {type.Nullable.ToString().ToLowerInvariant()});\n");
			else if (type.Kind is "array" or "span")
			{
				if (type.Kind == "array")
				{
					if (!type.Nullable) output.Append("\t\tArgumentNullException.ThrowIfNull(value);\n");
					else output.Append($"\t\tif (value == null) {{ writer.WriteCount(0, false, {MinimumCommandBytes(type.Element!)}); return; }}\n");
				}
				output.Append($"\t\twriter.WriteCount(value.Length, true, {MinimumCommandBytes(type.Element!)});\n\t\tforeach (var item in value) Write_{CodecName(type.Element!)}(writer, item);\n");
			}
			else
			{
				if (type.Kind == "optional") output.Append("\t\twriter.Write(value.HasValue ? 1u : 0u);\n\t\tif (!value.HasValue) return;\n");
				else if (type.IsClass)
				{
					if (!type.Nullable) output.Append("\t\tArgumentNullException.ThrowIfNull(value);\n");
					output.Append("\t\twriter.Write(value == null ? 0u : 1u);\n\t\tif (value == null) return;\n");
				}
				foreach (WireField field in type.Fields)
					output.Append($"\t\tWrite_{CodecName(field.Type)}(writer, {(type.Kind == "optional" ? "value.Value" : "value.@" + field.Name)});\n");
			}
			output.Append("\t}\n");
			output.Append($"\tprivate static void Validate_{codec}(ref BindingCommandReader reader)\n\t{{\n");
			if (type.Kind is "scalar" or "enum") output.Append($"\t\treader.{ScalarReader(type)}();\n");
			else if (type.Kind == "string") output.Append($"\t\treader.SkipString({type.Nullable.ToString().ToLowerInvariant()});\n");
			else if (type.Kind is "array" or "span")
				output.Append($"\t\tint count = reader.ReadCount({type.Nullable.ToString().ToLowerInvariant()}, {MinimumCommandBytes(type.Element!)});\n\t\tfor (int index = 0; index < count; ++index) Validate_{CodecName(type.Element!)}(ref reader);\n");
			else
			{
				if (type.IsClass || type.Kind == "optional")
				{
					output.Append("\t\tif (!reader.ReadPresence())\n\t\t{\n");
					output.Append(type.IsClass && !type.Nullable ? "\t\t\tthrow new InvalidOperationException(\"Required command DTO is null.\");\n" : "\t\t\treturn;\n");
					output.Append("\t\t}\n");
				}
				foreach (WireField field in type.Fields) output.Append($"\t\tValidate_{CodecName(field.Type)}(ref reader);\n");
			}
			output.Append("\t}\n");
		}
		foreach (BindingMethod method in Commands(module).Where(m => !m.NativeOnly))
		{
			string slot = ContractReader.Slot(method);
			output.Append($"\tinternal static void Encode_{slot}(BindingCommandWriter writer" + string.Concat(method.Parameters.Select(p => $", {p.Type.CsName} @{p.Name}")) + ")\n\t{\n");
			output.Append($"\t\tArgumentNullException.ThrowIfNull(writer);\n\t\twriter.RequireIdentity(Hash);\n\t\twriter.BeginRecord(Opcode_{slot});\n\t\ttry\n\t\t{{\n");
			foreach (BindingParameter parameter in method.Parameters) output.Append($"\t\t\tWrite_{CodecName(parameter.Type)}(writer, @{parameter.Name});\n");
			output.Append("\t\t\twriter.EndRecord();\n\t\t}\n\t\tcatch { writer.CancelRecord(); throw; }\n\t}\n");
		}
		output.Append("\tinternal static void Validate(ReadOnlySpan<byte> bytes)\n\t{\n\t\tvar reader = new BindingCommandReader(bytes, Hash);\n\t\twhile (reader.Next(out uint opcode))\n\t\t{\n\t\t\tswitch (opcode)\n\t\t\t{\n");
		foreach (BindingMethod method in Commands(module).Where(m => !m.NativeOnly))
		{
			output.Append($"\t\t\t\tcase Opcode_{ContractReader.Slot(method)}:\n");
			foreach (BindingParameter parameter in method.Parameters) output.Append($"\t\t\t\t\tValidate_{CodecName(parameter.Type)}(ref reader);\n");
			output.Append("\t\t\t\t\tbreak;\n");
		}
		output.Append("\t\t\t\tdefault: throw new InvalidOperationException(\"Unknown or native-only command opcode.\");\n\t\t\t}\n\t\t\treader.EndRecord();\n\t\t}\n\t}\n}\n}\n");
		foreach (var group in Commands(module).GroupBy(m => (m.CsNamespace, m.CsClass)))
		{
			if (group.Key.CsNamespace.Length != 0) output.Append($"namespace {group.Key.CsNamespace}\n{{\n");
			output.Append($"internal static partial class {group.Key.CsClass}\n{{\n");
			foreach (BindingMethod method in group)
			{
				output.Append($"\tinternal static partial void {method.Name}(global::Ursa.Bindings.BindingCommandWriter @{method.CommandWriter}" + string.Concat(method.Parameters.Select(p => $", {p.Type.CsName} @{p.Name}")) + ")\n\t{\n");
				if (method.NativeOnly) output.Append("\t\tthrow new NotSupportedException(\"This command is reserved for native execution.\");\n");
				else output.Append($"\t\tglobal::Ursa.Bindings.Generated.{owner}.Encode_{ContractReader.Slot(method)}(@{method.CommandWriter}" + string.Concat(method.Parameters.Select(p => ", @" + p.Name)) + ");\n");
				output.Append("\t}\n");
			}
			output.Append("}\n");
			if (group.Key.CsNamespace.Length != 0) output.Append("}\n");
		}
		return output.ToString();
	}

	private static string EmitNativeCommands(BindingModule module)
	{
		var output = new StringBuilder(banner).Append("#pragma once\n#include \"BindingTypes.generated.hpp\"\n#include <Scripting/Bindings/BindingCommands.hpp>\n#include <variant>\n");
		output.Append($"namespace Ursa::Scripting::Bindings::{module.Name}::Commands\n{{\ninline constexpr Array<UInt8, 32> hash{{" +
			string.Join(", ", Convert.FromHexString(module.CommandHash).Select(b => $"0x{b:x2}")) + "};\n");
		output.Append($"static_assert(bindingCommandVersion == {Ursa.Bindings.BindingCommandWriter.Version} && bindingCommandHeaderSize == {Ursa.Bindings.BindingCommandWriter.HeaderSize} && maximumCommandBytes == {Ursa.Bindings.BindingCommandWriter.MaximumBytes}, \"Command framing differs from the generated contract\");\n");
		foreach (WireType type in CommandTypes(module))
		{
			string codec = CodecName(type);
			output.Append($"inline {Cpp(type)} Read_{codec}(BindingCommandReader& reader, BindingArena& arena)\n{{\n\t(void)arena;\n");
			if (type.Kind is "scalar" or "enum")
				output.Append($"\tauto value = reader.Scalar<{Cpp(type)}>();\n");
			else if (type.Kind == "string") output.Append($"\tauto value = reader.StringValue({type.Nullable.ToString().ToLowerInvariant()});\n");
			else if (type.Kind is "array" or "span")
				output.Append($"\tauto value = reader.ArrayValue<{Cpp(type.Element!)}>(arena, {type.Nullable.ToString().ToLowerInvariant()}, {MinimumCommandBytes(type.Element!)}, [&] {{ return Read_{CodecName(type.Element!)}(reader, arena); }});\n");
			else
			{
				output.Append($"\t{Cpp(type)} value{{}};\n");
				if (type.IsClass || type.Kind == "optional")
				{
					output.Append("\tvalue.present = reader.Presence();\n\tif (!value.present)\n\t{\n");
					output.Append(type.IsClass && !type.Nullable ? "\t\tthrow std::runtime_error(\"Required command DTO is null\");\n" : "\t\treturn value;\n");
					output.Append("\t}\n");
				}
				foreach (WireField field in type.Fields) output.Append($"\tvalue.F_{field.Name} = Read_{CodecName(field.Type)}(reader, arena);\n");
			}
			output.Append($"\t{Check(type)}(value);\n\treturn value;\n}}\n");
			output.Append($"inline void Write_{codec}(BindingCommandWriter& writer, const {Cpp(type)}& value)\n{{\n\t{Check(type)}(value);\n");
			if (type.Kind is "scalar" or "enum") output.Append("\twriter.Scalar(value);\n");
			else if (type.Kind == "string") output.Append($"\twriter.StringValue(value, {type.Nullable.ToString().ToLowerInvariant()});\n");
			else if (type.Kind is "array" or "span")
				output.Append($"\twriter.Collection(value.length, value.present, {MinimumCommandBytes(type.Element!)});\n\tconst auto* items = static_cast<const {Cpp(type.Element!)}*>(value.data);\n\tfor (UInt64 index = 0; index < value.length; ++index) Write_{CodecName(type.Element!)}(writer, items[index]);\n");
			else
			{
				if (type.IsClass || type.Kind == "optional") output.Append("\twriter.Scalar(value.present);\n\tif (!value.present) return;\n");
				foreach (WireField field in type.Fields) output.Append($"\tWrite_{CodecName(field.Type)}(writer, value.F_{field.Name});\n");
			}
			output.Append("}\n");
		}
		foreach (BindingMethod method in Commands(module))
		{
			string slot = ContractReader.Slot(method);
			output.Append($"struct {slot}\n{{\n\tstatic constexpr UInt32 opcode = 0x{method.Opcode:x8}u;\n");
			foreach (BindingParameter parameter in method.Parameters) output.Append($"\t{Cpp(parameter.Type)} F_{parameter.Name}{{}};\n");
			output.Append("};\n");
			output.Append($"inline void Record_{slot}(BindingCommandWriter& writer, const {slot}& value)\n{{\n\t(void)value;\n\twriter.RequireIdentity(hash);\n\twriter.Begin({slot}::opcode);\n\ttry\n\t{{\n");
			foreach (BindingParameter parameter in method.Parameters) output.Append($"\t\tWrite_{CodecName(parameter.Type)}(writer, value.F_{parameter.Name});\n");
			output.Append("\t\twriter.End();\n\t}\n\tcatch (...) { writer.Cancel(); throw; }\n}\n");
		}
		output.Append("using Command = std::variant<" + string.Join(", ", Commands(module).Select(ContractReader.Slot)) + ">;\n");
		output.Append("inline Vector<Command> Decode(const void* bytes, Size size, BindingArena& arena, bool allowNativeOnly = false)\n{\n\t(void)arena;\n\t(void)allowNativeOnly;\n\tBindingCommandReader reader(bytes, size, hash);\n\tVector<Command> result;\n\tif (reader.RecordCount() > maximumCommandBytes / sizeof(Command)) throw std::runtime_error(\"Decoded command storage exceeds its budget\");\n\tresult.reserve(reader.RecordCount());\n\tUInt32 opcode;\n\twhile (reader.Next(opcode))\n\t{\n\t\tswitch (opcode)\n\t\t{\n");
		foreach (BindingMethod method in Commands(module))
		{
			string slot = ContractReader.Slot(method);
			output.Append($"\t\t\tcase {slot}::opcode:\n\t\t\t{{\n");
			if (method.NativeOnly) output.Append("\t\t\t\tif (!allowNativeOnly) throw std::runtime_error(\"Native-only command cannot be submitted by managed code\");\n");
			output.Append($"\t\t\t\t{slot} value;\n");
			foreach (BindingParameter parameter in method.Parameters) output.Append($"\t\t\t\tvalue.F_{parameter.Name} = Read_{CodecName(parameter.Type)}(reader, arena);\n");
			output.Append("\t\t\t\tresult.emplace_back(std::move(value));\n\t\t\t\tbreak;\n\t\t\t}\n");
		}
		output.Append("\t\t\tdefault: throw std::runtime_error(\"Unknown command opcode\");\n\t\t}\n\t\treader.End();\n\t}\n\treturn result;\n}\n}\n");
		return output.ToString();
	}
}
