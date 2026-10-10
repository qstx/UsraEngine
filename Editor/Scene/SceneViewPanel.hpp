#pragma once

#include <Application/PlaySession.hpp>
#include <Assets/SceneDraft.hpp>
#include <Scene/EditorGizmo.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <memory>

namespace Ursa::Editor
{
class SceneViewPanel final
{
public:
	static Result<std::unique_ptr<SceneViewPanel>> Create(Rhi::DevicePtr device, ImGuiBridge& gui);

	~SceneViewPanel();

	SceneViewPanel(const SceneViewPanel&) = delete;
	SceneViewPanel& operator=(const SceneViewPanel&) = delete;

	Result<void> Draw(SceneDraft& draft, EditorDocument& document, EditorHierarchy& hierarchy, PlaySession& play,
	                  bool& showScene, bool& showGame, bool& viewportFocused, String& diagnostic);

	void DrawEmpty(bool& showScene, bool& showGame);

	Result<void> ReloadResources();

private:
	SceneViewPanel(Rhi::DevicePtr selectedDevice, ImGuiBridge& selectedGui)
	    : device(std::move(selectedDevice)), gui(&selectedGui)
	{
	}

	Result<void> Initialize();

	Rhi::DevicePtr device;
	ImGuiBridge* gui = nullptr;
	std::unique_ptr<EditorScenePreview> preview;
	Rhi::TexturePtr color;
	Rhi::SamplerPtr sampler;
	ImTextureID imageId = ImTextureID_Invalid;
	bool rendered = false;
	GizmoOperation gizmoOperation = GizmoOperation::Translate;
};
} // namespace Ursa::Editor
