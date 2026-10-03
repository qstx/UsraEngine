#include <serialization/JsonInterop.hpp>
#include <serialization/ComponentRegistry.hpp>
#include <cmath>
#include <limits>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

Json ColorJson(Color color)
{
	return Json::array({color.red, color.green, color.blue, color.alpha});
}

Result<float> number(const Json& data, const char* field)
{
	if (!data.is_object() || !data.contains(field) || !data[field].is_number())
	{
		return Result<float>::Failure(String("Missing or invalid component field ") + field);
	}
	const double value = data[field].get<double>();
	if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
	{
		return Result<float>::Failure(String("Out-of-range component field ") + field);
	}
	return Result<float>::Success(static_cast<float>(value));
}

Result<Color> ParseColor(const Json& data, const char* field)
{
	if (!data.is_object() || !data.contains(field) || !data[field].is_array() || data[field].size() != 4)
	{
		return Result<Color>::Failure(String("Missing or invalid component field ") + field);
	}
	Color color;
	float* channels[] = {&color.red, &color.green, &color.blue, &color.alpha};
	for (size_t index = 0; index < 4; ++index)
	{
		if (!data[field][index].is_number())
		{
			return Result<Color>::Failure(String("Invalid component color field ") + field);
		}
		const double channel = data[field][index].get<double>();
		if (!std::isfinite(channel) || std::abs(channel) > std::numeric_limits<float>::max())
		{
			return Result<Color>::Failure(String("Out-of-range component color field ") + field);
		}
		*channels[index] = static_cast<float>(channel);
	}
	return Result<Color>::Success(color);
}

Result<void> RegisterCamera(ComponentRegistry& registry)
{
	ComponentCodec codec;
	codec.type = "ursa.Camera";
	codec.matches = [](const Component& value) { return dynamic_cast<const CameraComponent*>(&value) != nullptr; };
	codec.save = [](const Component& value, const ComponentSaveContext&) -> Result<Json>
	{
		const auto& camera = static_cast<const CameraComponent&>(value);
		return Result<Json>::Success({{"verticalFovRadians", camera.verticalFovRadians},
		                              {"nearPlane", camera.nearPlane},
		                              {"farPlane", camera.farPlane}});
	};
	codec.create = [](Scene& scene, NodeId node, const Json& data, ComponentLoadContext&)
	{
		const auto fov = number(data, "verticalFovRadians");
		const auto nearPlane = number(data, "nearPlane");
		const auto farPlane = number(data, "farPlane");
		if (!fov || !nearPlane || !farPlane || fov.value <= 0.0f || fov.value >= 3.14159265f ||
		    nearPlane.value <= 0.0f || farPlane.value <= nearPlane.value)
		{
			return Result<ComponentId>::Failure("Invalid camera projection fields");
		}
		auto added = scene.AddComponent<CameraComponent>(node);
		if (!added)
		{
			return added;
		}
		auto* camera = static_cast<CameraComponent*>(scene.GetComponent(added.value));
		camera->verticalFovRadians = fov.value;
		camera->nearPlane = nearPlane.value;
		camera->farPlane = farPlane.value;
		return added;
	};
	return registry.RegisterCodec(std::move(codec));
}

Result<void> RegisterBox(ComponentRegistry& registry)
{
	ComponentCodec codec;
	codec.type = "ursa.BoxRender";
	codec.matches = [](const Component& value) { return dynamic_cast<const BoxRenderComponent*>(&value) != nullptr; };
	codec.save = [](const Component& value, const ComponentSaveContext& context) -> Result<Json>
	{
		const auto& box = static_cast<const BoxRenderComponent&>(value);
		Json material = nullptr;
		if (box.material)
		{
			if (!context.materialId)
			{
				return Result<Json>::Failure("No material ID resolver for BoxRenderComponent");
			}
			auto id = context.materialId(box.material);
			if (!id || !id.value)
			{
				return Result<Json>::Failure("Cannot resolve BoxRenderComponent material ID: " + id.error);
			}
			material = id.value.ToString();
		}
		return Result<Json>::Success({{"color", ColorJson(box.color)}, {"material", std::move(material)}});
	};
	codec.create = [](Scene& scene, NodeId node, const Json& data, ComponentLoadContext& context)
	{
		auto selectedColor = ParseColor(data, "color");
		if (!selectedColor || !data.is_object() || !data.contains("material"))
		{
			return Result<ComponentId>::Failure("Invalid box renderer fields");
		}
		auto added = scene.AddComponent<BoxRenderComponent>(node, selectedColor.value);
		if (!added)
		{
			return added;
		}
		auto& material = data["material"];
		if (!material.is_null())
		{
			if (!material.is_string())
			{
				return Result<ComponentId>::Failure("Invalid box renderer material ID");
			}
			auto id = ResourceId::Parse(material.get<String>());
			if (!id)
			{
				return Result<ComponentId>::Failure(id.error);
			}
			auto loaded = context.Resources().LoadMaterial(id.value);
			if (!loaded)
			{
				return Result<ComponentId>::Failure(loaded.error);
			}
			static_cast<BoxRenderComponent*>(scene.GetComponent(added.value))->material = std::move(loaded.value);
		}
		return added;
	};
	return registry.RegisterCodec(std::move(codec));
}

Result<void> RegisterDirectionalLight(ComponentRegistry& registry)
{
	ComponentCodec codec;
	codec.type = "ursa.DirectionalLight";
	codec.matches = [](const Component& value)
	{ return dynamic_cast<const DirectionalLightComponent*>(&value) != nullptr; };
	codec.save = [](const Component& value, const ComponentSaveContext&) -> Result<Json>
	{
		const auto& light = static_cast<const DirectionalLightComponent&>(value);
		return Result<Json>::Success({{"color", ColorJson(light.Color())}, {"intensity", light.Intensity()}});
	};
	codec.create = [](Scene& scene, NodeId node, const Json& data, ComponentLoadContext&)
	{
		const auto selectedColor = ParseColor(data, "color");
		const auto intensity = number(data, "intensity");
		if (!selectedColor || !intensity)
		{
			return Result<ComponentId>::Failure(!selectedColor ? selectedColor.error : intensity.error);
		}
		auto added = scene.AddComponent<DirectionalLightComponent>(node);
		if (!added)
		{
			return added;
		}
		auto* light = static_cast<DirectionalLightComponent*>(scene.GetComponent(added.value));
		auto changed = light->SetColor(selectedColor.value);
		if (changed)
		{
			changed = light->SetIntensity(intensity.value);
		}
		return changed ? added : Result<ComponentId>::Failure(changed.error);
	};
	return registry.RegisterCodec(std::move(codec));
}

Result<void> RegisterPointLight(ComponentRegistry& registry)
{
	ComponentCodec codec;
	codec.type = "ursa.PointLight";
	codec.matches = [](const Component& value) { return dynamic_cast<const PointLightComponent*>(&value) != nullptr; };
	codec.save = [](const Component& value, const ComponentSaveContext&) -> Result<Json>
	{
		const auto& light = static_cast<const PointLightComponent&>(value);
		return Result<Json>::Success(
		    {{"color", ColorJson(light.Color())}, {"intensity", light.Intensity()}, {"range", light.Range()}});
	};
	codec.create = [](Scene& scene, NodeId node, const Json& data, ComponentLoadContext&)
	{
		const auto selectedColor = ParseColor(data, "color");
		const auto intensity = number(data, "intensity");
		const auto range = number(data, "range");
		if (!selectedColor || !intensity || !range)
		{
			return Result<ComponentId>::Failure(!selectedColor ? selectedColor.error
			                                    : !intensity   ? intensity.error
			                                                   : range.error);
		}
		auto added = scene.AddComponent<PointLightComponent>(node);
		if (!added)
		{
			return added;
		}
		auto* light = static_cast<PointLightComponent*>(scene.GetComponent(added.value));
		auto changed = light->SetColor(selectedColor.value);
		if (changed)
		{
			changed = light->SetIntensity(intensity.value);
		}
		if (changed)
		{
			changed = light->SetRange(range.value);
		}
		return changed ? added : Result<ComponentId>::Failure(changed.error);
	};
	return registry.RegisterCodec(std::move(codec));
}
} // namespace

Result<void> RegisterBuiltinComponents(ComponentRegistry& registry)
{
	auto result = RegisterCamera(registry);
	if (result)
	{
		result = RegisterBox(registry);
	}
	if (result)
	{
		result = RegisterDirectionalLight(registry);
	}
	if (result)
	{
		result = RegisterPointLight(registry);
	}
	return result;
}
} // namespace Ursa
