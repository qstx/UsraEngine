#include <Scene/RenderingComponents.hpp>
#include <Serialization/ArchiveFields.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/JsonArchiveInterop.hpp>

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
	auto valid = rendering.Validate();
	if (!valid)
	{
		return valid;
	}
	auto result = writer.Write("verticalFovRadians", Float64(verticalFovRadians));
	if (result)
	{
		result = writer.Write("nearPlane", Float64(nearPlane));
	}
	if (result)
	{
		result = writer.Write("farPlane", Float64(farPlane));
	}
	if (!result)
	{
		return result;
	}
	auto settings = writer.Object("rendering");
	return settings ? ArchiveWriteJson(settings.value, {{"type", UInt32(rendering.type)},
	                                                    {"depth", rendering.depth},
	                                                    {"clearFlags", UInt32(rendering.clearFlags)},
	                                                    {"background",
	                                                     {rendering.background.red, rendering.background.green,
	                                                      rendering.background.blue, rendering.background.alpha}},
	                                                    {"cullingMask", rendering.cullingMask},
	                                                    {"viewport",
	                                                     {rendering.viewport.x, rendering.viewport.y,
	                                                      rendering.viewport.width, rendering.viewport.height}},
	                                                    {"targetWidth", rendering.targetWidth},
	                                                    {"targetHeight", rendering.targetHeight}})
	                : Result<void>::Failure(settings.error);
}

Result<void> CameraComponent::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto fov = reader.Number("verticalFovRadians");
	auto nearValue = reader.Number("nearPlane");
	auto farValue = reader.Number("farPlane");
	Rendering::CameraSettings loadedSettings;
	if (reader.Contains("rendering"))
	{
		auto settings = reader.Object("rendering");
		auto document = settings ? ArchiveToJson(settings.value) : Result<nlohmann::json>::Failure(settings.error);
		if (!document)
		{
			return Result<void>::Failure(document.error);
		}
		try
		{
			const auto& value = document.value;
			loadedSettings.type = static_cast<Rendering::CameraType>(value.at("type").get<UInt32>());
			loadedSettings.depth = value.at("depth").get<Float32>();
			loadedSettings.clearFlags = static_cast<Rendering::CameraClearFlags>(value.at("clearFlags").get<UInt32>());
			loadedSettings.cullingMask = value.at("cullingMask").get<UInt32>();
			auto background = ReadNumbers(settings.value, "background", 4);
			auto viewport = ReadNumbers(settings.value, "viewport", 4);
			if (!background || !viewport)
			{
				return Result<void>::Failure("Invalid camera background or viewport");
			}
			loadedSettings.background = {background.value[0], background.value[1], background.value[2],
			                             background.value[3]};
			loadedSettings.viewport = {viewport.value[0], viewport.value[1], viewport.value[2], viewport.value[3]};
			loadedSettings.targetWidth = value.at("targetWidth").get<UInt32>();
			loadedSettings.targetHeight = value.at("targetHeight").get<UInt32>();
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(String("Invalid camera rendering settings: ") + error.what());
		}
	}
	auto valid = loadedSettings.Validate();
	if (!valid || !fov || !nearValue || !farValue || fov.value <= 0 || fov.value >= 3.141592653589793 ||
	    nearValue.value <= 0 || farValue.value <= nearValue.value || !ValidFloat(farValue.value))
	{
		return Result<void>::Failure("Invalid Camera component projection");
	}
	verticalFovRadians = Float32(fov.value);
	nearPlane = Float32(nearValue.value);
	farPlane = Float32(farValue.value);
	rendering = loadedSettings;
	return {};
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
