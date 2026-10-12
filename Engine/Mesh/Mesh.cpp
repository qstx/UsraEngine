#include <Mesh/Mesh.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace Ursa
{
namespace
{
std::atomic<UInt64> nextMeshIdentity{1};

bool Finite(Vec3 value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Bounds CalculateBounds(const Vector<Vec3>& positions)
{
	Bounds bounds;
	if (!positions.empty())
	{
		bounds.empty = false;
		bounds.minimum = positions.front();
		bounds.maximum = positions.front();
		for (Vec3 value : positions)
		{
			bounds.minimum.x = std::min(bounds.minimum.x, value.x);
			bounds.minimum.y = std::min(bounds.minimum.y, value.y);
			bounds.minimum.z = std::min(bounds.minimum.z, value.z);
			bounds.maximum.x = std::max(bounds.maximum.x, value.x);
			bounds.maximum.y = std::max(bounds.maximum.y, value.y);
			bounds.maximum.z = std::max(bounds.maximum.z, value.z);
		}
	}
	return bounds;
}

String IndexedError(const char* field, Size index)
{
	return String("Invalid Mesh ") + field + " at index " + ToUrsaString(std::to_string(index));
}
} // namespace

Mesh::Mesh() : identity(nextMeshIdentity.fetch_add(1, std::memory_order_relaxed))
{
	snapshot = MakeShared<MeshSnapshot>(Memory::MemoryTag::Assets);
}

Result<MeshPtr> Mesh::Create(MeshData data)
{
	try
	{
		auto mesh = MakeShared<Mesh>(Memory::MemoryTag::Assets);
		auto result = mesh->SetData(std::move(data));
		return result ? Result<MeshPtr>::Success(std::move(mesh)) : Result<MeshPtr>::Failure(result.error);
	}
	catch (const std::bad_alloc&)
	{
		return Result<MeshPtr>::Failure("Cannot allocate Mesh data");
	}
}

Result<void> Mesh::Validate(const MeshData& data)
{
	const Size count = data.positions.size();
	if (count > std::numeric_limits<UInt32>::max() || data.indices.size() > std::numeric_limits<UInt32>::max() ||
	    data.subMeshes.size() > maxSubMeshes)
	{
		return Result<void>::Failure("Mesh vertex, index or submesh count exceeds capacity");
	}
	UInt64 bytes = 0;
	const auto add = [&](Size size, Size stride)
	{
		if (size > maxDataBytes / stride || bytes > maxDataBytes - size * stride)
		{
			return false;
		}
		bytes += size * stride;
		return true;
	};
	if (!add(count, sizeof(Vec3)) || !add(data.normals.size(), sizeof(Vec3)) || !add(data.uv0.size(), sizeof(Vec2)) ||
	    !add(data.colors.size(), sizeof(Color)) || !add(data.tangents.size(), sizeof(Vec4)) ||
	    !add(data.indices.size(), sizeof(UInt32)) || !add(data.subMeshes.size(), sizeof(SubMesh)) ||
	    !add(data.skinInfluences.size(), sizeof(MeshSkinInfluence)))
	{
		return Result<void>::Failure("Mesh decoded data exceeds 256 MiB");
	}
	if ((!data.normals.empty() && data.normals.size() != count) || (!data.uv0.empty() && data.uv0.size() != count) ||
	    (!data.colors.empty() && data.colors.size() != count) ||
	    (!data.tangents.empty() && data.tangents.size() != count))
	{
		return Result<void>::Failure("Mesh vertex channel counts must match positions");
	}
	for (Size i = 0; i < count; ++i)
	{
		if (!Finite(data.positions[i]) || (!data.normals.empty() && !Finite(data.normals[i])))
		{
			return Result<void>::Failure(IndexedError("position/normal", i));
		}
		if (!data.uv0.empty() && (!std::isfinite(data.uv0[i].x) || !std::isfinite(data.uv0[i].y)))
		{
			return Result<void>::Failure(IndexedError("UV0", i));
		}
		if (!data.colors.empty())
		{
			const Color value = data.colors[i];
			if (!std::isfinite(value.red) || !std::isfinite(value.green) || !std::isfinite(value.blue) ||
			    !std::isfinite(value.alpha))
			{
				return Result<void>::Failure(IndexedError("color", i));
			}
		}
		if (!data.tangents.empty())
		{
			const Vec4 value = data.tangents[i];
			if (!Finite({value.x, value.y, value.z}) || !std::isfinite(value.w) ||
			    (value.w != 1.0f && value.w != -1.0f))
			{
				return Result<void>::Failure(IndexedError("tangent", i));
			}
		}
	}
	if (data.indices.size() % 3 != 0)
	{
		return Result<void>::Failure("Mesh triangle index count must be divisible by three");
	}
	for (Size i = 0; data.subMeshes.empty() && i < data.indices.size(); ++i)
	{
		if (data.indices[i] >= count)
		{
			return Result<void>::Failure(IndexedError("index", i));
		}
	}
	for (Size i = 0; i < data.subMeshes.size(); ++i)
	{
		const auto& sub = data.subMeshes[i];
		if (sub.firstIndex > data.indices.size() || sub.indexCount > data.indices.size() - sub.firstIndex ||
		    sub.indexCount % 3 != 0)
		{
			return Result<void>::Failure(IndexedError("submesh range", i));
		}
		for (Size j = sub.firstIndex; j < Size(sub.firstIndex) + sub.indexCount; ++j)
		{
			const Int64 index = Int64(data.indices[j]) + sub.baseVertex;
			if (index < 0 || UInt64(index) >= count)
			{
				return Result<void>::Failure(IndexedError("submesh base vertex", i));
			}
		}
	}
	if (!data.bounds.empty &&
	    (!Finite(data.bounds.minimum) || !Finite(data.bounds.maximum) ||
	     data.bounds.minimum.x > data.bounds.maximum.x || data.bounds.minimum.y > data.bounds.maximum.y ||
	     data.bounds.minimum.z > data.bounds.maximum.z))
	{
		return Result<void>::Failure("Invalid Mesh bounds");
	}
	if (data.skeletonReference.guid)
	{
		if (!data.skeletonReference.localFileId || !data.skeleton ||
		    data.skeleton->modelGuid != data.skeletonReference.guid ||
		    data.skeleton->localFileId != data.skeletonReference.localFileId || data.skinInfluences.size() != count)
		{
			return Result<void>::Failure("Mesh skin binding does not match skeleton or vertex count");
		}
		for (Size i = 0; i < count; ++i)
		{
			Float32 total = 0.0f;
			for (Size j = 0; j < 4; ++j)
			{
				const Float32 weight = data.skinInfluences[i].weights[j];
				if (!std::isfinite(weight) || weight < 0.0f ||
				    (weight > 0.0f && data.skinInfluences[i].jointIndices[j] >= data.skeleton->joints.size()))
				{
					return Result<void>::Failure(IndexedError("skin influence", i));
				}
				total += weight;
			}
			if (total != 0.0f && std::abs(total - 1.0f) > 0.001f)
			{
				return Result<void>::Failure(IndexedError("skin weight sum", i));
			}
		}
	}
	else if (data.skeletonReference.localFileId || data.skeleton || !data.skinInfluences.empty())
	{
		return Result<void>::Failure("Mesh skin data requires a skeleton asset reference");
	}
	return {};
}

String Mesh::SerializationType() const
{
	return "ursa.Mesh";
}

UInt64 Mesh::Identity() const
{
	return identity;
}

UInt64 Mesh::Version() const
{
	auto current = Snapshot();
	return current ? current->version : 0;
}

bool Mesh::IsValid() const
{
	return valid.load(std::memory_order_acquire);
}

std::shared_ptr<const MeshSnapshot> Mesh::Snapshot() const
{
	return IsValid() ? std::atomic_load_explicit(&snapshot, std::memory_order_acquire) : nullptr;
}

Result<MeshData> Mesh::GetData() const
{
	const auto current = Snapshot();
	if (!current)
	{
		return Result<MeshData>::Failure("Mesh resource has been destroyed");
	}
	try
	{
		return Result<MeshData>::Success(current->data);
	}
	catch (const std::bad_alloc&)
	{
		return Result<MeshData>::Failure("Cannot copy Mesh data");
	}
}

Result<void> Mesh::Publish(MeshData data)
{
	if (!IsValid())
	{
		return Result<void>::Failure("Mesh resource has been destroyed");
	}
	auto checked = Validate(data);
	if (!checked)
	{
		return checked;
	}
	try
	{
		const auto previous = std::atomic_load_explicit(&snapshot, std::memory_order_acquire);
		auto candidate = MakeShared<MeshSnapshot>(Memory::MemoryTag::Assets);
		candidate->version = previous->version + 1;
		candidate->data = std::move(data);
		std::shared_ptr<const MeshSnapshot> immutable = std::move(candidate);
		std::atomic_store_explicit(&snapshot, std::move(immutable), std::memory_order_release);
		return {};
	}
	catch (const std::bad_alloc&)
	{
		return Result<void>::Failure("Cannot allocate Mesh snapshot");
	}
}

Result<void> Mesh::SetData(MeshData data)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (data.subMeshes.empty() && !data.indices.empty())
	{
		if (data.indices.size() > std::numeric_limits<UInt32>::max())
		{
			return Result<void>::Failure("Mesh index count exceeds capacity");
		}
		data.subMeshes.push_back({0, static_cast<UInt32>(data.indices.size()), 0});
	}
	if (data.bounds.empty)
	{
		data.bounds = CalculateBounds(data.positions);
	}
	return Publish(std::move(data));
}

Result<void> Mesh::Update(const std::function<Result<void>(MeshData&)>& edit)
{
	std::lock_guard<std::mutex> lock(mutex);
	auto current = GetData();
	if (!current)
	{
		return Result<void>::Failure(current.error);
	}
	try
	{
		auto result = edit(current.value);
		return result ? Publish(std::move(current.value)) : result;
	}
	catch (const std::bad_alloc&)
	{
		return Result<void>::Failure("Cannot allocate Mesh update");
	}
}

Result<void> Mesh::SetVertices(Vector<Vec3> values)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    data.positions = std::move(values);
		    data.bounds = CalculateBounds(data.positions);
		    return {};
	    });
}

Result<void> Mesh::SetNormals(Vector<Vec3> values)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    data.normals = std::move(values);
		    return {};
	    });
}

Result<void> Mesh::SetUv0(Vector<Vec2> values)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    data.uv0 = std::move(values);
		    return {};
	    });
}

Result<void> Mesh::SetColors(Vector<Color> values)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    data.colors = std::move(values);
		    return {};
	    });
}

Result<void> Mesh::SetTangents(Vector<Vec4> values)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    data.tangents = std::move(values);
		    return {};
	    });
}

Result<void> Mesh::SetIndices(Vector<UInt32> values, Vector<SubMesh> subMeshes)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    if (values.size() > std::numeric_limits<UInt32>::max())
		    {
			    return Result<void>::Failure("Mesh index count exceeds capacity");
		    }
		    data.indices = std::move(values);
		    data.subMeshes = std::move(subMeshes);
		    if (data.subMeshes.empty() && !data.indices.empty())
		    {
			    data.subMeshes.push_back({0, static_cast<UInt32>(data.indices.size()), 0});
		    }
		    return {};
	    });
}

Result<void> Mesh::SetSubMeshCount(Size count)
{
	if (count > maxSubMeshes)
	{
		return Result<void>::Failure("Mesh submesh count exceeds capacity");
	}
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    data.subMeshes.resize(count);
		    return {};
	    });
}

Result<void> Mesh::SetSubMesh(Size index, SubMesh value)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    if (index >= data.subMeshes.size())
		    {
			    return Result<void>::Failure("Mesh submesh index is out of range");
		    }
		    data.subMeshes[index] = value;
		    return {};
	    });
}

Result<Vector<UInt32>> Mesh::GetTriangles(Size subMesh) const
{
	const auto current = Snapshot();
	if (!current || subMesh >= current->data.subMeshes.size())
	{
		return Result<Vector<UInt32>>::Failure("Mesh is destroyed or submesh index is out of range");
	}
	const auto& sub = current->data.subMeshes[subMesh];
	Vector<UInt32> result;
	try
	{
		result.reserve(sub.indexCount);
		for (Size i = sub.firstIndex; i < Size(sub.firstIndex) + sub.indexCount; ++i)
		{
			result.push_back(static_cast<UInt32>(Int64(current->data.indices[i]) + sub.baseVertex));
		}
	}
	catch (const std::bad_alloc&)
	{
		return Result<Vector<UInt32>>::Failure("Cannot copy Mesh triangles");
	}
	return Result<Vector<UInt32>>::Success(std::move(result));
}

Result<void> Mesh::SetTriangles(Size subMesh, Vector<UInt32> values)
{
	return Update(
	    [&](MeshData& data) -> Result<void>
	    {
		    if (subMesh >= data.subMeshes.size())
		    {
			    return Result<void>::Failure("Mesh submesh index is out of range");
		    }
		    Vector<UInt32> indices;
		    Vector<SubMesh> ranges;
		    for (Size i = 0; i < data.subMeshes.size(); ++i)
		    {
			    const Size first = indices.size();
			    if (i == subMesh)
			    {
				    indices.insert(indices.end(), values.begin(), values.end());
			    }
			    else
			    {
				    const auto& old = data.subMeshes[i];
				    for (Size j = old.firstIndex; j < Size(old.firstIndex) + old.indexCount; ++j)
				    {
					    indices.push_back(static_cast<UInt32>(Int64(data.indices[j]) + old.baseVertex));
				    }
			    }
			    if (indices.size() > std::numeric_limits<UInt32>::max())
			    {
				    return Result<void>::Failure("Mesh triangle storage exceeds capacity");
			    }
			    ranges.push_back({static_cast<UInt32>(first), static_cast<UInt32>(indices.size() - first), 0});
		    }
		    data.indices = std::move(indices);
		    data.subMeshes = std::move(ranges);
		    return {};
	    });
}

Result<void> Mesh::RecalculateNormals()
{
	return Update(
	    [](MeshData& data) -> Result<void>
	    {
		    data.normals.assign(data.positions.size(), Vec3{});
		    for (const auto& sub : data.subMeshes)
		    {
			    for (Size i = sub.firstIndex; i < Size(sub.firstIndex) + sub.indexCount; i += 3)
			    {
				    const Size a = static_cast<Size>(Int64(data.indices[i]) + sub.baseVertex);
				    const Size b = static_cast<Size>(Int64(data.indices[i + 1]) + sub.baseVertex);
				    const Size c = static_cast<Size>(Int64(data.indices[i + 2]) + sub.baseVertex);
				    const Vec3 normal =
				        Cross(data.positions[b] - data.positions[a], data.positions[c] - data.positions[a]);
				    for (Size vertex : {a, b, c})
				    {
					    data.normals[vertex].x += normal.x;
					    data.normals[vertex].y += normal.y;
					    data.normals[vertex].z += normal.z;
				    }
			    }
		    }
		    for (auto& normal : data.normals)
		    {
			    const Float32 length = std::sqrt(Dot(normal, normal));
			    normal =
			        length > 0.000001f ? Vec3{normal.x / length, normal.y / length, normal.z / length} : Vec3{0, 1, 0};
		    }
		    return {};
	    });
}

Result<void> Mesh::RecalculateBounds()
{
	return Update(
	    [](MeshData& data) -> Result<void>
	    {
		    data.bounds = CalculateBounds(data.positions);
		    return {};
	    });
}

Result<void> Mesh::Clear()
{
	return SetData({});
}

Result<MeshPtr> Mesh::Clone() const
{
	auto copied = GetData();
	return copied ? Create(std::move(copied.value)) : Result<MeshPtr>::Failure(copied.error);
}

Result<void> Mesh::Destroy()
{
	std::lock_guard<std::mutex> lock(mutex);
	if (assetIdentity.guid)
	{
		return Result<void>::Failure("Cannot destroy a persistent Mesh asset; clone it first");
	}
	if (!valid.exchange(false, std::memory_order_acq_rel))
	{
		return Result<void>::Failure("Mesh resource has already been destroyed");
	}
	std::atomic_store_explicit(&snapshot, std::shared_ptr<const MeshSnapshot>{}, std::memory_order_release);
	return {};
}

AssetReference Mesh::AssetIdentity() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return assetIdentity;
}

Result<void> Mesh::BindAssetIdentity(AssetReference reference)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (!IsValid() || !reference.guid ||
	    (assetIdentity.guid &&
	     (assetIdentity.guid != reference.guid || assetIdentity.localFileId != reference.localFileId)))
	{
		return Result<void>::Failure("Mesh asset identity is invalid or already bound");
	}
	assetIdentity = reference;
	return {};
}
} // namespace Ursa
