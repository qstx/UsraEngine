using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

internal sealed class Manifest
{
	public string RuntimeAssembly { get; set; } = string.Empty;
	public string RenderingAssembly { get; set; } = string.Empty;
	public string EditorAssembly { get; set; } = string.Empty;
	public string ReferenceDirectory { get; set; } = string.Empty;
	public string OutputPath { get; set; } = string.Empty;
	public string DiagnosticsPath { get; set; } = string.Empty;
	public string BindingsAssembly { get; set; } = string.Empty;
	public string LanguageVersion { get; set; } = "14.0";
	public string Nullable { get; set; } = "enable";
	public List<SourceEntry> Sources { get; set; } = new();
}

internal sealed class SourceEntry
{
	public string Path { get; set; } = string.Empty;
	public string VirtualPath { get; set; } = string.Empty;
	public string Guid { get; set; } = string.Empty;
	public string Group { get; set; } = string.Empty;
	public ScriptIdentityTable Identities { get; set; } = new();
}

internal sealed class ScriptRecord
{
	public string SourceGuid { get; set; } = string.Empty;
	public string ScriptType { get; set; } = string.Empty;
	public uint Version { get; set; }
	public ulong LocalFileId { get; set; }
	public bool LegacyIdentity { get; set; }
	public string ClrTypeName { get; set; } = string.Empty;
	public string VirtualPath { get; set; } = string.Empty;
	public string Group { get; set; } = string.Empty;
	public List<ScriptFieldRecord> Fields { get; set; } = new();
	public string Kind { get; set; } = "Behaviour";
	public bool IsRenderPipeline { get; set; }
	public string CreateAssetMenuName { get; set; } = string.Empty;
	public string CreateAssetFileName { get; set; } = string.Empty;
	public int CreateAssetMenuOrder { get; set; }
}

internal sealed class ScriptFieldRecord
{
	public string Name { get; set; } = string.Empty;
	public string Kind { get; set; } = string.Empty;
	public string ElementKind { get; set; } = string.Empty;
	public string TypeName { get; set; } = string.Empty;
	public string ElementTypeName { get; set; } = string.Empty;
	public string DefaultValueJson { get; set; } = "null";
	public List<string> EnumValues { get; set; } = new();
	public List<object> EnumNumericValues { get; set; } = new();
	public string EnumUnderlyingType { get; set; } = string.Empty;
	public List<string> FormerNames { get; set; } = new();
}

internal sealed class Catalog
{
	public uint Version { get; set; } = 1;
	public List<ScriptRecord> Scripts { get; set; } = new();
	public List<SourceMetadataRecord> SourceMetadata { get; set; } = new();
}

internal sealed class SourceMetadataRecord
{
	public string Guid { get; set; } = string.Empty;
	public string VirtualPath { get; set; } = string.Empty;
	public ScriptIdentityTable Identities { get; set; } = new();
}

internal sealed class ScriptDiagnosticRecord
{
	public string Path { get; set; } = string.Empty;
	public int Line { get; set; }
	public int Column { get; set; }
	public string Severity { get; set; } = "error";
	public string Code { get; set; } = string.Empty;
	public string Message { get; set; } = string.Empty;
}

internal static class Program
{
	private static int Main(string[] arguments)
	{
		if (arguments.Length != 1)
		{
			Console.Error.WriteLine("Usage: Ursa.ScriptCatalogGenerator <manifest.json>");
			return 2;
		}

		Manifest? activeManifest = null;
		Location? activeDeclaration = null;
		var records = new List<ScriptDiagnosticRecord>();

		try
		{
			Manifest manifest = JsonSerializer.Deserialize<Manifest>(File.ReadAllText(arguments[0]))
			                    ?? throw new InvalidDataException("Script catalog manifest is empty.");
			activeManifest = manifest;
			SaveDiagnostics(manifest, records);
			if (!File.Exists(manifest.RuntimeAssembly) || !File.Exists(manifest.EditorAssembly) ||
			    !Directory.Exists(manifest.ReferenceDirectory))
			{
				throw new FileNotFoundException("Script catalog compiler references are incomplete.");
			}

			if (!LanguageVersionFacts.TryParse(manifest.LanguageVersion, out LanguageVersion languageVersion))
				throw new InvalidDataException("Unsupported script language version.");
			var parseOptions = new CSharpParseOptions(languageVersion);
			var trees = manifest.Sources.Select(source =>
				CSharpSyntaxTree.ParseText(File.ReadAllText(source.Path), parseOptions, source.VirtualPath)).ToArray();
			var references = Directory.GetFiles(manifest.ReferenceDirectory, "*.dll")
				.Select(path => MetadataReference.CreateFromFile(path)).ToList();
			references.Add(MetadataReference.CreateFromFile(manifest.RuntimeAssembly));
			if (!string.IsNullOrEmpty(manifest.RenderingAssembly))
				references.Add(MetadataReference.CreateFromFile(manifest.RenderingAssembly));
			references.Add(MetadataReference.CreateFromFile(manifest.EditorAssembly));
			if (!string.IsNullOrEmpty(manifest.BindingsAssembly))
				references.Add(MetadataReference.CreateFromFile(manifest.BindingsAssembly));
			var compilation = CSharpCompilation.Create("Ursa.ScriptCatalog", trees, references,
				new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
					nullableContextOptions: manifest.Nullable == "enable" ? NullableContextOptions.Enable : NullableContextOptions.Disable));
			var errors = compilation.GetDiagnostics().Where(diagnostic =>
			{
				if (diagnostic.Severity != DiagnosticSeverity.Error)
				{
					return false;
				}
				if (diagnostic.Location.SourceTree == null)
				{
					return true;
				}
				SourceEntry? source = manifest.Sources.FirstOrDefault(item => item.VirtualPath == diagnostic.Location.SourceTree.FilePath);
				return source == null || source.Group != "Editor";
			}).ToArray();
			if (errors.Length != 0)
			{
				foreach (Diagnostic diagnostic in errors)
				{
					Console.Error.WriteLine(diagnostic.ToString());
					records.Add(Record(diagnostic.Location, diagnostic.Severity.ToString().ToLowerInvariant(), diagnostic.Id, diagnostic.GetMessage()));
				}
				SaveDiagnostics(manifest, records);
				return 1;
			}
			foreach (Diagnostic warning in compilation.GetDiagnostics().Where(item => item.Severity == DiagnosticSeverity.Warning))
				records.Add(Record(warning.Location, "warning", warning.Id, warning.GetMessage()));

			INamedTypeSymbol behaviorBase = compilation.GetTypeByMetadataName("Ursa.Behaviour")
			                                ?? throw new InvalidDataException("Ursa.Runtime does not define Ursa.Behaviour.");
			INamedTypeSymbol assetBase = compilation.GetTypeByMetadataName("Ursa.ScriptableObject")
			                            ?? throw new InvalidDataException("Ursa.Runtime does not define Ursa.ScriptableObject.");
			INamedTypeSymbol? pipelineBase = compilation.GetTypeByMetadataName("Ursa.Rendering.RenderPipelineAsset");
			INamedTypeSymbol? scriptTypeAttribute = compilation.GetTypeByMetadataName("Ursa.UrsaScriptTypeAttribute");
			INamedTypeSymbol? behaviorType = compilation.GetTypeByMetadataName("Ursa.Behaviour");
			INamedTypeSymbol? componentType = compilation.GetTypeByMetadataName("Ursa.Component");
			INamedTypeSymbol? serializeFieldAttribute = compilation.GetTypeByMetadataName("Ursa.SerializeFieldAttribute");
			INamedTypeSymbol? formerlySerializedAsAttribute =
				compilation.GetTypeByMetadataName("Ursa.FormerlySerializedAsAttribute");
			INamedTypeSymbol? nonSerializedAttribute = compilation.GetTypeByMetadataName("System.NonSerializedAttribute");
			INamedTypeSymbol? createAssetMenuAttribute = compilation.GetTypeByMetadataName("Ursa.CreateAssetMenuAttribute");
			var catalog = new Catalog();
			var metadata = new ScriptIdentityMetadata(manifest.Sources);
			var typeIds = new Dictionary<string, string>(StringComparer.Ordinal);
			var localIds = new Dictionary<ulong, string>();
			var assetMenuNames = new Dictionary<string, string>(StringComparer.Ordinal);
			INamedTypeSymbol? forwardPipeline = compilation.GetTypeByMetadataName("Ursa.Rendering.ForwardRenderPipelineAsset");
			if (forwardPipeline != null && scriptTypeAttribute != null)
			{
				AttributeData? identity = forwardPipeline.GetAttributes().FirstOrDefault(attribute =>
					SymbolEqualityComparer.Default.Equals(attribute.AttributeClass, scriptTypeAttribute));
				if (identity?.ConstructorArguments.FirstOrDefault().Value is string builtInId &&
				    !string.IsNullOrWhiteSpace(builtInId))
				{
					string origin = "framework type Ursa.Rendering.ForwardRenderPipelineAsset";
					ulong localFileId = MakeLocalFileId(builtInId);
					typeIds.Add(builtInId, origin);
					localIds.Add(localFileId, origin);
				}
			}
			var declaredTypes = new HashSet<ISymbol>(SymbolEqualityComparer.Default);
			foreach (SyntaxTree tree in trees)
			{
				SourceEntry source = manifest.Sources.First(item => item.VirtualPath == tree.FilePath);
				SemanticModel semanticModel = compilation.GetSemanticModel(tree);
				var root = tree.GetCompilationUnitRoot();
				INamedTypeSymbol[] sourceTypes = root.DescendantNodes().OfType<ClassDeclarationSyntax>()
					.Select(declaration => semanticModel.GetDeclaredSymbol(declaration)).OfType<INamedTypeSymbol>()
					.Where(candidate => candidate.DeclaredAccessibility == Accessibility.Public && !candidate.IsAbstract &&
						candidate.Arity == 0 && (DerivesFrom(candidate, behaviorBase) || DerivesFrom(candidate, assetBase)))
					.Distinct<INamedTypeSymbol>(SymbolEqualityComparer.Default).ToArray();
				foreach (ClassDeclarationSyntax declaration in root.DescendantNodes().OfType<ClassDeclarationSyntax>())
				{
					if (semanticModel.GetDeclaredSymbol(declaration) is not INamedTypeSymbol type ||
					    !declaredTypes.Add(type))
					{
						continue;
					}

					activeDeclaration = declaration.Identifier.GetLocation();
					AttributeData? createMenu = createAssetMenuAttribute == null
						                            ? null
						                            : type.GetAttributes().FirstOrDefault(attribute =>
							                              SymbolEqualityComparer.Default.Equals(attribute.AttributeClass,
							                                                              createAssetMenuAttribute));
					bool isAsset = DerivesFrom(type, assetBase);
					bool isBehavior = DerivesFrom(type, behaviorBase);
					bool supportedType = type.DeclaredAccessibility == Accessibility.Public && !type.IsAbstract &&
					                    type.Arity == 0 && (isBehavior || isAsset);
					if (createMenu != null && (!supportedType || !isAsset || source.Group != "Game"))
					{
						throw new InvalidDataException(
							$"CreateAssetMenu requires a public, concrete, non-generic Game ScriptableObject: {type.ToDisplayString()} in {source.VirtualPath}.");
					}
					if (!supportedType)
					{
						continue;
					}

					(string? declaredId, uint declaredVersion) = ReadScriptType(type, scriptTypeAttribute, source);
					StoredScriptIdentity identity = metadata.Resolve(source, type, sourceTypes, declaredId, declaredVersion,
						MakeLocalFileId);
					string scriptTypeId = identity.Id;
					uint version = identity.Version;
					string origin = $"{type.ToDisplayString()} in {source.VirtualPath}";
					(string menuName, string fileName, int menuOrder) = ReadCreateAssetMenu(createMenu, type, source);
					if (!string.IsNullOrEmpty(menuName) && assetMenuNames.TryGetValue(menuName, out string? previousMenu))
					{
						throw new InvalidDataException(
							$"Duplicate CreateAssetMenu '{menuName}': {previousMenu}; {origin}.");
					}
					if (!string.IsNullOrEmpty(menuName))
					{
						assetMenuNames.Add(menuName, origin);
					}
					if (typeIds.TryGetValue(scriptTypeId, out string? previousType))
					{
						throw new InvalidDataException($"Duplicate ScriptType '{scriptTypeId}': {previousType}; {origin}.");
					}
					typeIds.Add(scriptTypeId, origin);
					ulong localFileId = identity.LocalFileId;
					if (localIds.TryGetValue(localFileId, out string? previousLocalId))
					{
						throw new InvalidDataException($"Duplicate ScriptType local file ID {localFileId}: {previousLocalId}; {origin}.");
					}
					localIds.Add(localFileId, origin);
					var fields = DescribeFields(type, compilation, behaviorType, componentType, assetBase, serializeFieldAttribute,
					                            formerlySerializedAsAttribute, nonSerializedAttribute);
					if (createMenu != null && fields.Any(field => field.Kind == "Unsupported" ||
					                                         ((field.Kind is "Array" or "List") &&
					                                          field.ElementKind == "Unsupported")))
					{
						throw new InvalidDataException(
							$"CreateAssetMenu type has unsupported serialized fields: {origin}.");
					}
					catalog.Scripts.Add(new ScriptRecord
					{
						SourceGuid = source.Guid,
						ScriptType = scriptTypeId,
						Version = version,
						LocalFileId = localFileId,
						LegacyIdentity = identity.Legacy,
						ClrTypeName = type.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat) + ", " +
						              (source.Group == "Editor" ? "Ursa.EditorScripts" : "Ursa.Game"),
						VirtualPath = source.VirtualPath,
						Group = source.Group,
						Kind = isAsset ? "ScriptableObject" : "Behaviour",
						IsRenderPipeline = pipelineBase != null && DerivesFrom(type, pipelineBase),
						CreateAssetMenuName = menuName,
						CreateAssetFileName = fileName,
						CreateAssetMenuOrder = menuOrder,
						Fields = fields
					});
				}
			}

			activeDeclaration = null;
			SaveDiagnostics(manifest, records);
			catalog.Scripts.Sort((left, right) => string.CompareOrdinal(left.ScriptType, right.ScriptType));
			foreach (SourceEntry source in manifest.Sources)
			{
				source.Identities.Types.Sort((left, right) => string.CompareOrdinal(left.Id, right.Id));
				catalog.SourceMetadata.Add(new SourceMetadataRecord
				{
					Guid = source.Guid, VirtualPath = source.VirtualPath, Identities = source.Identities
				});
			}
			Directory.CreateDirectory(Path.GetDirectoryName(manifest.OutputPath)!);
			File.WriteAllText(manifest.OutputPath, JsonSerializer.Serialize(catalog, new JsonSerializerOptions { WriteIndented = true }));
			return 0;
		}
		catch (Exception exception)
		{
			Console.Error.WriteLine(exception.Message);
			if (activeManifest != null)
			{
				records.Add(Record(activeDeclaration, "error", "URSA-CATALOG", exception.Message));
				try { SaveDiagnostics(activeManifest, records); }
				catch (Exception writeError) { Console.Error.WriteLine(writeError.Message); }
			}
			return 2;
		}
	}

	private static ScriptDiagnosticRecord Record(Location? location, string severity, string code, string message)
	{
		FileLinePositionSpan? span = location?.IsInSource == true ? location.GetLineSpan() : null;
		return new ScriptDiagnosticRecord
		{
			Path = span?.Path ?? string.Empty,
			Line = span.HasValue ? span.Value.StartLinePosition.Line + 1 : 0,
			Column = span.HasValue ? span.Value.StartLinePosition.Character + 1 : 0,
			Severity = severity, Code = code, Message = message
		};
	}

	private static void SaveDiagnostics(Manifest manifest, List<ScriptDiagnosticRecord> records)
	{
		if (string.IsNullOrEmpty(manifest.DiagnosticsPath)) return;
		Directory.CreateDirectory(Path.GetDirectoryName(manifest.DiagnosticsPath)!);
		File.WriteAllText(manifest.DiagnosticsPath, JsonSerializer.Serialize(new { Version = 1, Diagnostics = records }));
	}

	private static bool DerivesFrom(INamedTypeSymbol type, INamedTypeSymbol baseType)
	{
		for (INamedTypeSymbol? current = type.BaseType; current != null; current = current.BaseType)
		{
			if (SymbolEqualityComparer.Default.Equals(current, baseType))
			{
				return true;
			}
		}
		return false;
	}

	private static (string? Id, uint Version) ReadScriptType(INamedTypeSymbol type, INamedTypeSymbol? attributeType,
		SourceEntry source)
	{
		if (attributeType != null)
		{
			foreach (AttributeData attribute in type.GetAttributes())
			{
				if (!SymbolEqualityComparer.Default.Equals(attribute.AttributeClass, attributeType))
				{
					continue;
				}
				if (attribute.ConstructorArguments.Length != 1 || attribute.ConstructorArguments[0].Value is not string id ||
				    string.IsNullOrWhiteSpace(id))
				{
					throw new InvalidDataException($"Invalid UrsaScriptType attribute on {type.Name} in {source.VirtualPath}.");
				}
				uint version = 1;
				foreach (KeyValuePair<string, TypedConstant> argument in attribute.NamedArguments)
				{
					if (argument.Key == "Version" && argument.Value.Value is uint declaredVersion)
					{
						version = declaredVersion;
					}
				}
				if (version == 0) throw new InvalidDataException($"Script data version must be positive: {type.Name}.");
				return (id, version);
			}
		}
		return (null, 1);
	}

	private static ulong MakeLocalFileId(string id)
	{
		byte[] digest = SHA256.HashData(Encoding.UTF8.GetBytes(id));
		ulong value = BitConverter.ToUInt64(digest, 0);
		return value == 0 ? 1ul : value;
	}

	private static (string MenuName, string FileName, int Order) ReadCreateAssetMenu(AttributeData? attribute,
		INamedTypeSymbol type, SourceEntry source)
	{
		if (attribute == null)
		{
			return (string.Empty, string.Empty, 0);
		}
		string menuName = type.Name;
		string fileName = "NewAsset";
		int order = 0;
		foreach (KeyValuePair<string, TypedConstant> argument in attribute.NamedArguments)
		{
			switch (argument.Key)
			{
				case "MenuName" when argument.Value.Value is string menuValue:
					menuName = menuValue.Trim();
					break;
				case "FileName" when argument.Value.Value is string fileValue:
					fileName = fileValue.Trim();
					break;
				case "Order" when argument.Value.Value is int orderValue:
					order = orderValue;
					break;
			}
		}
		if (string.IsNullOrWhiteSpace(menuName) || string.IsNullOrWhiteSpace(fileName) ||
		    fileName is "." or ".." || fileName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
		    fileName.Contains('/') || fileName.Contains('\\'))
		{
			throw new InvalidDataException($"Invalid CreateAssetMenu declaration on {type.ToDisplayString()} in {source.VirtualPath}.");
		}
		return (menuName, fileName, order);
	}

	private static List<ScriptFieldRecord> DescribeFields(INamedTypeSymbol scriptType, Compilation compilation,
		INamedTypeSymbol? behaviorBase, INamedTypeSymbol? componentBase, INamedTypeSymbol? assetBase, INamedTypeSymbol? serializeFieldAttribute,
		INamedTypeSymbol? formerNameAttribute, INamedTypeSymbol? nonSerializedAttribute)
	{
		var fields = new List<ScriptFieldRecord>();
		for (INamedTypeSymbol? current = scriptType;
		     current != null && !SymbolEqualityComparer.Default.Equals(current, behaviorBase) &&
	     !SymbolEqualityComparer.Default.Equals(current, componentBase) &&
	     !SymbolEqualityComparer.Default.Equals(current, assetBase);
		     current = current.BaseType)
		{
			foreach (IFieldSymbol field in current.GetMembers().OfType<IFieldSymbol>())
			{
				bool optedIn = serializeFieldAttribute != null && field.GetAttributes().Any(attribute =>
					SymbolEqualityComparer.Default.Equals(attribute.AttributeClass, serializeFieldAttribute));
				if (field.IsImplicitlyDeclared || (field.DeclaredAccessibility != Accessibility.Public && !optedIn) ||
				    field.IsStatic || field.IsConst || field.IsReadOnly ||
				    (nonSerializedAttribute != null && field.GetAttributes().Any(attribute =>
					 SymbolEqualityComparer.Default.Equals(attribute.AttributeClass, nonSerializedAttribute))))
				{
					continue;
				}

				(string kind, string elementKind) = FieldKind(field.Type);
				var record = new ScriptFieldRecord
				{
					Name = field.Name,
					Kind = kind,
					ElementKind = elementKind,
					TypeName = FieldTypeName(field.Type),
					DefaultValueJson = DefaultValueJson(field, compilation, kind)
				};
				ITypeSymbol scalarType = field.Type is IArrayTypeSymbol arrayField ? arrayField.ElementType :
					field.Type is INamedTypeSymbol listField && kind == "List" ? listField.TypeArguments[0] : field.Type;
				record.ElementTypeName = kind is "Array" or "List" ? FieldTypeName(scalarType) : string.Empty;
				if (scalarType.TypeKind == TypeKind.Enum)
				{
					record.EnumUnderlyingType = FieldTypeName(((INamedTypeSymbol)scalarType).EnumUnderlyingType!);
					record.EnumNumericValues = scalarType.GetMembers().OfType<IFieldSymbol>()
						.Where(member => member.HasConstantValue && !member.IsImplicitlyDeclared)
						.Select(member => member.ConstantValue!).ToList();
					record.EnumValues = scalarType.GetMembers().OfType<IFieldSymbol>()
						.Where(member => member.HasConstantValue && !member.IsImplicitlyDeclared)
						.Select(member => member.Name).ToList();
				}
				if (formerNameAttribute != null)
				{
					record.FormerNames = field.GetAttributes()
						.Where(attribute => SymbolEqualityComparer.Default.Equals(attribute.AttributeClass, formerNameAttribute) &&
						                    attribute.ConstructorArguments.Length == 1 &&
						                    attribute.ConstructorArguments[0].Value is string)
						.Select(attribute => (string)attribute.ConstructorArguments[0].Value!)
						.Distinct(StringComparer.Ordinal).ToList();
				}
				fields.Add(record);
			}
		}
		fields.Sort((left, right) => string.CompareOrdinal(left.Name, right.Name));
		return fields;
	}

	private static string FieldTypeName(ITypeSymbol type)
	{
		if (type is IArrayTypeSymbol array) return FieldTypeName(array.ElementType) + "[]";
		if (type is INamedTypeSymbol named && !named.IsGenericType)
		{
			string prefix = named.ContainingType != null ? FieldTypeName(named.ContainingType) + "+" :
				named.ContainingNamespace.IsGlobalNamespace ? string.Empty : named.ContainingNamespace.ToDisplayString() + ".";
			return prefix + named.MetadataName;
		}
		return type.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat);
	}

	private static (string Kind, string ElementKind) FieldKind(ITypeSymbol type)
	{
		if (type is IArrayTypeSymbol array)
		{
			if (array.Rank != 1 || array.ElementType is IArrayTypeSymbol)
			{
				return ("Unsupported", "Unsupported");
			}
			return ("Array", ScalarFieldKind(array.ElementType));
		}
		if (type is INamedTypeSymbol named && named.IsGenericType &&
		    named.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.List<T>")
		{
			return ("List", ScalarFieldKind(named.TypeArguments[0]));
		}
		return (ScalarFieldKind(type), "Unsupported");
	}

	private static string ScalarFieldKind(ITypeSymbol type)
	{
		if (type.SpecialType == SpecialType.System_Boolean) return "Boolean";
		if (type.SpecialType is SpecialType.System_SByte or SpecialType.System_Int16 or SpecialType.System_Int32 or
		    SpecialType.System_Int64) return "SignedInteger";
		if (type.SpecialType is SpecialType.System_Byte or SpecialType.System_UInt16 or SpecialType.System_UInt32 or
		    SpecialType.System_UInt64) return "UnsignedInteger";
		if (type.SpecialType is SpecialType.System_Single or SpecialType.System_Double) return "FloatingPoint";
		if (type.SpecialType == SpecialType.System_String) return "String";
		if (type.TypeKind == TypeKind.Enum) return "Enumeration";
		for (INamedTypeSymbol? current = type as INamedTypeSymbol; current != null; current = current.BaseType)
		{
			string identity = FieldTypeName(current);
			if (identity == "Ursa.Component") return "ComponentReference";
			if (identity == "Ursa.ScriptableObject") return "ScriptableObjectReference";
			if (identity == "Ursa.Material") return "MaterialReference";
			if (identity == "Ursa.Texture") return "TextureReference";
		}
		if (type.ContainingNamespace.ToDisplayString() != "Ursa") return "Unsupported";
		return type.Name switch
		{
			"Vector2" => "Vector2",
			"Vector3" => "Vector3",
			"Vector4" => "Vector4",
			"Mesh" => "MeshReference",
			"Quaternion" => "Quaternion",
			"Color" => "Color",
			"AssetReference" => "AssetReference",
			"Shader" => "ShaderReference",
			"ComputeShader" => "ComputeShaderReference",
			"GameObject" => "GameObjectReference",
			"Component" or "Behaviour" => "ComponentReference",
			_ => "Unsupported"
		};
	}

	private static string DefaultValueJson(IFieldSymbol field, Compilation compilation, string kind)
	{
		foreach (SyntaxReference reference in field.DeclaringSyntaxReferences)
		{
			if (reference.GetSyntax() is not VariableDeclaratorSyntax variable || variable.Initializer == null)
			{
				continue;
			}
			SemanticModel model = compilation.GetSemanticModel(variable.SyntaxTree);
			ExpressionSyntax initializer = variable.Initializer.Value;
			if (kind == "Enumeration")
			{
				ISymbol? symbol = model.GetSymbolInfo(initializer).Symbol;
				if (symbol is IFieldSymbol enumMember && enumMember.ContainingType.TypeKind == TypeKind.Enum)
				{
					return JsonSerializer.Serialize(enumMember.ConstantValue);
				}
			}
			Optional<object?> constant = model.GetConstantValue(initializer);
			if (constant.HasValue)
			{
				return JsonSerializer.Serialize(constant.Value);
			}
			if (kind is "Vector2" or "Vector3" or "Vector4" or "Quaternion" or "Color")
			{
				IEnumerable<ArgumentSyntax>? arguments = initializer switch
				{
					ObjectCreationExpressionSyntax creation => creation.ArgumentList?.Arguments,
					ImplicitObjectCreationExpressionSyntax creation => creation.ArgumentList.Arguments,
					_ => null
				};
				if (arguments != null)
				{
					var values = arguments.Select(argument => model.GetConstantValue(argument.Expression))
						.Select(value => value.HasValue ? value.Value : null).ToList();
					while (values.Count < 4)
					{
						values.Add(kind == "Quaternion" && values.Count == 3 || kind == "Color" && values.Count == 3 ? 1.0 : 0.0);
					}
					int componentCount = kind == "Vector2" ? 2 : kind == "Vector3" ? 3 : 4;
					if (values.Count != componentCount) values.RemoveRange(componentCount, values.Count - componentCount);
					return JsonSerializer.Serialize(values);
				}
			}
			break;
		}
		return kind switch
		{
			"Boolean" => "false",
			"SignedInteger" or "UnsignedInteger" or "FloatingPoint" => "0",
			"String" or "GameObjectReference" or "ComponentReference" or "AssetReference" => "null",
			"Vector2" => "[0,0]",
			"Vector3" => "[0,0,0]",
			"Vector4" or "Quaternion" or "Color" => "[0,0,0,0]",
			"Enumeration" => "0",
			"Array" or "List" => "[]",
			_ => "null"
		};
	}
}
