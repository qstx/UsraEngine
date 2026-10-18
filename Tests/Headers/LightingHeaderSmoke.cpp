#include <Scene/Scene.hpp>

int TestLightingHeader()
{
	Ursa::Scene litScene;
	auto cameraNode = litScene.CreateGameObject().value;
	litScene.SetActiveCamera(litScene.AddComponent<Ursa::CameraComponent>(cameraNode).value);
	auto sunNode = litScene.CreateGameObject().value;
	litScene.AddComponent<Ursa::DirectionalLightComponent>(sunNode);
	auto lampNode = litScene.CreateGameObject().value;
	litScene.AddComponent<Ursa::PointLightComponent>(lampNode);
	litScene.SetAmbientLight({1.0f, 1.0f, 1.0f, 1.0f}, 0.1f);
	auto frame = litScene.CollectRenderData(16.0f / 9.0f);
	return frame && frame.value.lighting.directional.size() == 1 && frame.value.lighting.point.size() == 1 ? 0 : 1;
}
