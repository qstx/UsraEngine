using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.CodeAnalysis;
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace Ursa.BindingsGenerator;

internal enum BindingDirection
{
	NativeMethod,
	ManagedCallback,
	RecordedCommand
}

internal sealed record WireField(string Name, WireType Type, Location Location);
internal sealed record WireEnumMember(string Name, ulong Bits);

internal sealed class WireType(string key, string csName, string abiName, string cppName, int size, int alignment)
{
	internal string Key { get; } = key;
	internal string CsName { get; } = csName;
	internal string AbiName { get; } = abiName;
	internal string CppName { get; } = cppName;
	internal int Size { get; set; } = size;
	internal int Alignment { get; set; } = alignment;
	internal string Kind { get; set; } = "scalar";
	internal bool Nullable { get; set; }
	internal bool IsClass { get; set; }
	internal WireType? Element { get; set; }
	internal string EnumMembers { get; set; } = "";
	internal List<WireEnumMember> EnumValues { get; } = new();
	internal Location? Declaration { get; set; }
	internal string NativeEnumName => "Enum_" + new string(Key.Replace("global::", "", StringComparison.Ordinal)
		.Select(c => char.IsLetterOrDigit(c) || c == '_' ? c : '_').ToArray());
	internal List<WireField> Fields { get; } = new();
	internal bool Indirect => Kind is not ("scalar" or "enum");
	internal bool NeedsArena => Kind is "string" or "array" or "span" || Fields.Any(f => f.Type.NeedsArena);
	internal string Shape
	{
		get
		{
			int offset = IsClass || Kind == "optional" ? 4 : 0;
			var fields = new List<string>();
			foreach (WireField member in Fields)
			{
				offset = ContractReader.Align(offset, member.Type.Alignment);
				fields.Add($"{member.Name}:{member.Type.Key}@{offset}");
				offset += member.Type.Size;
			}
			return $"{Key}:{Kind}:{Size}:{Alignment}:{Nullable}:{IsClass}:" +
				(Element?.Key ?? "") + EnumMembers + string.Join(";", fields);
		}
	}
}

internal sealed record BindingParameter(string Name, WireType Type, uint HandleKind, bool Session);

internal sealed record BindingMethod(
	string Name, string Identity, string CsNamespace, string CsClass, string Target,
	BindingDirection Direction, uint Thread, bool NativeOnly, WireType Return,
	IReadOnlyList<BindingParameter> Parameters, Location Location, string? CommandWriter = null)
{
	internal uint Opcode => BinaryPrimitives.ReadUInt32LittleEndian(SHA256.HashData(Encoding.UTF8.GetBytes(Identity)));
}

internal sealed class BindingModule(string name)
{
	internal string Name { get; } = name;
	internal static ulong IdFor(string name) => BinaryPrimitives.ReadUInt64LittleEndian(SHA256.HashData(Encoding.UTF8.GetBytes(name)));
	internal ulong Id { get; } = IdFor(name);
	internal bool Required { get; set; }
	internal bool CallScoped { get; set; }
	internal List<string> Dependencies { get; } = new();
	internal List<string> Headers { get; } = new();
	internal List<BindingMethod> Methods { get; } = new();
	internal string Hash { get; set; } = "";
	internal string CommandHash { get; set; } = "";
}

internal sealed record BindingModel(IReadOnlyList<BindingModule> Modules, IReadOnlyList<WireType> Types);

internal sealed class BindingDiagnostic(string code, string message, Location? location = null) : Exception(message)
{
	internal string Code { get; } = code;
	internal Location? SourceLocation { get; } = location;
	public override string ToString()
	{
		if (SourceLocation is null || !SourceLocation.IsInSource) return $"{Code}: {Message}";
		FileLinePositionSpan span = SourceLocation.GetLineSpan();
		return $"{span.Path}({span.StartLinePosition.Line + 1},{span.StartLinePosition.Character + 1}): error {Code}: {Message}";
	}
}
