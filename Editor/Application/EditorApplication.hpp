#pragma once

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/Result.hpp>
#include <Rhi/RHI.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Window/Window.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Assets/MaterialPreview.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Scene/EditorScenePreview.hpp>
#include <Scene/EditorGizmo.hpp>
#include <Scene/ComponentEditors.hpp>
#include <Application/PlaySession.hpp>
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

	Result<void> Run(Ursa::UInt32 width = 1280, Ursa::UInt32 height = 720, const String& title = "Ursa Editor",
	                 bool visible = true, Ursa::UInt32 frameLimit = 0);

	Result<void> OpenProject(const std::filesystem::path& root, const std::filesystem::path& package = {});

protected:
	virtual Result<void> OnStart()
	{
		return {};
	}

	virtual Result<void> OnFrame(Ursa::Float32 deltaSeconds)
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
	GizmoOperation gizmoOperation = GizmoOperation::Translate;
	PlaySession play;
	bool viewportFocused = false;
	std::unique_ptr<ProjectWorkspace> workspace;
	std::unique_ptr<EditorDocuments> documents;
	Ursa::Array<char, 256> assetNameInput{};
	String workspaceError;
	String inspectorError;
	bool exitRequested = false;
	bool dockLayoutInitialized = false;
};
} // namespace Ursa::Editor
