#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <scene/Scene.hpp>
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
	const auto directionalNode = Take(scene.CreateNode());
	Take(scene.AddComponent<Ursa::DirectionalLightComponent>(directionalNode));
	auto* directional = scene.FindComponent<Ursa::DirectionalLightComponent>(directionalNode);
	done(directional->SetColor({0.8f, 0.6f, 0.2f, 1.0f}));
	done(directional->SetIntensity(2.0f));
	const auto badDirectional = directional->SetIntensity(-1.0f);
	Expect(!badDirectional && badDirectional.error.find("intensity") != Ursa::String::npos &&
	           directional->Intensity() == 2.0f,
	       "Invalid directional intensity changed the light");
	const auto pointNode = Take(scene.CreateNode());
	Take(scene.AddComponent<Ursa::PointLightComponent>(pointNode));
	auto* point = scene.FindComponent<Ursa::PointLightComponent>(pointNode);
	done(point->SetRange(4.0f));
	done(point->SetIntensity(3.0f));
	Expect(!point->SetRange(0.0f) && !point->SetIntensity(-0.5f) && point->Range() == 4.0f &&
	           point->Intensity() == 3.0f,
	       "Invalid point light settings changed the light");
}

void TestWorldSnapshotAndCapacity()
{
	Ursa::Scene scene;
	const auto cameraNode = Take(scene.CreateNode());
	const auto cameraId = Take(scene.AddComponent<Ursa::CameraComponent>(cameraNode));
	done(scene.SetActiveCamera(cameraId));
	const auto parent = Take(scene.CreateNode());
	const auto directionalNode = Take(scene.CreateNode(parent));
	Take(scene.AddComponent<Ursa::DirectionalLightComponent>(directionalNode));
	const auto pointNode = Take(scene.CreateNode(parent));
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
	done(scene.SetEnabled(parent, false));
	Expect(Take(scene.CollectRenderData(1.0f)).lighting.point.empty() &&
	           Take(scene.CollectRenderData(1.0f)).lighting.directional.empty(),
	       "Disabled ancestor still contributed lights");
	done(scene.SetEnabled(parent, true));
	Expect(Take(scene.CollectRenderData(1.0f)).lighting.point.size() == 1, "Reenabled ancestor did not restore lights");
	for (uint32_t index = 1; index < Ursa::SceneLighting::maxPointLights; ++index)
	{
		const auto node = Take(scene.CreateNode());
		Take(scene.AddComponent<Ursa::PointLightComponent>(node));
	}
	Expect(Take(scene.CollectRenderData(1.0f)).lighting.point.size() == Ursa::SceneLighting::maxPointLights,
	       "Point light capacity boundary failed");
	const auto extra = Take(scene.CreateNode());
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
		TestWorldSnapshotAndCapacity();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
