#include <Scene/SceneViewPanel.hpp>

#include <Ui/ImGuiBridge.hpp>
#include <imgui.h>
#include <algorithm>

namespace Ursa::Editor
{
Result<std::unique_ptr<SceneViewPanel>> SceneViewPanel::Create(Rhi::DevicePtr selectedDevice, ImGuiBridge& gui)
{
	if (!selectedDevice)
	{
		return Result<std::unique_ptr<SceneViewPanel>>::Failure("Scene view requires an RHI device");
	}
	auto panel = std::unique_ptr<SceneViewPanel>(new SceneViewPanel(std::move(selectedDevice), gui));
	auto initialized = panel->Initialize();
	if (!initialized)
	{
		return Result<std::unique_ptr<SceneViewPanel>>::Failure(initialized.error);
	}
	return Result<std::unique_ptr<SceneViewPanel>>::Success(std::move(panel));
}

SceneViewPanel::~SceneViewPanel()
{
	if (gui && imageId != ImTextureID_Invalid)
	{
		gui->UnregisterTexture(imageId);
	}
}

Result<void> SceneViewPanel::Initialize()
{
	auto selectedPreview = EditorScenePreview::Create(device);
	if (!selectedPreview)
	{
		return Result<void>::Failure(selectedPreview.error);
	}
	preview = std::move(selectedPreview.value);
	auto selectedColor = device->CreateTexture(
	    {512, 512, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled});
	auto selectedSampler = device->CreateSampler({true, false});
	if (!selectedColor || !selectedSampler)
	{
		return Result<void>::Failure(!selectedColor ? selectedColor.error : selectedSampler.error);
	}
	color = std::move(selectedColor.value);
	sampler = std::move(selectedSampler.value);
	auto registered = gui->RegisterTexture(color, sampler);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	imageId = registered.value;
	return {};
}

Result<void> SceneViewPanel::Draw(SceneDraft& draft, EditorDocument& document, EditorHierarchy& hierarchy,
                                  PlaySession& play, bool& showScene, bool& showGame, bool& viewportFocused,
                                  String& diagnostic)
{
	viewportFocused = false;
	auto command = device->CreateCommandList();
	Result<void> recorded = command ? command.value->Begin() : Result<void>::Failure(command.error);
	if (recorded)
	{
		const auto initial = rendered ? Rhi::ResourceState::ShaderRead : Rhi::ResourceState::Undefined;
		recorded =
		    play.Active()
		        ? preview->RecordGame(*play.GetScene(), command.value, color, initial, Rhi::ResourceState::ShaderRead)
		        : preview->Record(draft.GetScene(), command.value, color, initial, Rhi::ResourceState::ShaderRead);
	}
	if (recorded)
	{
		recorded = command.value->End();
	}
	if (recorded)
	{
		auto submitted = device->Submit(command.value);
		recorded = submitted ? submitted.value->Wait() : Result<void>::Failure(submitted.error);
	}
	if (recorded)
	{
		rendered = true;
	}
	else
	{
		diagnostic = recorded.error;
	}
	if (showScene)
	{
		ImGui::Begin("Scene", &showScene);
		ImGui::BeginDisabled(play.Active());
		if (ImGui::Button("Translate"))
		{
			gizmoOperation = GizmoOperation::Translate;
		}
		ImGui::SameLine();
		if (ImGui::Button("Rotate"))
		{
			gizmoOperation = GizmoOperation::Rotate;
		}
		ImGui::SameLine();
		if (ImGui::Button("Scale"))
		{
			gizmoOperation = GizmoOperation::Scale;
		}
		ImGui::EndDisabled();
		if (recorded)
		{
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const Ursa::Float32 imageSize = std::max(1.f, std::min(available.x, available.y));
			const ImVec2 position = ImGui::GetCursorScreenPos();
			ImGui::Image(imageId, ImVec2(imageSize, imageSize));
			GameObject::Ptr gizmoTarget;
			ResourceId prefabObjectId;
			if (draft.Kind() == ResourceKind::Prefab && draft.GetPrefabAsset())
			{
				auto selectedAssetObject = hierarchy.SelectedGameObject(*draft.GetPrefabAsset());
				if (selectedAssetObject)
				{
					prefabObjectId = selectedAssetObject.value->Id();
					auto selectedPreviewObject = draft.PreviewGameObject(prefabObjectId);
					if (selectedPreviewObject)
					{
						gizmoTarget = selectedPreviewObject.value;
					}
				}
			}
			else
			{
				auto selected = hierarchy.SelectedGameObject(draft.GetScene());
				if (selected)
				{
					gizmoTarget = selected.value;
				}
			}
			if (gizmoTarget && !play.Active())
			{
				auto manipulated = DrawGizmo(draft.GetScene(), gizmoTarget, preview->GetCamera(),
				                             {position.x, position.y, imageSize, imageSize}, gizmoOperation);
				if (manipulated && manipulated.value)
				{
					if (prefabObjectId)
					{
						auto synced = draft.SyncPrefabTransform(prefabObjectId);
						if (synced)
						{
							synced = draft.RefreshPrefabPreview();
						}
						if (!synced)
						{
							diagnostic = synced.error;
						}
					}
					document.MarkDirty();
				}
				else if (!manipulated)
				{
					diagnostic = manipulated.error;
				}
			}
		}
		else
		{
			ImGui::TextWrapped("%s", diagnostic.c_str());
		}
		ImGui::End();
	}
	if (showGame)
	{
		ImGui::Begin("Game", &showGame);
		if (play.Active() && rendered)
		{
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const Ursa::Float32 imageSize = std::max(1.f, std::min(available.x, available.y));
			ImGui::Image(imageId, ImVec2(imageSize, imageSize));
			viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsItemHovered();
		}
		else
		{
			ImGui::TextDisabled("Enter Play mode to preview the game camera.");
		}
		ImGui::End();
	}
	return recorded ? Result<void>{} : Result<void>::Failure(recorded.error);
}

void SceneViewPanel::DrawEmpty(bool& showScene, bool& showGame)
{
	if (showScene)
	{
		ImGui::Begin("Scene", &showScene);
		ImGui::TextDisabled("Open a scene to view and edit it.");
		ImGui::End();
	}
	if (showGame)
	{
		ImGui::Begin("Game", &showGame);
		ImGui::TextDisabled("Open a scene and enter Play mode to preview the game camera.");
		ImGui::End();
	}
}
} // namespace Ursa::Editor
