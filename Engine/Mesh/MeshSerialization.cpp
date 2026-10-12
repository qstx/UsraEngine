#include <Mesh/Mesh.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SerializationContext.hpp>
#include <cstring>
#include <stdexcept>

namespace Ursa
{
namespace
{
void Write32(Vector<UInt8>& bytes, UInt32 value)
{
	for (Size i = 0; i < 4; ++i)
	{
		bytes.push_back(static_cast<UInt8>(value >> (i * 8)));
	}
}

UInt32 Read32(const Vector<UInt8>& bytes, Size& offset)
{
	if (offset > bytes.size() || bytes.size() - offset < 4)
	{
		throw std::runtime_error("Mesh channel contains a truncated value");
	}
	UInt32 result = 0;
	for (Size i = 0; i < 4; ++i)
	{
		result |= UInt32(bytes[offset++]) << (i * 8);
	}
	return result;
}

void WriteFloat(Vector<UInt8>& bytes, Float32 value)
{
	UInt32 bits = 0;
	static_assert(sizeof(value) == sizeof(bits), "Mesh requires 32-bit floats");
	std::memcpy(&bits, &value, sizeof(bits));
	Write32(bytes, bits);
}

Float32 ReadFloat(const Vector<UInt8>& bytes, Size& offset)
{
	UInt32 bits = Read32(bytes, offset);
	Float32 value = 0;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

template <class T, class Write> Vector<UInt8> Channel(const Vector<T>& values, Write write)
{
	Vector<UInt8> bytes;
	bytes.reserve(values.size() * sizeof(T));
	for (const auto& value : values)
	{
		write(bytes, value);
	}
	return bytes;
}

template <class T, class Read>
Result<void> ReadChannel(const ArchiveReader& reader, const char* name, Size count, Size stride, Vector<T>& output,
                         UInt64& budget, Read read, bool optional = true)
{
	auto bytes = reader.Bytes(name);
	if (!bytes)
	{
		return Result<void>::Failure(String("Mesh channel ") + name + ": " + bytes.error);
	}
	if (count > Mesh::maxDataBytes / stride || bytes.value.size() > Mesh::maxDataBytes - budget ||
	    (bytes.value.size() != count * stride && !(optional && bytes.value.empty())))
	{
		return Result<void>::Failure(String("Invalid or oversized Mesh channel ") + name);
	}
	budget += bytes.value.size();
	if (bytes.value.empty())
	{
		return {};
	}
	output.reserve(count);
	Size offset = 0;
	for (Size i = 0; i < count; ++i)
	{
		output.push_back(read(bytes.value, offset));
	}
	return {};
}
} // namespace

Result<void> Mesh::Serialize(ArchiveWriter& writer) const
{
	const auto current = Snapshot();
	if (!current)
	{
		return Result<void>::Failure("Cannot serialize a destroyed Mesh");
	}
	const auto& data = current->data;
	auto validData = Validate(data);
	if (!validData)
	{
		return validData;
	}
	try
	{
		auto result = writer.Write("dataVersion", UInt64{1});
		if (result)
		{
			result = writer.Write("vertexCount", UInt64(data.positions.size()));
		}
		const auto vector3 = [](Vector<UInt8>& bytes, Vec3 v)
		{
			WriteFloat(bytes, v.x);
			WriteFloat(bytes, v.y);
			WriteFloat(bytes, v.z);
		};
		if (result)
		{
			result = writer.WriteBytes("positions", Channel(data.positions, vector3));
		}
		if (result)
		{
			result = writer.WriteBytes("normals", Channel(data.normals, vector3));
		}
		if (result)
		{
			result = writer.WriteBytes("uv0", Channel(data.uv0,
			                                          [](Vector<UInt8>& bytes, Vec2 v)
			                                          {
				                                          WriteFloat(bytes, v.x);
				                                          WriteFloat(bytes, v.y);
			                                          }));
		}
		if (result)
		{
			result = writer.WriteBytes("colors", Channel(data.colors,
			                                             [](Vector<UInt8>& bytes, Color v)
			                                             {
				                                             WriteFloat(bytes, v.red);
				                                             WriteFloat(bytes, v.green);
				                                             WriteFloat(bytes, v.blue);
				                                             WriteFloat(bytes, v.alpha);
			                                             }));
		}
		if (result)
		{
			result = writer.WriteBytes("tangents", Channel(data.tangents,
			                                               [](Vector<UInt8>& bytes, Vec4 v)
			                                               {
				                                               WriteFloat(bytes, v.x);
				                                               WriteFloat(bytes, v.y);
				                                               WriteFloat(bytes, v.z);
				                                               WriteFloat(bytes, v.w);
			                                               }));
		}
		if (result)
		{
			result = writer.Write("indexCount", UInt64(data.indices.size()));
		}
		if (result)
		{
			result = writer.WriteBytes(
			    "indices", Channel(data.indices, [](Vector<UInt8>& bytes, UInt32 v) { Write32(bytes, v); }));
		}
		if (!result)
		{
			return result;
		}
		auto ranges = writer.Array("subMeshes");
		if (!ranges)
		{
			return Result<void>::Failure(ranges.error);
		}
		for (const auto& range : data.subMeshes)
		{
			auto object = ranges.value.AddObject();
			if (!object)
			{
				return Result<void>::Failure(object.error);
			}
			result = object.value.Write("firstIndex", UInt64(range.firstIndex));
			if (result)
			{
				result = object.value.Write("indexCount", UInt64(range.indexCount));
			}
			if (result)
			{
				result = object.value.Write("baseVertex", Int64(range.baseVertex));
			}
			if (!result)
			{
				return result;
			}
		}
		auto bounds = writer.Object("bounds");
		if (!bounds)
		{
			return Result<void>::Failure(bounds.error);
		}
		result = bounds.value.Write("empty", data.bounds.empty);
		if (result)
		{
			result = bounds.value.WriteBytes("minimum", Channel(Vector<Vec3>{data.bounds.minimum}, vector3));
		}
		if (result)
		{
			result = bounds.value.WriteBytes("maximum", Channel(Vector<Vec3>{data.bounds.maximum}, vector3));
		}
		if (result)
		{
			result = data.skeletonReference.guid ? WriteAssetReference(writer, "skeleton", data.skeletonReference)
			                                     : writer.Null("skeleton");
		}
		if (result)
		{
			result = writer.WriteBytes("skinInfluences", Channel(data.skinInfluences,
			                                                     [](Vector<UInt8>& bytes, const MeshSkinInfluence& v)
			                                                     {
				                                                     for (UInt16 joint : v.jointIndices)
				                                                     {
					                                                     bytes.push_back(UInt8(joint));
					                                                     bytes.push_back(UInt8(joint >> 8));
				                                                     }
				                                                     for (Float32 weight : v.weights)
				                                                     {
					                                                     WriteFloat(bytes, weight);
				                                                     }
			                                                     }));
		}
		return result;
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(String("Cannot serialize Mesh: ") + error.what());
	}
}

Result<void> Mesh::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	try
	{
		auto format = reader.Unsigned("dataVersion");
		auto count = reader.Unsigned("vertexCount");
		auto indexCount = reader.Unsigned("indexCount");
		if (!format || format.value != 1 || !count || !indexCount || count.value > maxDataBytes / sizeof(Vec3) ||
		    indexCount.value > maxDataBytes / sizeof(UInt32))
		{
			return Result<void>::Failure("Invalid Mesh data version or channel count");
		}
		MeshData data;
		UInt64 budget = 0;
		const auto vector3 = [](const Vector<UInt8>& bytes, Size& at) -> Vec3
		{
			const auto x = ReadFloat(bytes, at);
			const auto y = ReadFloat(bytes, at);
			const auto z = ReadFloat(bytes, at);
			return {x, y, z};
		};
		auto result = ReadChannel(reader, "positions", Size(count.value), 12, data.positions, budget, vector3, false);
		if (result)
		{
			result = ReadChannel(reader, "normals", Size(count.value), 12, data.normals, budget, vector3);
		}
		if (result)
		{
			result = ReadChannel(reader, "uv0", Size(count.value), 8, data.uv0, budget,
			                     [](const Vector<UInt8>& bytes, Size& at) -> Vec2
			                     {
				                     const auto x = ReadFloat(bytes, at);
				                     const auto y = ReadFloat(bytes, at);
				                     return {x, y};
			                     });
		}
		if (result)
		{
			result = ReadChannel(reader, "colors", Size(count.value), 16, data.colors, budget,
			                     [](const Vector<UInt8>& bytes, Size& at) -> Color
			                     {
				                     const auto r = ReadFloat(bytes, at);
				                     const auto g = ReadFloat(bytes, at);
				                     const auto b = ReadFloat(bytes, at);
				                     const auto a = ReadFloat(bytes, at);
				                     return {r, g, b, a};
			                     });
		}
		if (result)
		{
			result = ReadChannel(reader, "tangents", Size(count.value), 16, data.tangents, budget,
			                     [](const Vector<UInt8>& bytes, Size& at) -> Vec4
			                     {
				                     const auto x = ReadFloat(bytes, at);
				                     const auto y = ReadFloat(bytes, at);
				                     const auto z = ReadFloat(bytes, at);
				                     const auto w = ReadFloat(bytes, at);
				                     return {x, y, z, w};
			                     });
		}
		if (result)
		{
			result = ReadChannel(reader, "indices", Size(indexCount.value), 4, data.indices, budget, Read32, false);
		}
		if (!result)
		{
			return result;
		}
		auto ranges = reader.Array("subMeshes");
		if (!ranges || ranges.value.size() > maxSubMeshes)
		{
			return Result<void>::Failure("Invalid Mesh submesh count");
		}
		for (const auto& range : ranges.value)
		{
			auto first = range.Unsigned("firstIndex");
			auto size = range.Unsigned("indexCount");
			auto base = range.Integer("baseVertex");
			if (!first || !size || !base || first.value > UINT32_MAX || size.value > UINT32_MAX ||
			    base.value < INT32_MIN || base.value > INT32_MAX)
			{
				return Result<void>::Failure("Invalid Mesh submesh descriptor");
			}
			data.subMeshes.push_back({UInt32(first.value), UInt32(size.value), Int32(base.value)});
		}
		auto bounds = reader.Object("bounds");
		if (!bounds)
		{
			return Result<void>::Failure(bounds.error);
		}
		auto empty = bounds.value.Boolean("empty");
		if (!empty)
		{
			return Result<void>::Failure(empty.error);
		}
		Vector<Vec3> minimum, maximum;
		result = ReadChannel(bounds.value, "minimum", 1, 12, minimum, budget, vector3, false);
		if (result)
		{
			result = ReadChannel(bounds.value, "maximum", 1, 12, maximum, budget, vector3, false);
		}
		if (!result)
		{
			return result;
		}
		data.bounds = {minimum.front(), maximum.front(), empty.value};
		result = ReadChannel(reader, "skinInfluences", Size(count.value), 24, data.skinInfluences, budget,
		                     [](const Vector<UInt8>& bytes, Size& at) -> MeshSkinInfluence
		                     {
			                     MeshSkinInfluence value;
			                     for (auto& joint : value.jointIndices)
			                     {
				                     joint = UInt16(UInt16(bytes[at]) | (UInt16(bytes[at + 1]) << 8));
				                     at += 2;
			                     }
			                     for (auto& weight : value.weights)
			                     {
				                     weight = ReadFloat(bytes, at);
			                     }
			                     return value;
		                     });
		if (!result)
		{
			return result;
		}
		if (!reader.Contains("skeleton"))
		{
			return Result<void>::Failure("Mesh skeleton field is missing");
		}
		if (!reader.IsNull("skeleton"))
		{
			auto reference = ReadAssetReference(reader, "skeleton");
			if (!reference || !context.ResourceLoader())
			{
				return Result<void>::Failure(reference ? "Mesh skin requires a ResourceSession" : reference.error);
			}
			auto skeleton = context.ResourceLoader()->LoadSkeleton(reference.value.guid, reference.value.localFileId);
			if (!skeleton)
			{
				return Result<void>::Failure(skeleton.error);
			}
			data.skeletonReference = reference.value;
			data.skeleton = std::move(skeleton.value);
		}
		std::lock_guard<std::mutex> lock(mutex);
		return Publish(std::move(data));
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(String("Cannot deserialize Mesh: ") + error.what());
	}
}
} // namespace Ursa
