#pragma once

#include <core/Result.hpp>
#include <rhi/RHI.hpp>
#include <serialization/ComponentRegistry.hpp>
#include <window/Window.hpp>
#include <ui/ImGuiBridge.hpp>
#include <workspace/EditorDocuments.hpp>
#include <assets/MaterialPreview.hpp>
#include <scene/EditorHierarchy.hpp>
#include <scene/EditorScenePreview.hpp>
#include <scene/EditorGizmo.hpp>
#include <scene/ComponentEditors.hpp>
#include <application/PlaySession.hpp>
#include <array>
#include <functional>

namespace Ursa::Editor
{
struct EditorProject
{
	ComponentRegistry components;
	ComponentEditors componentEditors;
	std::function<Result<void>(ComponentLoadContext&)> editContextSetup;
	PlayContextSetup playContextSetup;
};

using EditorProjectSetup = std::function<Result<void>(EditorProject&)>;

class EditorApplication
{
public:
	explicit EditorApplication(EditorProjectSetup setup = {}) : projectSetup(std::move(setup))
	{
	}

	virtual ~EditorApplication() = default;

	Result<void> Run(uint32_t width = 1280, uint32_t height = 720, const String& title = "Ursa Editor",
	                 bool visible = true, uint32_t frameLimit = 0);

	Result<void> OpenProject(const std::filesystem::path& root);

protected:
	virtual Result<void> OnStart()
	{
		return {};
	}

	virtual Result<void> OnFrame(float deltaSeconds)
	{
		(void)deltaSeconds;
		return {};
	}

	virtual Result<void> OnGui();

	virtual void OnStop()
	{
	}

	std::shared_ptr<Window> window;
	Rhi::DevicePtr device;
	Rhi::SwapchainPtr swapchain;

	ImGuiBridge& Gui()
	{
		return *gui;
	}

	EditorProject& Project()
	{
		return *project;
	}

	ProjectWorkspace* Workspace() const
	{
		return workspace.get();
	}

	EditorDocuments* Documents() const
	{
		return documents.get();
	}

private:
	EditorProjectSetup projectSetup;
	std::unique_ptr<EditorProject> project;
	std::unique_ptr<ImGuiBridge> gui;
	std::unique_ptr<MaterialPreview> materialPreview;
	std::unique_ptr<EditorScenePreview> scenePreview;
	EditorHierarchy hierarchy;
	Rhi::TexturePtr scenePreviewColor;
	Rhi::SamplerPtr scenePreviewSampler;
	ImTextureID scenePreviewId = ImTextureID_Invalid;
	bool scenePreviewRendered = false;
	ResourceId displayedSceneId;
	GizmoOperation gizmoOperation = GizmoOperation::translate;
	PlaySession play;
	bool viewportFocused = false;
	std::unique_ptr<ProjectWorkspace> workspace;
	std::unique_ptr<EditorDocuments> documents;
	std::array<char, 256> assetNameInput{};
	String workspaceError;
	String inspectorError;
	bool exitRequested = false;
	bool dockLayoutInitialized = false;
};
} // namespace Ursa::Editor
