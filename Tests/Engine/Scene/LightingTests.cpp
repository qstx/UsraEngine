#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Scene/Scene.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/ArchiveFields.hpp>
#include <Serialization/SerializationContext.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return result.value;
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void TestValidation()
{
	Ursa::Scene scene;
	done(scene.SetAmbientLight({0.3f, 0.4f, 0.5f, 1.0f}, 0.2f));
	Expect(!scene.SetAmbientLight({-1.0f, 0.0f, 0.0f, 1.0f}, 0.5f) && !scene.SetAmbientLight({}, -1.0f) &&
	           scene.AmbientLightIntensity() == 0.2f && scene.AmbientLightColor().red == 0.3f,
	       "Invalid ambient light changed the last valid setting");
	const auto directionalNode = Take(scene.CreateGameObject());
	Take(scene.AddComponent<Ursa::DirectionalLightComponent>(directionalNode));
	auto* directional = scene.FindComponent<Ursa::DirectionalLightComponent>(directionalNode);
	done(directional->SetColor({0.8f, 0.6f, 0.2f, 1.0f}));
	done(directional->SetIntensity(2.0f));
	const auto badDirectional = directional->SetIntensity(-1.0f);
	Expect(!badDirectional && badDirectional.error.find("intensity") != Ursa::String::npos &&
	           directional->Intensity() == 2.0f,
	       "Invalid directional intensity changed the light");
	const auto pointNode = Take(scene.CreateGameObject());
	Take(scene.AddComponent<Ursa::PointLightComponent>(pointNode));
	auto* point = scene.FindComponent<Ursa::PointLightComponent>(pointNode);
	done(point->SetRange(4.0f));
	done(point->SetIntensity(3.0f));
	Expect(!point->SetRange(0.0f) && !point->SetIntensity(-0.5f) && point->Range() == 4.0f &&
	           point->Intensity() == 3.0f,
	       "Invalid point light settings changed the light");
}

void TestLegacyShadowDefaultsAndInvalidArchive()
{
	Ursa::ArchiveWriter legacyLight;
	done(Ursa::WriteNumbers(legacyLight, "color", {1.0, 1.0, 1.0, 1.0}));
	done(legacyLight.Write("intensity", 1.0));
	auto legacyLightBytes = Take(Ursa::JsonArchive::Encode(legacyLight));
	auto legacyLightReader = Take(Ursa::JsonArchive::Decode(legacyLightBytes));
	Ursa::DirectionalLightComponent light;
	Ursa::SerializationContext context;
	done(light.Deserialize(legacyLightReader, context));
	Expect(light.CastsShadows() && light.ShadowStrength() == 1.0f && light.ShadowBias() == 0.001f &&
	           light.ShadowNormalBias() == 0.01f && !light.SoftShadows(),
	       "Legacy directional light did not receive compatible shadow defaults");

	Ursa::ArchiveWriter invalidLight;
	done(Ursa::WriteNumbers(invalidLight, "color", {1.0, 1.0, 1.0, 1.0}));
	done(invalidLight.Write("intensity", 1.0));
	done(invalidLight.Write("shadowStrength", 1.1));
	auto invalidLightReader = Take(Ursa::JsonArchive::Encode(invalidLight));
	auto invalidLightArchive = Take(Ursa::JsonArchive::Decode(invalidLightReader));
	Expect(!light.Deserialize(invalidLightArchive, context) && light.ShadowStrength() == 1.0f,
	       "Directional light accepted an invalid serialized shadow strength");

	Ursa::ArchiveWriter legacyRenderer;
	auto materials = Take(legacyRenderer.Array("materials"));
	(void)materials;
	auto legacyRendererBytes = Take(Ursa::JsonArchive::Encode(legacyRenderer));
	auto legacyRendererReader = Take(Ursa::JsonArchive::Decode(legacyRendererBytes));
	Ursa::MeshRendererComponent renderer;
	done(renderer.Deserialize(legacyRendererReader, context));
	Expect(renderer.CastShadows() && renderer.ReceiveShadows(),
	       "Legacy Renderer did not default to casting and receiving shadows");
}

void TestWorldSnapshotAndCapacity()
{
	Ursa::Scene directionalCapacityScene;
	const auto capacityCameraNode = Take(directionalCapacityScene.CreateGameObject());
	const auto capacityCamera = Take(directionalCapacityScene.AddComponent<Ursa::CameraComponent>(capacityCameraNode));
	done(directionalCapacityScene.SetActiveCamera(capacityCamera));
	for (Ursa::UInt32 index = 0; index < Ursa::SceneLighting::maxDirectionalLights; ++index)
	{
		const auto node = Take(directionalCapacityScene.CreateGameObject());
		Take(directionalCapacityScene.AddComponent<Ursa::DirectionalLightComponent>(node));
	}
	Expect(Take(directionalCapacityScene.CollectRenderData(1.0f)).lighting.directional.size() ==
	           Ursa::SceneLighting::maxDirectionalLights,
	       "Directional light capacity boundary failed");
	const auto extraDirectional = Take(directionalCapacityScene.CreateGameObject());
	Take(directionalCapacityScene.AddComponent<Ursa::DirectionalLightComponent>(extraDirectional));
	const auto directionalOverflow = directionalCapacityScene.CollectRenderData(1.0f);
	Expect(!directionalOverflow && directionalOverflow.error.find("5") != Ursa::String::npos &&
	           directionalOverflow.error.find("4") != Ursa::String::npos,
	       "Directional light overflow lacked counts and capacity");

	Ursa::Scene scene;
	const auto cameraNode = Take(scene.CreateGameObject());
	const auto cameraId = Take(scene.AddComponent<Ursa::CameraComponent>(cameraNode));
	done(scene.SetActiveCamera(cameraId));
	const auto parent = Take(scene.CreateGameObject());
	const auto directionalNode = Take(scene.CreateGameObject(parent));
	Take(scene.AddComponent<Ursa::DirectionalLightComponent>(directionalNode));
	const auto pointNode = Take(scene.CreateGameObject(parent));
	Take(scene.AddComponent<Ursa::PointLightComponent>(pointNode));
	Ursa::LocalTransform parentTransform;
	parentTransform.position = {2.0f, 0.0f, 0.0f};
	parentTransform.rotation = Ursa::Quat::FromAxisAngle({0.0f, 1.0f, 0.0f}, 1.57079633f);
	done(scene.SetLocalTransform(parent, parentTransform));
	Ursa::LocalTransform pointTransform;
	pointTransform.position = {0.0f, 0.0f, -1.0f};
	done(scene.SetLocalTransform(pointNode, pointTransform));
	const auto frame = Take(scene.CollectRenderData(1.0f));
	Expect(frame.lighting.directional.size() == 1 && frame.lighting.point.size() == 1 &&
	           std::abs(frame.lighting.directional[0].direction.x + 1.0f) < 0.0001f &&
	           std::abs(frame.lighting.point[0].position.x - 1.0f) < 0.0001f,
	       "Scene lights did not use world transforms");
	pointTransform.position = {0.0f, 0.0f, -2.0f};
	done(scene.SetLocalTransform(pointNode, pointTransform));
	const auto movedFrame = Take(scene.CollectRenderData(1.0f));
	Expect(std::abs(frame.lighting.point[0].position.x - 1.0f) < 0.0001f &&
	           std::abs(movedFrame.lighting.point[0].position.x) < 0.0001f,
	       "A collected frame did not retain its own light snapshot");
	done(scene.SetActive(parent, false));
	Expect(Take(scene.CollectRenderData(1.0f)).lighting.point.empty() &&
	           Take(scene.CollectRenderData(1.0f)).lighting.directional.empty(),
	       "Disabled ancestor still contributed lights");
	done(scene.SetActive(parent, true));
	Expect(Take(scene.CollectRenderData(1.0f)).lighting.point.size() == 1, "Reenabled ancestor did not restore lights");
	for (Ursa::UInt32 index = 1; index < Ursa::SceneLighting::maxPointLights; ++index)
	{
		const auto node = Take(scene.CreateGameObject());
		Take(scene.AddComponent<Ursa::PointLightComponent>(node));
	}
	Expect(Take(scene.CollectRenderData(1.0f)).lighting.point.size() == Ursa::SceneLighting::maxPointLights,
	       "Point light capacity boundary failed");
	const auto extra = Take(scene.CreateGameObject());
	Take(scene.AddComponent<Ursa::PointLightComponent>(extra));
	const auto overflow = scene.CollectRenderData(1.0f);
	Expect(!overflow && overflow.error.find("9") != Ursa::String::npos &&
	           overflow.error.find("8") != Ursa::String::npos,
	       "Point light overflow lacked counts and capacity");
}
} // namespace

int main()
{
	try
	{
		TestValidation();
		TestLegacyShadowDefaultsAndInvalidArchive();
		TestWorldSnapshotAndCapacity();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
