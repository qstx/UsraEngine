using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Ursa.BindingsGenerator;

internal sealed class ContractReader
{
	private readonly Dictionary<string, WireType> types = new(StringComparer.Ordinal);
	private readonly HashSet<string> resolving = new(StringComparer.Ordinal);
	private static readonly SymbolDisplayFormat csFormat = SymbolDisplayFormat.FullyQualifiedFormat
		.WithMiscellaneousOptions(SymbolDisplayMiscellaneousOptions.UseSpecialTypes |
			SymbolDisplayMiscellaneousOptions.IncludeNullableReferenceTypeModifier |
			SymbolDisplayMiscellaneousOptions.EscapeKeywordIdentifiers);

	internal static BindingModel Read(string sourceRoot, IEnumerable<string>? additionalSources = null)
	{
		var reader = new ContractReader();
		string[] files = Directory.EnumerateFiles(sourceRoot, "*.cs", SearchOption.AllDirectories)
			.Where(p => !Path.GetRelativePath(sourceRoot, p).Replace('\\', '/').Split('/').Any(s =>
				s is "bin" or "obj" or "Generated" or "Ursa.BindingsGenerator" or "Ursa.ScriptCatalogGenerator"))
			.Concat(additionalSources ?? Array.Empty<string>()).Select(Path.GetFullPath)
			.Distinct(StringComparer.OrdinalIgnoreCase)
			.OrderBy(p => Path.GetRelativePath(sourceRoot, p), StringComparer.Ordinal).ToArray();
		var trees = files.Select(p => CSharpSyntaxTree.ParseText(File.ReadAllText(p),
			new CSharpParseOptions(LanguageVersion.Preview), Path.GetRelativePath(sourceRoot, p).Replace('\\', '/'))).ToArray();
		foreach (SyntaxTree tree in trees)
		{
			Diagnostic? syntaxError = tree.GetDiagnostics().FirstOrDefault(d => d.Severity == DiagnosticSeverity.Error);
			if (syntaxError != null) throw new BindingDiagnostic("UBG0002", syntaxError.GetMessage(), syntaxError.Location);
		}
		string[] references = ((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? "").Split(Path.PathSeparator);
		var compilation = CSharpCompilation.Create("Ursa.BindingContracts", trees,
			references.Append(typeof(Ursa.Bindings.BindingModuleAttribute).Assembly.Location).Where(File.Exists)
				.Distinct(StringComparer.OrdinalIgnoreCase).Select(p => MetadataReference.CreateFromFile(p)),
			new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: true,
				nullableContextOptions: NullableContextOptions.Enable));
		var modules = new Dictionary<string, BindingModule>(StringComparer.Ordinal);
		var identities = new HashSet<string>(StringComparer.Ordinal);
		foreach (SyntaxTree tree in trees)
		{
			if (!tree.FilePath.EndsWith(".bindings.cs", StringComparison.Ordinal)) continue;
			SemanticModel semantic = compilation.GetSemanticModel(tree);
			foreach (ClassDeclarationSyntax declaration in tree.GetRoot().DescendantNodes().OfType<ClassDeclarationSyntax>())
			{
				INamedTypeSymbol symbol = semantic.GetDeclaredSymbol(declaration)!;
				AttributeData? moduleAttribute = Attribute(symbol, "BindingModule");
				if (moduleAttribute == null) continue;
				if (!symbol.IsStatic || symbol.IsGenericType || symbol.ContainingType != null ||
					!declaration.Modifiers.Any(SyntaxKind.PartialKeyword))
					throw Error("UBG0100", "A binding class must be top-level, static, non-generic and partial.", symbol);
				string moduleName = Text(moduleAttribute, symbol);
				if (!IsIdentifier(moduleName)) throw Error("UBG0100", "Module names must be C# identifiers.", symbol);
				if (!modules.TryGetValue(moduleName, out BindingModule? module))
					modules.Add(moduleName, module = new BindingModule(moduleName));
				foreach (var named in moduleAttribute.NamedArguments)
				{
					if (named.Key == "Required") module.Required |= named.Value.Value is true;
					if (named.Key == "CallScoped") module.CallScoped |= named.Value.Value is true;
					if (named.Key == "Dependencies")
						foreach (TypedConstant dependency in named.Value.Values)
							if (dependency.Value is string name && !module.Dependencies.Contains(name)) module.Dependencies.Add(name);
				}
				if (module.Required && module.CallScoped)
					throw Error("UBG0112", "A required module cannot be call scoped.", symbol);
				AttributeData? header = Attribute(symbol, "NativeHeader");
				if (header != null)
				{
					string path = Text(header, symbol);
					if (path.Contains('"') || path.Contains('\n') || path.Contains('\r') || Path.IsPathRooted(path) ||
						path.Replace('\\', '/').Split('/').Contains(".."))
						throw Error("UBG0101", "NativeHeader must be a relative include path.", symbol);
					if (!module.Headers.Contains(path)) module.Headers.Add(path);
				}
				foreach (MethodDeclarationSyntax methodSyntax in declaration.Members.OfType<MethodDeclarationSyntax>())
				{
					IMethodSymbol method = semantic.GetDeclaredSymbol(methodSyntax)!;
					AttributeData? native = Attribute(method, "NativeMethod"), callback = Attribute(method, "ManagedCallback"),
						command = Attribute(method, "RecordedCommand");
					int count = (native != null ? 1 : 0) + (callback != null ? 1 : 0) + (command != null ? 1 : 0);
					if (count == 0) throw Error("UBG0102", "Each binding declaration needs exactly one direction attribute.", method);
					if (count != 1 || !method.IsStatic || method.IsGenericMethod || !method.IsPartialDefinition ||
						method.DeclaredAccessibility != Accessibility.Internal || methodSyntax.Body != null || methodSyntax.ExpressionBody != null)
						throw Error("UBG0102", "A binding must be an internal static partial method with exactly one direction.", method);
					if (method.ReturnsByRef || method.ReturnsByRefReadonly || method.Parameters.Any(p => p.RefKind != RefKind.None || p.IsParams || p.IsOptional))
						throw Error("UBG0103", "Use explicit return DTOs instead of ref/out/params/optional ABI parameters.", method);
					string identity = method.ContainingType.ToDisplayString() + "." + method.Name;
					if (!identities.Add(identity)) throw Error("UBG0104", $"Duplicate binding identity '{identity}'; give overloads distinct binding names.", method);
					BindingDirection direction = native != null ? BindingDirection.NativeMethod : callback != null
						? BindingDirection.ManagedCallback : BindingDirection.RecordedCommand;
					AttributeData primary = native ?? callback ?? command!;
					string target = direction == BindingDirection.RecordedCommand ? "" : Text(primary, method);
					if (direction == BindingDirection.NativeMethod && (header == null || !ValidTarget(target, "::")))
						throw Error("UBG0105", "Native methods require NativeHeader and a qualified C++ target.", method);
					if (direction == BindingDirection.ManagedCallback && !ValidTarget(target, "."))
						throw Error("UBG0105", "Callbacks require a qualified C# static target.", method);
					uint thread = 0;
					bool nativeOnly = false;
					foreach (var named in primary.NamedArguments)
					{
						if (named.Key == "Thread") thread = Convert.ToUInt32(named.Value.Value);
						if (named.Key == "NativeOnly") nativeOnly = named.Value.Value is true;
					}
					if (thread > 1) throw Error("UBG0106", "Unknown thread policy.", method);
					WireType returnType = reader.Resolve(method.ReturnType, method.Locations.First());
					if (returnType.Kind == "span") throw Error("UBG0200", "Borrowed spans cannot be returned.", method);
					if (direction == BindingDirection.RecordedCommand && returnType.Key != "void")
						throw Error("UBG0107", "Recorded commands must return void.", method);
					var parameters = new List<BindingParameter>();
					string? commandWriter = null;
					if (direction == BindingDirection.RecordedCommand)
					{
						if (method.Parameters.Length == 0 || method.Parameters[0].Type.ToDisplayString() != "Ursa.Bindings.BindingCommandWriter")
							throw Error("UBG0113", "A recorded command requires BindingCommandWriter as its first parameter.", method);
						commandWriter = method.Parameters[0].Name;
					}
					foreach (IParameterSymbol parameter in method.Parameters.Skip(commandWriter == null ? 0 : 1))
					{
						WireType type = reader.Resolve(parameter.Type, parameter.Locations.First());
						AttributeData? kind = Attribute(parameter, "HandleKind");
						uint handleKind = kind == null ? 0 : Convert.ToUInt32(kind.ConstructorArguments[0].Value);
						if (kind != null && (handleKind == 0 || type.Kind != "struct"))
							throw Error("UBG0108", "HandleKind requires a nonzero kind and a wire value handle.", parameter);
						bool session = Attribute(parameter, "BindingSession") != null;
						if (session && (direction != BindingDirection.ManagedCallback || type.Key != "ulong" || parameters.Any(p => p.Session)))
							throw Error("UBG0109", "BindingSession requires one ulong parameter on a managed callback.", parameter);
						parameters.Add(new(parameter.Name, type, handleKind, session));
					}
					module.Methods.Add(new(method.Name, identity, symbol.ContainingNamespace.ToDisplayString(), symbol.Name,
						target, direction, thread, nativeOnly, returnType, parameters, method.Locations.First(), commandWriter));
				}
			}
		}
		if (modules.Count == 0) throw new BindingDiagnostic("UBG0100", "No binding modules were found in .bindings.cs files.");
		var visited = new HashSet<string>(StringComparer.Ordinal);
		var visiting = new HashSet<string>(StringComparer.Ordinal);
		void Visit(string name)
		{
			if (!modules.TryGetValue(name, out BindingModule? module)) throw new BindingDiagnostic("UBG0110", $"Missing module dependency '{name}'.");
			if (visited.Contains(name)) return;
			if (!visiting.Add(name)) throw new BindingDiagnostic("UBG0110", $"Cyclic module dependency at '{name}'.");
			foreach (string dependency in module.Dependencies) Visit(dependency);
			if (name is "Runtime" or "Rendering" && visited.Contains("Editor") && DependsOnEditor(name, modules, new()))
				throw new BindingDiagnostic("UBG0111", $"{name} cannot depend on Editor.");
			visiting.Remove(name);
			visited.Add(name);
		}
		foreach (string name in modules.Keys.Order(StringComparer.Ordinal)) Visit(name);
		WireType[] orderedTypes = reader.types.Values.OrderBy(t => t.Key, StringComparer.Ordinal).ToArray();
		var typeCollision = orderedTypes.Where(t => t.Kind is "struct" or "optional" or "archive" or "enum")
			.GroupBy(t => t.Kind == "enum" ? t.NativeEnumName : t.AbiName, StringComparer.Ordinal).FirstOrDefault(g => g.Count() > 1);
		if (typeCollision != null)
			throw new BindingDiagnostic("UBG0202", "Wire type names collide: " + string.Join(", ", typeCollision.Select(t => t.Key)),
				typeCollision.First().Declaration ?? typeCollision.First().Fields.FirstOrDefault()?.Location);
		foreach (BindingModule module in modules.Values)
		{
			var slotCollision = module.Methods.GroupBy(Slot, StringComparer.Ordinal).FirstOrDefault(g => g.Count() > 1);
			if (slotCollision != null)
				throw new BindingDiagnostic("UBG0104", "Binding slots collide: " + string.Join(", ", slotCollision.Select(m => m.Identity)), slotCollision.First().Location);
			module.Dependencies.Sort(StringComparer.Ordinal);
			module.Headers.Sort(StringComparer.Ordinal);
			module.Methods.Sort((a, b) => StringComparer.Ordinal.Compare(a.Identity, b.Identity));
			var used = new HashSet<WireType>();
			void Collect(WireType type)
			{
				if (!used.Add(type)) return;
				if (type.Element != null) Collect(type.Element);
				foreach (WireField field in type.Fields) Collect(field.Type);
			}
			foreach (BindingMethod method in module.Methods)
			{
				Collect(method.Return);
				foreach (BindingParameter parameter in method.Parameters) Collect(parameter.Type);
			}
			string canonical = "Ursa.Bindings.v1|win-x64|cdecl|le\n" + BuiltinAbi.Identity + "\n" +
				$"{module.Name}:{module.Id}:{module.Required}:{module.CallScoped}\n" +
				string.Join("\n", module.Dependencies) + "\n" + string.Join("\n", used.OrderBy(t => t.Key, StringComparer.Ordinal).Select(t => t.Shape)) +
				"\n" + string.Join("\n", module.Methods.Select(m => $"{m.Identity}|{m.Direction}|{m.Target}|{m.Thread}|{m.NativeOnly}|{m.Return.Key}|" +
					string.Join(",", m.Parameters.Select(p => $"{p.Type.Key}:{p.HandleKind}:{p.Session}"))));
			module.Hash = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(canonical)));
			BindingMethod[] commands = module.Methods.Where(m => m.Direction == BindingDirection.RecordedCommand).ToArray();
			if (commands.Length != 0)
			{
				var collision = commands.GroupBy(m => m.Opcode).FirstOrDefault(g => g.Count() > 1);
				if (collision != null) throw new BindingDiagnostic("UBG0114", "Command opcode identity collision.", collision.First().Location);
				used.Clear();
				foreach (BindingMethod command in commands)
					foreach (BindingParameter parameter in command.Parameters) Collect(parameter.Type);
				if (used.Any(t => t.Kind == "archive"))
					throw new BindingDiagnostic("UBG0115", "Recorded commands cannot contain serialization archives.", commands[0].Location);
				string commandShape = $"Ursa.Commands.v1:{Ursa.Bindings.BindingCommandWriter.Version}:{Ursa.Bindings.BindingCommandWriter.HeaderSize}:{Ursa.Bindings.BindingCommandWriter.MaximumBytes}\n{module.Name}\n" +
					string.Join("\n", used.OrderBy(t => t.Key, StringComparer.Ordinal).Select(t => t.Shape)) + "\n" +
					string.Join("\n", commands.Select(m => $"{m.Identity}:{m.Opcode}:{m.NativeOnly}|" + string.Join(",", m.Parameters.Select(p => $"{p.Name}:{p.Type.Key}:{p.HandleKind}"))));
				module.CommandHash = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(commandShape)));
			}
		}
		return new(modules.Values.OrderBy(m => m.Name, StringComparer.Ordinal).ToArray(), orderedTypes);
	}

	private static bool DependsOnEditor(string name, Dictionary<string, BindingModule> modules, HashSet<string> seen) =>
		seen.Add(name) && (name == "Editor" || modules[name].Dependencies.Any(d => DependsOnEditor(d, modules, seen)));

	private WireType Resolve(ITypeSymbol symbol, Location location)
	{
		string csName = symbol.ToDisplayString(csFormat), key = csName;
		if (types.TryGetValue(key, out WireType? existing)) return existing;
		if (!resolving.Add(key)) throw new BindingDiagnostic("UBG0201", $"Recursive wire type '{key}' needs a serialized archive instead.", location);
		try
		{
			WireType type;
			(int size, string cs, string cpp) = symbol.SpecialType switch
			{
				SpecialType.System_Void => (0, "void", "void"),
				SpecialType.System_Boolean => (4, "uint", "UInt32"),
				SpecialType.System_Byte => (1, "byte", "UInt8"),
				SpecialType.System_SByte => (1, "sbyte", "Int8"),
				SpecialType.System_Int16 => (2, "short", "Int16"),
				SpecialType.System_UInt16 => (2, "ushort", "UInt16"),
				SpecialType.System_Int32 => (4, "int", "Int32"),
				SpecialType.System_UInt32 => (4, "uint", "UInt32"),
				SpecialType.System_Int64 => (8, "long", "Int64"),
				SpecialType.System_UInt64 => (8, "ulong", "UInt64"),
				SpecialType.System_Single => (4, "float", "Float32"),
				SpecialType.System_Double => (8, "double", "Float64"),
				_ => (-1, "", "")
			};
			if (size >= 0) type = new(key, csName, cs, cpp, size, Math.Max(size, 1));
			else if (symbol.SpecialType == SpecialType.System_String)
				type = new(key, csName, "Utf8View", "Utf8View", 24, 8) { Kind = "string", Nullable = symbol.NullableAnnotation == NullableAnnotation.Annotated };
			else if (symbol is IArrayTypeSymbol array && array.Rank == 1)
				type = new(key, csName, "DataView", "DataView", 24, 8) { Kind = "array", Nullable = symbol.NullableAnnotation == NullableAnnotation.Annotated,
					Element = Resolve(array.ElementType, location) };
			else if (symbol is INamedTypeSymbol span && span.Name == "ReadOnlySpan" && span.ContainingNamespace.ToDisplayString() == "System" && span.TypeArguments.Length == 1)
				type = new(key, csName, "DataView", "DataView", 24, 8) { Kind = "span", Element = Resolve(span.TypeArguments[0], location) };
			else if (symbol is INamedTypeSymbol optional && optional.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
			{
				WireType element = Resolve(optional.TypeArguments[0], location);
				type = new(key, csName, WireName(key), WireName(key), 0, Math.Max(4, element.Alignment)) { Kind = "optional", Element = element };
				type.Fields.Add(new("value", element, location));
				type.Size = Align(Align(4, element.Alignment) + element.Size, type.Alignment);
			}
			else if (symbol is INamedTypeSymbol declared && (Attribute(declared, "WireValue") != null || Attribute(declared, "SerializedArchive") != null))
			{
				if (declared.IsGenericType || declared.ContainingType != null || declared.TypeKind is not (TypeKind.Struct or TypeKind.Class or TypeKind.Enum))
					throw new BindingDiagnostic("UBG0200", $"Unsupported declared wire type '{key}'.", location);
				if (declared.TypeKind == TypeKind.Enum)
				{
					WireType underlying = Resolve(declared.EnumUnderlyingType!, location);
					type = new(key, csName, underlying.AbiName, underlying.CppName, underlying.Size, underlying.Alignment)
					{
						Kind = "enum", Element = underlying, Declaration = location,
						EnumMembers = string.Join(";", declared.GetMembers().OfType<IFieldSymbol>().Where(f => f.HasConstantValue)
							.OrderBy(f => f.Name, StringComparer.Ordinal).Select(f => $"{f.Name}={f.ConstantValue}"))
					};
					foreach (IFieldSymbol member in declared.GetMembers().OfType<IFieldSymbol>().Where(f => f.HasConstantValue).OrderBy(f => f.Name, StringComparer.Ordinal))
					{
						bool unsigned = declared.EnumUnderlyingType!.SpecialType is SpecialType.System_Byte or SpecialType.System_UInt16 or SpecialType.System_UInt32 or SpecialType.System_UInt64;
						ulong bits = unsigned ? Convert.ToUInt64(member.ConstantValue) : unchecked((ulong)Convert.ToInt64(member.ConstantValue));
						type.EnumValues.Add(new(member.Name, bits));
					}
				}
				else
				{
					type = new(key, csName, WireName(key), WireName(key), 0, declared.IsReferenceType ? 4 : 1) { Kind = Attribute(declared, "SerializedArchive") != null ? "archive" : "struct",
						IsClass = declared.IsReferenceType, Nullable = symbol.NullableAnnotation == NullableAnnotation.Annotated };
					if (declared.IsReferenceType && (declared.IsAbstract || !declared.InstanceConstructors.Any(c => c.Parameters.Length == 0)))
						throw new BindingDiagnostic("UBG0200", $"Wire DTO '{key}' must have a parameterless constructor.", location);
					foreach (ISymbol member in declared.GetMembers().Where(m => !m.IsStatic && m.DeclaredAccessibility == Accessibility.Public && Attribute(m, "WireIgnore") == null)
						.OrderBy(m => m.Locations.FirstOrDefault()?.SourceTree?.FilePath ?? "", StringComparer.Ordinal)
						.ThenBy(m => m.Locations.FirstOrDefault()?.SourceSpan.Start ?? 0))
					{
						ITypeSymbol? memberType = member switch { IFieldSymbol field when !field.IsReadOnly => field.Type,
							IPropertySymbol property when property.GetMethod != null && property.SetMethod != null && !property.IsIndexer => property.Type, _ => null };
						if (memberType != null)
						{
							WireType fieldType = Resolve(memberType, member.Locations.First());
							if (fieldType.Kind == "span") throw Error("UBG0200", "A borrowed span cannot be stored in a wire DTO.", member);
							type.Fields.Add(new(member.Name, fieldType, member.Locations.First()));
						}
					}
					if (type.Fields.Count == 0) throw new BindingDiagnostic("UBG0200", $"Wire DTO '{key}' has no supported writable members.", location);
					int offset = type.IsClass ? 4 : 0;
					foreach (WireField field in type.Fields)
					{
						type.Alignment = Math.Max(type.Alignment, field.Type.Alignment);
						offset = Align(offset, field.Type.Alignment) + field.Type.Size;
					}
					type.Size = Align(offset, type.Alignment);
				}
			}
			else throw new BindingDiagnostic("UBG0200", $"Unsupported binding type '{key}'; declare a WireValue DTO or an explicit archive.", location);
			types.Add(key, type);
			return type;
		}
		finally { resolving.Remove(key); }
	}

	internal static int Align(int offset, int alignment) => checked((offset + alignment - 1) / alignment * alignment);
	internal static string Name(string key) => "Wire_" + Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(key)))[..16];
	private static string WireName(string key) => "Value_" + new string(key.Replace("global::", "", StringComparison.Ordinal)
		.Select(c => char.IsLetterOrDigit(c) || c == '_' ? c : '_').ToArray());
	internal static string Slot(BindingMethod method) => method.CsClass + "_" + method.Name;
	private static bool IsIdentifier(string value) => SyntaxFacts.IsValidIdentifier(value) && SyntaxFacts.GetKeywordKind(value) == SyntaxKind.None;
	private static bool ValidTarget(string value, string separator) => value.Split(separator, StringSplitOptions.None).All(IsIdentifier);
	private static AttributeData? Attribute(ISymbol symbol, string name) => symbol.GetAttributes().FirstOrDefault(a =>
		a.AttributeClass?.ToDisplayString() == "Ursa.Bindings." + name + "Attribute");
	private static string Text(AttributeData attribute, ISymbol symbol) => attribute.ConstructorArguments.FirstOrDefault().Value is string value && !string.IsNullOrWhiteSpace(value)
		? value : throw Error("UBG0100", "Attribute requires a nonempty string.", symbol);
	private static BindingDiagnostic Error(string code, string message, ISymbol symbol) => new(code, message, symbol.Locations.FirstOrDefault());
}
