using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Ursa;

[AttributeUsage(AttributeTargets.Field, Inherited = true, AllowMultiple = false)]
public sealed class SerializeFieldAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Field, Inherited = false, AllowMultiple = true)]
public sealed class FormerlySerializedAsAttribute : Attribute
{
	public FormerlySerializedAsAttribute(string oldName)
	{
		OldName = string.IsNullOrWhiteSpace(oldName)
		             ? throw new ArgumentException("Former field name is required.", nameof(oldName))
		             : oldName;
	}

	public string OldName { get; }
}

public enum ScriptSerializedFieldKind
{
	Unsupported,
	Boolean,
	SignedInteger,
	UnsignedInteger,
	FloatingPoint,
	String,
	Enumeration,
	Vector3,
	Quaternion,
	Color,
	GameObjectReference,
	ComponentReference,
	AssetReference,
	Array,
	List
}

public sealed class ScriptFieldDescriptor
{
	internal ScriptFieldDescriptor(FieldInfo field, ScriptSerializedFieldKind kind, ScriptSerializedFieldKind elementKind,
	                              string diagnostic, IReadOnlyList<string> formerNames)
	{
		Field = field;
		Name = field.Name;
		ValueType = field.FieldType;
		Kind = kind;
		ElementKind = elementKind;
		Diagnostic = diagnostic;
		FormerNames = formerNames;
	}

	public FieldInfo Field { get; }

	public string Name { get; }

	public Type ValueType { get; }

	public ScriptSerializedFieldKind Kind { get; }

	public ScriptSerializedFieldKind ElementKind { get; }

	public bool IsSupported => Kind != ScriptSerializedFieldKind.Unsupported &&
	                           (Kind is not (ScriptSerializedFieldKind.Array or ScriptSerializedFieldKind.List) ||
	                            ElementKind != ScriptSerializedFieldKind.Unsupported);

	public string Diagnostic { get; }

	public IReadOnlyList<string> FormerNames { get; }
}

public static class ScriptFieldSchema
{
	private sealed class Payload
	{
		public uint FormatVersion { get; set; } = 1;
		public string ScriptType { get; set; } = string.Empty;
		public string SourceGuid { get; set; } = string.Empty;
		public ulong LocalFileId { get; set; }
		public uint Version { get; set; } = 1;
		public Dictionary<string, JsonElement> Fields { get; set; } = new(StringComparer.Ordinal);
		public Dictionary<string, string[]> FormerFieldNames { get; set; } = new(StringComparer.Ordinal);
		public Dictionary<string, FieldTypePayload> FieldTypes { get; set; } = new(StringComparer.Ordinal);
	}

	private sealed class FieldTypePayload
	{
		public string Kind { get; set; } = string.Empty;
		public string ElementKind { get; set; } = string.Empty;
		public string TypeName { get; set; } = string.Empty;
		public string ElementTypeName { get; set; } = string.Empty;
		public string[] EnumValues { get; set; } = Array.Empty<string>();
		public object[] EnumNumericValues { get; set; } = Array.Empty<object>();
		public string EnumUnderlyingType { get; set; } = string.Empty;
	}

	private sealed class ReferencePayload
	{
		public string Kind { get; set; } = string.Empty;
		public string Id { get; set; } = string.Empty;
	}

	// Do not retain serializer metadata for collectible project Types in the shared API.
	private static JsonSerializerOptions jsonOptions => CreateJsonOptions();
	private static readonly ConditionalWeakTable<Behaviour, List<Action>> pendingReferences = new();

	public static string Serialize(Behaviour behaviour)
	{
		if (behaviour == null)
		{
			throw new ArgumentNullException(nameof(behaviour));
		}
		Type scriptType = behaviour.GetType();
		ScriptIdentity identity = BehaviourRegistry.IdentityFor(scriptType);
		var payload = new Payload { ScriptType = identity.Id, Version = identity.Version,
			SourceGuid = identity.SourceGuid, LocalFileId = identity.LocalFileId };
		foreach (ScriptFieldDescriptor descriptor in Describe(scriptType))
		{
			if (!descriptor.IsSupported)
			{
				throw new InvalidOperationException(descriptor.Diagnostic);
			}
			object? value = descriptor.Field.GetValue(behaviour);
			payload.Fields.Add(descriptor.Name, SerializeFieldValue(descriptor, value));
			Type scalarType = descriptor.Kind == ScriptSerializedFieldKind.Array ? descriptor.ValueType.GetElementType()! :
				descriptor.Kind == ScriptSerializedFieldKind.List ? descriptor.ValueType.GetGenericArguments()[0] : descriptor.ValueType;
			payload.FieldTypes.Add(descriptor.Name, new FieldTypePayload
			{
				Kind = descriptor.Kind.ToString(),
				ElementKind = descriptor.ElementKind.ToString(),
				TypeName = descriptor.ValueType.FullName ?? descriptor.ValueType.Name,
				ElementTypeName = scalarType == descriptor.ValueType ? string.Empty : scalarType.FullName ?? scalarType.Name,
				EnumValues = scalarType.IsEnum ? Enum.GetNames(scalarType) : Array.Empty<string>(),
				EnumNumericValues = scalarType.IsEnum ? Enum.GetValues(scalarType).Cast<object>()
					.Select(value => Convert.ChangeType(value, Enum.GetUnderlyingType(scalarType))).ToArray() : Array.Empty<object>(),
				EnumUnderlyingType = scalarType.IsEnum ? Enum.GetUnderlyingType(scalarType).FullName! : string.Empty
			});
			if (descriptor.FormerNames.Count != 0)
			{
				payload.FormerFieldNames.Add(descriptor.Name, descriptor.FormerNames.ToArray());
			}
		}
		return JsonSerializer.Serialize(payload, jsonOptions);
	}

	public static void Deserialize(Behaviour behaviour, string serializedPayload)
	{
		if (behaviour == null)
		{
			throw new ArgumentNullException(nameof(behaviour));
		}
		using JsonDocument document = JsonDocument.Parse(serializedPayload);
		Payload payload = document.RootElement.Deserialize<Payload>(jsonOptions) ??
		                 throw new InvalidOperationException("Script component payload is empty.");
		Type scriptType = behaviour.GetType();
		ScriptIdentity identity = BehaviourRegistry.IdentityFor(scriptType);
		if (payload.FormatVersion != 1 || payload.ScriptType != identity.Id ||
		    payload.Version != identity.Version)
		{
			throw new InvalidOperationException(
			    $"Script payload identity/version does not match '{scriptType.FullName}'.");
		}
		var assignments = new List<Action>();
		foreach (ScriptFieldDescriptor descriptor in Describe(scriptType))
		{
			if (!descriptor.IsSupported)
			{
				throw new InvalidOperationException(descriptor.Diagnostic);
			}
			bool hasValue = payload.Fields.TryGetValue(descriptor.Name, out JsonElement value);
			if (!hasValue)
			{
				foreach (string formerName in descriptor.FormerNames)
				{
					if (payload.Fields.TryGetValue(formerName, out value))
					{
						hasValue = true;
						break;
					}
				}
				if (!hasValue)
				{
					continue;
				}
			}
			if (value.ValueKind == JsonValueKind.Null &&
			    descriptor.Kind is ScriptSerializedFieldKind.Array or ScriptSerializedFieldKind.List)
			{
				assignments.Add(() => descriptor.Field.SetValue(behaviour, null));
				continue;
			}
			if (IsReferenceKind(descriptor.Kind))
			{
				ReferencePayload? reference = ReadReference(value);
				assignments.Add(() => QueueReferenceRestore(behaviour, descriptor,
				                      new List<ReferencePayload?> { reference }));
			}
			else if ((descriptor.Kind is ScriptSerializedFieldKind.Array or ScriptSerializedFieldKind.List) &&
			         IsReferenceKind(descriptor.ElementKind))
			{
				List<ReferencePayload?> references = value.EnumerateArray().Select(ReadReference).ToList();
				assignments.Add(() => QueueReferenceRestore(behaviour, descriptor, references));
			}
			else
			{
				if (value.ValueKind == JsonValueKind.Null && descriptor.ValueType.IsValueType)
					throw new JsonException($"Nonnullable script field '{descriptor.Name}' cannot be null.");
				object? restored = DeserializeScalarValue(descriptor, value);
				assignments.Add(() => descriptor.Field.SetValue(behaviour, restored));
			}
		}
		pendingReferences.Remove(behaviour);
		foreach (Action apply in assignments) apply();
	}

	internal static void ResolvePendingReferences(Behaviour behaviour)
	{
		if (!pendingReferences.TryGetValue(behaviour, out List<Action>? pending))
		{
			return;
		}
		pendingReferences.Remove(behaviour);
		foreach (Action resolve in pending)
		{
			resolve();
		}
	}

	public static IReadOnlyList<ScriptFieldDescriptor> Describe(Type scriptType)
	{
		if (scriptType == null)
		{
			throw new ArgumentNullException(nameof(scriptType));
		}
		if (!typeof(Behaviour).IsAssignableFrom(scriptType) || scriptType.IsAbstract || scriptType.ContainsGenericParameters)
		{
			throw new ArgumentException("Script field schema requires a concrete Behaviour type.", nameof(scriptType));
		}

		var fields = new List<ScriptFieldDescriptor>();
		for (Type? current = scriptType; current != null && current != typeof(Behaviour) && current != typeof(Component);
		     current = current.BaseType)
		{
			foreach (FieldInfo field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
			                                              BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
			{
				bool optedIn = field.IsDefined(typeof(SerializeFieldAttribute), inherit: true);
				if ((!field.IsPublic && !optedIn) || field.IsStatic || field.IsLiteral || field.IsInitOnly ||
				    field.IsDefined(typeof(NonSerializedAttribute), inherit: false))
				{
					continue;
				}

				(ScriptSerializedFieldKind kind, ScriptSerializedFieldKind elementKind) = DescribeType(field.FieldType);
				string diagnostic = kind == ScriptSerializedFieldKind.Unsupported ||
				                    (kind is ScriptSerializedFieldKind.Array or ScriptSerializedFieldKind.List &&
				                     elementKind == ScriptSerializedFieldKind.Unsupported)
				                    ? $"Unsupported serialized field type '{field.FieldType.FullName}' on " +
				                      $"'{scriptType.FullName}.{field.Name}'."
				                    : string.Empty;
				string[] formerNames = field.GetCustomAttributes<FormerlySerializedAsAttribute>()
				                         .Select(attribute => attribute.OldName)
				                         .Distinct(StringComparer.Ordinal)
				                         .ToArray();
				fields.Add(new ScriptFieldDescriptor(field, kind, elementKind, diagnostic, formerNames));
			}
		}
		fields.Sort((left, right) => string.CompareOrdinal(left.Name, right.Name));
		return fields;
	}

	private static (ScriptSerializedFieldKind Kind, ScriptSerializedFieldKind ElementKind) DescribeType(Type type)
	{
		if (type.IsArray)
		{
			if (type.GetArrayRank() != 1 || type.GetElementType()!.IsArray)
			{
				return (ScriptSerializedFieldKind.Unsupported, ScriptSerializedFieldKind.Unsupported);
			}
			ScriptSerializedFieldKind element = DescribeType(type.GetElementType()!).Kind;
			return (ScriptSerializedFieldKind.Array, element);
		}
		if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>))
		{
			Type itemType = type.GetGenericArguments()[0];
			if (itemType.IsArray ||
			    (itemType.IsGenericType && itemType.GetGenericTypeDefinition() == typeof(List<>)))
			{
				return (ScriptSerializedFieldKind.List, ScriptSerializedFieldKind.Unsupported);
			}
			ScriptSerializedFieldKind element = DescribeType(itemType).Kind;
			return (ScriptSerializedFieldKind.List, element);
		}
		if (type == typeof(bool)) return (ScriptSerializedFieldKind.Boolean, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(sbyte) || type == typeof(short) || type == typeof(int) || type == typeof(long))
			return (ScriptSerializedFieldKind.SignedInteger, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(byte) || type == typeof(ushort) || type == typeof(uint) || type == typeof(ulong))
			return (ScriptSerializedFieldKind.UnsignedInteger, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(float) || type == typeof(double))
			return (ScriptSerializedFieldKind.FloatingPoint, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(string)) return (ScriptSerializedFieldKind.String, ScriptSerializedFieldKind.Unsupported);
		if (type.IsEnum) return (ScriptSerializedFieldKind.Enumeration, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(Vector3)) return (ScriptSerializedFieldKind.Vector3, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(Quaternion)) return (ScriptSerializedFieldKind.Quaternion, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(Color)) return (ScriptSerializedFieldKind.Color, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(GameObject))
			return (ScriptSerializedFieldKind.GameObjectReference, ScriptSerializedFieldKind.Unsupported);
		if (typeof(Component).IsAssignableFrom(type))
			return (ScriptSerializedFieldKind.ComponentReference, ScriptSerializedFieldKind.Unsupported);
		if (type == typeof(AssetReference))
			return (ScriptSerializedFieldKind.AssetReference, ScriptSerializedFieldKind.Unsupported);
		return (ScriptSerializedFieldKind.Unsupported, ScriptSerializedFieldKind.Unsupported);
	}

	private static JsonSerializerOptions CreateJsonOptions()
	{
		var options = new JsonSerializerOptions { IncludeFields = true, PropertyNameCaseInsensitive = false };
		options.Converters.Add(new AssetReferenceConverter());
		options.Converters.Add(new Vector3Converter());
		options.Converters.Add(new QuaternionConverter());
		options.Converters.Add(new ColorConverter());
		return options;
	}

	private static float[] ReadComponents(ref Utf8JsonReader reader, int count)
	{
		using JsonDocument document = JsonDocument.ParseValue(ref reader);
		JsonElement value = document.RootElement;
		if (value.ValueKind != JsonValueKind.Array || value.GetArrayLength() != count)
			throw new JsonException("Vector/color payload has an invalid component count.");
		float[] components = new float[count];
		for (int index = 0; index < count; ++index)
		{
			components[index] = value[index].GetSingle();
			if (!float.IsFinite(components[index])) throw new JsonException("Vector/color component is not finite.");
		}
		return components;
	}

	private static void WriteComponents(Utf8JsonWriter writer, params float[] values)
	{
		writer.WriteStartArray();
		foreach (float value in values) writer.WriteNumberValue(value);
		writer.WriteEndArray();
	}

	private sealed class Vector3Converter : JsonConverter<Vector3>
	{
		public override Vector3 Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options)
		{
			float[] values = ReadComponents(ref reader, 3);
			return new Vector3(values[0], values[1], values[2]);
		}

		public override void Write(Utf8JsonWriter writer, Vector3 value, JsonSerializerOptions options) =>
			WriteComponents(writer, value.X, value.Y, value.Z);
	}

	private sealed class QuaternionConverter : JsonConverter<Quaternion>
	{
		public override Quaternion Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options)
		{
			float[] values = ReadComponents(ref reader, 4);
			return new Quaternion(values[0], values[1], values[2], values[3]);
		}

		public override void Write(Utf8JsonWriter writer, Quaternion value, JsonSerializerOptions options) =>
			WriteComponents(writer, value.X, value.Y, value.Z, value.W);
	}

	private sealed class ColorConverter : JsonConverter<Color>
	{
		public override Color Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options)
		{
			float[] values = ReadComponents(ref reader, 4);
			return new Color(values[0], values[1], values[2], values[3]);
		}

		public override void Write(Utf8JsonWriter writer, Color value, JsonSerializerOptions options) =>
			WriteComponents(writer, value.R, value.G, value.B, value.A);
	}

	private sealed class AssetReferenceConverter : JsonConverter<AssetReference>
	{
		public override AssetReference Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
		{
			if (reader.TokenType == JsonTokenType.Null)
			{
				return default;
			}
			using JsonDocument document = JsonDocument.ParseValue(ref reader);
			JsonElement value = document.RootElement;
			return new AssetReference(value.GetProperty("guid").GetString() ?? string.Empty,
			                          value.GetProperty("localFileId").GetUInt64());
		}

		public override void Write(Utf8JsonWriter writer, AssetReference value, JsonSerializerOptions options)
		{
			if (string.IsNullOrEmpty(value.Guid))
			{
				writer.WriteNullValue();
				return;
			}
			writer.WriteStartObject();
			writer.WriteString("guid", value.Guid);
			writer.WriteNumber("localFileId", value.LocalFileId);
			writer.WriteEndObject();
		}
	}

	private static object? DeserializeScalarValue(ScriptFieldDescriptor descriptor, JsonElement value)
	{
		if (descriptor.ValueType.IsEnum) return ReadEnum(descriptor.ValueType, value);
		Type? elementType = descriptor.Kind == ScriptSerializedFieldKind.Array ? descriptor.ValueType.GetElementType() :
			descriptor.Kind == ScriptSerializedFieldKind.List ? descriptor.ValueType.GetGenericArguments()[0] : null;
		if (elementType?.IsEnum != true) return value.Deserialize(descriptor.ValueType, jsonOptions);
		if (value.ValueKind != JsonValueKind.Array) throw new JsonException("Enum collection must be an array.");
		if (descriptor.Kind == ScriptSerializedFieldKind.Array)
		{
			Array result = Array.CreateInstance(elementType, value.GetArrayLength());
			int index = 0;
			foreach (JsonElement item in value.EnumerateArray()) result.SetValue(ReadEnum(elementType, item), index++);
			return result;
		}
		var list = (IList)Activator.CreateInstance(descriptor.ValueType)!;
		foreach (JsonElement item in value.EnumerateArray()) list.Add(ReadEnum(elementType, item));
		return list;
	}

	private static object ReadEnum(Type type, JsonElement value)
	{
		if (value.ValueKind != JsonValueKind.Number) throw new JsonException("Enum payload requires an integer.");
		object number = value.Deserialize(Enum.GetUnderlyingType(type), jsonOptions)!;
		return Enum.ToObject(type, number);
	}

	private static JsonElement SerializeFieldValue(ScriptFieldDescriptor descriptor, object? value)
	{
		if (value == null)
		{
			return JsonSerializer.SerializeToElement<object?>(null, jsonOptions);
		}
		if (IsReferenceKind(descriptor.Kind))
		{
			return SerializeReference(value, descriptor.Kind);
		}
		if ((descriptor.Kind is ScriptSerializedFieldKind.Array or ScriptSerializedFieldKind.List) &&
		    IsReferenceKind(descriptor.ElementKind))
		{
			var references = new List<JsonElement?>();
			if (value is IEnumerable collection)
			{
				foreach (object? item in collection)
				{
					references.Add(item == null ? null : SerializeReference(item, descriptor.ElementKind));
				}
			}
			return JsonSerializer.SerializeToElement(references, jsonOptions);
		}
		if (descriptor.ValueType.IsEnum)
		{
			// Passing collectible enum Types to System.Text.Json can populate shared converter caches.
			// Serialize only their framework primitive values; no project Type enters serializer metadata.
			return JsonSerializer.SerializeToElement(Convert.ChangeType(value, Enum.GetUnderlyingType(descriptor.ValueType)), jsonOptions);
		}
		Type? elementType = descriptor.Kind == ScriptSerializedFieldKind.Array ? descriptor.ValueType.GetElementType() :
			descriptor.Kind == ScriptSerializedFieldKind.List ? descriptor.ValueType.GetGenericArguments()[0] : null;
		if (elementType?.IsEnum == true)
		{
			var numbers = new List<object>();
			foreach (object item in (IEnumerable)value) numbers.Add(Convert.ChangeType(item, Enum.GetUnderlyingType(elementType)));
			return JsonSerializer.SerializeToElement(numbers, jsonOptions);
		}
		return JsonSerializer.SerializeToElement(value, descriptor.ValueType, jsonOptions);
	}

	private static string? GetPersistentId(object? value, ScriptSerializedFieldKind kind)
	{
		return value switch
		{
			null => null,
			GameObject gameObject when kind == ScriptSerializedFieldKind.GameObjectReference =>
				gameObject.PersistentId,
			Component component when kind == ScriptSerializedFieldKind.ComponentReference => component.PersistentId,
			_ => throw new InvalidOperationException("Script reference field value does not match its declared kind.")
		};
	}

	private static JsonElement SerializeReference(object value, ScriptSerializedFieldKind kind)
	{
		string? persistentId = GetPersistentId(value, kind);
		var reference = new Dictionary<string, ReferencePayload>(StringComparer.Ordinal)
		{
			["$ursaReference"] = new ReferencePayload
			{
				Kind = kind == ScriptSerializedFieldKind.GameObjectReference ? "GameObject" : "Component",
				Id = persistentId ?? string.Empty
			}
		};
		return JsonSerializer.SerializeToElement(reference, jsonOptions);
	}

	private static ReferencePayload? ReadReference(JsonElement value)
	{
		if (value.ValueKind == JsonValueKind.Null)
		{
			return null;
		}
		if (!value.TryGetProperty("$ursaReference", out JsonElement reference) ||
		    reference.ValueKind != JsonValueKind.Object || !reference.TryGetProperty("Kind", out JsonElement kind) ||
		    kind.ValueKind != JsonValueKind.String || !reference.TryGetProperty("Id", out JsonElement id) ||
		    id.ValueKind != JsonValueKind.String)
		{
			throw new InvalidOperationException("Serialized script reference has an invalid structure.");
		}
		return new ReferencePayload { Kind = kind.GetString() ?? string.Empty, Id = id.GetString() ?? string.Empty };
	}

	private static void QueueReferenceRestore(Behaviour behaviour, ScriptFieldDescriptor descriptor,
	                                          List<ReferencePayload?> references)
	{
		if (!pendingReferences.TryGetValue(behaviour, out List<Action>? pending))
		{
			pending = new List<Action>();
			pendingReferences.Add(behaviour, pending);
		}
		pending.Add(() =>
		{
			Type? elementType = descriptor.Kind == ScriptSerializedFieldKind.Array
			                        ? descriptor.ValueType.GetElementType()
			                        : descriptor.Kind == ScriptSerializedFieldKind.List
			                              ? descriptor.ValueType.GetGenericArguments()[0]
			                              : descriptor.ValueType;
			object? Resolve(ReferencePayload? reference)
			{
				if (reference == null)
				{
					return null;
				}
				if (reference.Kind == "GameObject")
				{
					return GameObject.FromPersistentId(reference.Id);
				}
				if (reference.Kind == "Component")
				{
					return Component.FromPersistentId(reference.Id, elementType!);
				}
				throw new InvalidOperationException($"Unknown serialized script reference kind '{reference.Kind}'.");
			}
			if (descriptor.Kind == ScriptSerializedFieldKind.Array)
			{
				Array values = Array.CreateInstance(elementType!, references.Count);
				for (int index = 0; index < references.Count; ++index)
				{
					values.SetValue(Resolve(references[index]), index);
				}
				descriptor.Field.SetValue(behaviour, values);
			}
			else if (descriptor.Kind == ScriptSerializedFieldKind.List)
			{
				if (Activator.CreateInstance(descriptor.ValueType) is not IList values)
				{
					throw new InvalidOperationException($"Cannot create List field '{descriptor.Name}'.");
				}
				foreach (ReferencePayload? reference in references)
				{
					values.Add(Resolve(reference));
				}
				descriptor.Field.SetValue(behaviour, values);
			}
			else
			{
				descriptor.Field.SetValue(behaviour, Resolve(references[0]));
			}
		});
	}

	private static bool IsReferenceKind(ScriptSerializedFieldKind kind)
	{
		return kind is ScriptSerializedFieldKind.GameObjectReference or ScriptSerializedFieldKind.ComponentReference;
	}
}
