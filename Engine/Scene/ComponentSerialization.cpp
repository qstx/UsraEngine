#include <Scene/RenderingComponents.hpp>
#include <Serialization/ArchiveFields.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/ResourceSession.hpp>

namespace Ursa
{
namespace
{
Result<Color> ReadColor(const ArchiveReader& reader)
{
	auto values = ReadNumbers(reader, "color", 4);
	if (!values)
	{
		return Result<Color>::Failure(values.error);
	}
	return Result<Color>::Success({values.value[0], values.value[1], values.value[2], values.value[3]});
}

Result<void> WriteColor(ArchiveWriter& writer, Color color)
{
	return WriteNumbers(writer, "color", {color.red, color.green, color.blue, color.alpha});
}

bool ValidFloat(Float64 value)
{
	return value >= 0 && value <= (std::numeric_limits<Float32>::max)();
}
} // namespace

Result<void> CameraComponent::Serialize(ArchiveWriter& writer) const
{
	auto result = writer.Write("verticalFovRadians", Float64(verticalFovRadians));
	if (result)
	{
		result = writer.Write("nearPlane", Float64(nearPlane));
	}
	if (result)
	{
		result = writer.Write("farPlane", Float64(farPlane));
	}
	return result;
}

Result<void> CameraComponent::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto fov = reader.Number("verticalFovRadians");
	auto nearValue = reader.Number("nearPlane");
	auto farValue = reader.Number("farPlane");
	if (!fov || !nearValue || !farValue || fov.value <= 0 || fov.value >= 3.141592653589793 || nearValue.value <= 0 ||
	    farValue.value <= nearValue.value || !ValidFloat(farValue.value))
	{
		return Result<void>::Failure("Invalid Camera component projection");
	}
	verticalFovRadians = Float32(fov.value);
	nearPlane = Float32(nearValue.value);
	farPlane = Float32(farValue.value);
	return {};
}

Result<void> BoxRenderComponent::Serialize(ArchiveWriter& writer) const
{
	auto result = WriteColor(writer, color);
	if (!result)
	{
		return result;
	}
	if (!material)
	{
		return writer.Null("material");
	}
	if (!writer.Context())
	{
		return Result<void>::Failure("No material ID resolver for BoxRenderComponent");
	}
	auto id = writer.Context()->Identify(*material);
	return id ? WriteAssetReference(writer, "material", {id.value, 0}) : Result<void>::Failure(id.error);
}

Result<void> BoxRenderComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto loadedColor = ReadColor(reader);
	if (!loadedColor)
	{
		return Result<void>::Failure(loadedColor.error);
	}
	if (!reader.Contains("material"))
	{
		return Result<void>::Failure("Missing BoxRender material field");
	}
	std::shared_ptr<MaterialInstance> loadedMaterial;
	if (!reader.IsNull("material"))
	{
		auto reference = ReadAssetReference(reader, "material");
		if (!reference || reference.value.localFileId != 0)
		{
			return Result<void>::Failure(reference ? "Material sub-asset references are not supported by this loader"
			                                       : reference.error);
		}
		if (!context.ResourceLoader())
		{
			return Result<void>::Failure("Invalid BoxRender material reference");
		}
		auto loaded = context.ResourceLoader()->LoadMaterial(reference.value.guid);
		if (!loaded)
		{
			return Result<void>::Failure(loaded.error);
		}
		loadedMaterial = std::move(loaded.value);
	}
	color = loadedColor.value;
	material = std::move(loadedMaterial);
	return {};
}

Result<void> MeshRendererComponent::Serialize(ArchiveWriter& writer) const
{
	if (!meshGuid || meshLocalFileId == 0 || !mesh)
	{
		return Result<void>::Failure("MeshRenderer has no imported model mesh assigned");
	}
	return WriteAssetReference(writer, "mesh", {meshGuid, meshLocalFileId});
}

Result<void> MeshRendererComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto reference = ReadAssetReference(reader, "mesh");
	if (!reference || reference.value.localFileId == 0 || !context.ResourceLoader())
	{
		return Result<void>::Failure(reference ? "MeshRenderer requires a model mesh sub-asset and ResourceSession"
		                                       : reference.error);
	}
	auto loaded = context.ResourceLoader()->LoadMesh(reference.value.guid, reference.value.localFileId);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	return SetMesh(reference.value.guid, reference.value.localFileId, std::move(loaded.value));
}

Result<void> SkinnedMeshRendererComponent::Serialize(ArchiveWriter& writer) const
{
	if (!meshGuid || meshLocalFileId == 0 || !mesh || !mesh->skeleton)
	{
		return Result<void>::Failure("SkinnedMeshRenderer has no imported skinned mesh assigned");
	}
	return WriteAssetReference(writer, "mesh", {meshGuid, meshLocalFileId});
}

Result<void> SkinnedMeshRendererComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto reference = ReadAssetReference(reader, "mesh");
	if (!reference || reference.value.localFileId == 0 || !context.ResourceLoader())
	{
		return Result<void>::Failure(
		    reference ? "SkinnedMeshRenderer requires a model mesh sub-asset and ResourceSession" : reference.error);
	}
	auto loaded = context.ResourceLoader()->LoadMesh(reference.value.guid, reference.value.localFileId);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	return SetMesh(reference.value.guid, reference.value.localFileId, std::move(loaded.value));
}

Result<void> DirectionalLightComponent::Serialize(ArchiveWriter& writer) const
{
	auto result = WriteColor(writer, color);
	return result ? writer.Write("intensity", Float64(intensity)) : result;
}

Result<void> DirectionalLightComponent::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto loadedColor = ReadColor(reader);
	auto loadedIntensity = reader.Number("intensity");
	if (!loadedColor || !loadedIntensity || !ValidLightColor(loadedColor.value) || !ValidFloat(loadedIntensity.value))
	{
		return Result<void>::Failure("Invalid DirectionalLight component");
	}
	color = loadedColor.value;
	intensity = Float32(loadedIntensity.value);
	return {};
}

Result<void> PointLightComponent::Serialize(ArchiveWriter& writer) const
{
	auto result = WriteColor(writer, color);
	if (result)
	{
		result = writer.Write("intensity", Float64(intensity));
	}
	if (result)
	{
		result = writer.Write("range", Float64(range));
	}
	return result;
}

Result<void> PointLightComponent::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto loadedColor = ReadColor(reader);
	auto loadedIntensity = reader.Number("intensity");
	auto loadedRange = reader.Number("range");
	if (!loadedColor || !loadedIntensity || !loadedRange || !ValidLightColor(loadedColor.value) ||
	    !ValidFloat(loadedIntensity.value) || loadedRange.value <= 0 || !ValidFloat(loadedRange.value))
	{
		return Result<void>::Failure("Invalid PointLight component");
	}
	color = loadedColor.value;
	intensity = Float32(loadedIntensity.value);
	range = Float32(loadedRange.value);
	return {};
}
} // namespace Ursa
