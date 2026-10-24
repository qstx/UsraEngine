#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <Assets/AssetPreviewPanel.hpp>
#include <Assets/ProjectBrowserPanel.hpp>
#include <Assets/ImportedAssetInspector.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/TextureDraft.hpp>
#include <Assets/ModelImporter.hpp>
#include <Assets/ModelImportView.hpp>
#include <Inspector/MeshInspector.hpp>
#include <Scripting/Bindings/EditorServices.hpp>
#include <Editor.generated.hpp>
#include <Assets/TextureImportView.hpp>
#include <Inspector/AssetInspectorPanel.hpp>
#include <Scene/SceneViewPanel.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/Bindings/BindingRequirements.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <iomanip>
#include <sstream>
#endif
#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <RenderingFixtures.hpp>
#include <MeshFixtures.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Core/StringInterop.hpp>
#include <imgui_internal.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

AnimatorComponent* FindAnimator(const GameObject::Ptr& object)
{
	if (auto* animator = object->FindComponent<AnimatorComponent>())
	{
		return animator;
	}
	for (const auto& child : object->Children())
	{
		if (auto* animator = FindAnimator(child))
		{
			return animator;
		}
	}
	return nullptr;
}

SkinnedMeshRendererComponent* FindSkinnedRenderer(const GameObject::Ptr& object)
{
	if (auto* renderer = object->FindComponent<SkinnedMeshRendererComponent>())
	{
		return renderer;
	}
	for (const auto& child : object->Children())
	{
		if (auto* renderer = FindSkinnedRenderer(child))
		{
			return renderer;
		}
	}
	return nullptr;
}

VirtualPath Path(const char* text)
{
	return Take(VirtualPath::Parse(text));
}

class Probe final : public Component
{
public:
	static inline int callbacks = 0;

	String SerializationType() const override
	{
		return "test.PreviewProbe";
	}

	Result<void> Serialize(ArchiveWriter&) const override
	{
		return {};
	}

	Result<void> Deserialize(const ArchiveReader&, SerializationContext&) override
	{
		return {};
	}

	Result<void> Awake(const FrameContext&) override
	{
		++callbacks;
		return {};
	}

	Result<void> Start(const FrameContext&) override
	{
		++callbacks;
		return {};
	}

	Result<void> Update(const FrameContext&) override
	{
		++callbacks;
		return {};
	}
};

Vector<UInt8> Readback(Rhi::DevicePtr device, const Rhi::TexturePtr& image, Rhi::ResourceState before)
{
	Vector<UInt8> pixels(Size(image->desc.width) * image->desc.height * 4);
	auto buffer =
	    Take(device->CreateBuffer({pixels.size(), Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Done(command->Barrier(image, before, Rhi::ResourceState::TransferSource));
	Done(command->Barrier(buffer, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
	Done(command->CopyTextureToBuffer(image, buffer));
	Done(command->Barrier(image, Rhi::ResourceState::TransferSource, before));
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	Done(buffer->Read(0, pixels.data(), pixels.size()));
	return pixels;
}

Size Difference(const Vector<UInt8>& a, const Vector<UInt8>& b)
{
	Expect(a.size() == b.size(), "Compared preview images have different sizes");
	Size changed = 0;
	for (Size i = 0; i < a.size(); i += 4)
	{
		if (std::abs(int(a[i]) - b[i]) > 3 || std::abs(int(a[i + 1]) - b[i + 1]) > 3 ||
		    std::abs(int(a[i + 2]) - b[i + 2]) > 3)
		{
			++changed;
		}
	}
	return changed;
}

class SettingsProvider final : public VirtualFileProvider
{
public:
	bool failRead = false;
	bool failWrite = false;
	MemoryFileProvider storage;

	String Identity() const override
	{
		return storage.Identity();
	}

	String SourceLabel() const override
	{
		return "preview-settings-test";
	}

	Result<bool> Exists(const String& path) const override
	{
		return storage.Exists(path);
	}

	Result<UInt64> Size(const String& path) const override
	{
		return storage.Size(path);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return failRead ? Result<Vector<UInt8>>::Failure("Injected settings read failure")
		                : storage.ReadRange(path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		return storage.List();
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool directories) override
	{
		return failWrite ? Result<void>::Failure("Injected settings write failure")
		                 : storage.WriteAtomic(path, bytes, directories);
	}

	Result<void> Append(const String& path, const Vector<UInt8>& bytes, bool directories) override
	{
		return storage.Append(path, bytes, directories);
	}
};

struct Harness
{
	VirtualFileSystem files;
	std::shared_ptr<SettingsProvider> settings = std::make_shared<SettingsProvider>();
	std::unique_ptr<ProjectWorkspace> workspace;
	std::unique_ptr<EditorDocuments> documents;
	ComponentRegistry registry;
	ProjectBrowserModel browser;
	EditorSelectionContext selection;
	Rhi::DevicePtr device;
	std::unique_ptr<ImGuiBridge> gui;
	std::unique_ptr<Test::SceneRenderer> renderer;
	std::unique_ptr<AssetPreviewPanel> preview;
	AssetInspectorPanel inspector;
	ProjectBrowserPanel projectPanel;
	bool drawProject = false;
	std::unique_ptr<SceneViewPanel> views;
	EditorHierarchy hierarchy;
	PlaySession play;
	std::shared_ptr<SceneDraft> author;
	Vector<UInt8> lastPixels;
	Vector<Mat4> lastPalette;
	Vector<ScriptTypeDescriptor> scriptTypes;
	Rhi::TexturePtr canvas;
	std::filesystem::path evidence;
	Float32 dpi = 1;
	Float32 inspectorWidth = 440;
	Float32 inspectorHeight = 850;
	bool docked = false;
	bool show = true;
	bool failRender = false;
	std::function<void()> duringRender;
	std::function<void()> extraUi;
	bool focused = false;
	String diagnostic;
	String projectKey = "preview-test";
	UInt64 previewCalls = 0;
	UInt32 publishedPreviewVersion = 0;
	Size vertexCount = 0;
	ResourceId previewCamera;
	Vector<UInt8> sourceBytes;
	Vector<UInt8> materialBytes;
	Vector<UInt8> sceneBytes;
	VirtualPath materialPath = Path("prj:A.ursamat");
	VirtualPath otherMaterial = Path("prj:B.ursamat");
	VirtualPath modelPath = Path("prj:Animated.fbx");
	VirtualPath staticGltfPath = Path("prj:Static.glb");
	VirtualPath staticTextGltfPath = Path("prj:External/BoxTextured.gltf");
	VirtualPath texturePath = Path("prj:Alpha.png");
	VirtualPath prefabPath = Path("prj:Probe.ursaprefab");
	UInt64 meshId = 0;
	UInt64 secondMeshId = 0;
	ResourceId staticTextGltfGuid;
	Vector<UInt64> staticTextGltfMeshIds;
	UInt64 clipId = 0;
	Float64 clipDuration = 0;

	explicit Harness(std::filesystem::path output, bool managed = false) : evidence(std::move(output))
	{
		files = Take(OpenEngineFileSystem());
		Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		if (managed)
		{
			std::filesystem::create_directories(evidence / "ManagedUser");
			Done(files.Mount(userDataScheme, evidence / "ManagedUser", MountAccess::ReadWrite));
		}
		else
		{
			Done(files.MountProvider("user", settings, MountAccess::ReadWrite));
		}
		workspace = Take(ProjectWorkspace::FromFilesystem({}, files));
		documents = Take(EditorDocuments::Create(*workspace));
		Done(RegisterBuiltinComponents(registry));
		Done(registry.RegisterSerializable<Probe>(
		    "test.PreviewProbe", 1,
		    [](const GameObject::Ptr& object, ComponentLoadContext&) -> Result<Component*>
		    {
			    auto added = object->AddComponent<Probe>();
			    return added ? Result<Component*>::Success(added.value) : Result<Component*>::Failure(added.error);
		    }));
		for (const auto& path : {materialPath, otherMaterial})
		{
			auto document = Take(NewMaterialDocument(path, MaterialType::Unlit, *workspace));
			Done(document->Payload<MaterialDraft>()->Material().SetBaseColor(
			    path == materialPath ? Color{.9f, .08f, .04f, 1} : Color{.04f, .8f, .08f, 1}));
			Done(documents->RequestOpen(std::move(document)));
			Done(documents->SaveCurrent());
		}
		materialBytes = Take(files.Read(materialPath));
		Vector<UInt8> png;
		const Vector<UInt8> pixels{128, 64, 32, 128, 32, 128, 64, 255, 64, 32, 128, 0, 255, 128, 64, 255,
		                           128, 64, 32, 128, 32, 128, 64, 255, 64, 32, 128, 0, 255, 128, 64, 255};
		Expect(stbi_write_png_to_func(
		           [](void* context, void* data, int size)
		           {
			           auto& bytes = *static_cast<Vector<UInt8>*>(context);
			           const auto* begin = static_cast<UInt8*>(data);
			           bytes.insert(bytes.end(), begin, begin + size);
		           },
		           &png, 4, 2, 4, pixels.data(), 16) != 0,
		       "Cannot encode transparent PNG fixture");
		Done(files.WriteAtomic(texturePath, png));
		std::ifstream input(URSA_PREVIEW_ANIMATED_FBX, std::ios::binary | std::ios::ate);
		Expect(bool(input), "Animated FBX fixture is unavailable");
		sourceBytes.resize(static_cast<Size>(input.tellg()));
		input.seekg(0);
		input.read(reinterpret_cast<char*>(sourceBytes.data()), static_cast<std::streamsize>(sourceBytes.size()));
		Done(files.WriteAtomic(modelPath, sourceBytes));
		Done(workspace->Refresh());
		Take(TextureImporter::Import(*workspace->Database(), texturePath, TextureImportSettings{}));
		const auto imported = Take(ModelImporter::Import(*workspace->Database(), modelPath, ModelImportSettings{}));
		std::ifstream glbInput(URSA_PREVIEW_STATIC_GLB, std::ios::binary | std::ios::ate);
		Expect(bool(glbInput), "Static glTF binary fixture is unavailable");
		Vector<UInt8> glbBytes(static_cast<Size>(glbInput.tellg()));
		glbInput.seekg(0);
		glbInput.read(reinterpret_cast<char*>(glbBytes.data()), static_cast<std::streamsize>(glbBytes.size()));
		Expect(bool(glbInput), "Cannot read static glTF binary fixture");
		Done(files.WriteAtomic(staticGltfPath, glbBytes));
		Done(workspace->Refresh());
		const auto staticGltf =
		    Take(ModelImporter::Import(*workspace->Database(), staticGltfPath, ModelImportSettings{}));
		const auto staticGltfBytes = Take(files.Read(staticGltf.artifactPath));
		const auto staticGltfModel = nlohmann::json::from_cbor(staticGltfBytes.begin(), staticGltfBytes.end());
		Expect(!staticGltfModel["meshLocalFileIds"].empty() && !staticGltfModel["materialLocalFileIds"].empty(),
		       "Static GLB did not import geometry and material sub-assets");
		for (const auto& entry : std::filesystem::directory_iterator(URSA_PREVIEW_STATIC_GLTF_DIR))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}
			std::ifstream gltfInput(entry.path(), std::ios::binary | std::ios::ate);
			Expect(bool(gltfInput), "External glTF fixture file is unavailable");
			Vector<UInt8> gltfBytes(static_cast<Size>(gltfInput.tellg()));
			gltfInput.seekg(0);
			gltfInput.read(reinterpret_cast<char*>(gltfBytes.data()), static_cast<std::streamsize>(gltfBytes.size()));
			Expect(bool(gltfInput), "Cannot read external glTF fixture file");
			const auto virtualPath =
			    Take(VirtualPath::Parse("prj:External/" + PathToUrsaString(entry.path().filename())));
			Done(files.WriteAtomic(virtualPath, gltfBytes));
		}
		Done(workspace->Refresh());
		const auto staticTextGltf =
		    Take(ModelImporter::Import(*workspace->Database(), staticTextGltfPath, ModelImportSettings{}));
		staticTextGltfGuid = staticTextGltf.guid;
		for (const auto& artifact : staticTextGltf.subAssets)
		{
			if (artifact.kind == "Mesh")
			{
				staticTextGltfMeshIds.push_back(artifact.localFileId);
			}
		}
		const auto staticTextGltfBytes = Take(files.Read(staticTextGltf.artifactPath));
		const auto staticTextGltfModel =
		    nlohmann::json::from_cbor(staticTextGltfBytes.begin(), staticTextGltfBytes.end());
		Expect(!staticTextGltfModel["meshLocalFileIds"].empty() && !staticTextGltfModel["materialLocalFileIds"].empty(),
		       "External glTF did not import geometry and material sub-assets");
		const auto bytes = Take(files.Read(imported.artifactPath));
		const auto model = nlohmann::json::from_cbor(bytes.begin(), bytes.end());
		meshId = model["meshLocalFileIds"][0].get<UInt64>();
		clipId = model["animationClipLocalFileIds"][0].get<UInt64>();
		// A second local mesh identity uses deliberately different geometry and statistics.
		const String obj = "mtllib Two.mtl\no Small\nusemtl Red\nv -1 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no Large\nusemtl "
		                   "Green\nv 2 0 0\nv 4 0 0\nv 3 2 0\nv 3 0 1\nf 4 5 6\nf 4 7 5\n";
		const String mtl = "newmtl Red\nKd 0.9 0.05 0.02\nnewmtl Green\nKd 0.02 0.9 0.05\n";
		Done(files.WriteAtomic(Path("prj:Two.obj"), Vector<UInt8>(obj.data(), obj.data() + obj.size())));
		Done(files.WriteAtomic(Path("prj:Two.mtl"), Vector<UInt8>(mtl.data(), mtl.data() + mtl.size())));
		Done(workspace->Refresh());
		Take(ModelImporter::Import(*workspace->Database(), Path("prj:Two.obj"), ModelImportSettings{}));
		Done(workspace->Refresh());
		Done(documents->RefreshResources());
		clipDuration = Take(documents->Session().LoadAnimationClip(imported.guid, clipId))->durationSeconds;
		auto prefab = Take(SceneDraft::Create(ResourceKind::Prefab, registry, documents->Session()));
		auto root = prefab->GetPrefabAsset()->Root();
		Take(root->AddComponent<Probe>());
		Take(root->AddComponent<Scripting::ScriptBehaviourComponent>("test.PreviewScript"))
		    ->SetSerializedPayload(R"({"Fields":{}})");
		Take(Test::AddRenderer(root));
		Done(prefab->AssignMaterial(root, Take(workspace->Database()->Metadata(materialPath)).guid,
		                            documents->Session()));
		auto disabled = GameObject::Create("Disabled far geometry");
		Done(disabled->SetParent(root));
		Take(Test::AddRenderer(disabled));
		Done(prefab->AssignMaterial(disabled, Take(workspace->Database()->Metadata(otherMaterial)).guid,
		                            documents->Session()));
		LocalTransform pose;
		pose.position = {1000, 1000, 1000};
		Done(disabled->SetLocalTransform(pose));
		Done(disabled->SetActive(false));
		Done(prefab->Save(files, prefabPath, ResourceId::Generate()));
		Done(workspace->Refresh());
		Done(browser.Refresh(*workspace));
		auto document =
		    Take(NewSceneDocument(Path("prj:Unsaved.ursascene"), ResourceKind::Scene, registry, documents->Session()));
		author = document->Payload<SceneDraft>();
		Done(documents->RequestOpen(std::move(document)));
		Done(documents->SaveCurrent());
		sceneBytes = Take(files.Read(Path("prj:Unsaved.ursascene")));
		Take(author->GetScene().CreateGameObject("Unsaved marker"));
		documents->Current()->MarkDirty();
		Rhi::DeviceDesc desc;
#ifndef NDEBUG
		desc.validation = true;
#endif
		device = Take(Rhi::CreateDevice(desc));
		gui = Take(ImGuiBridge::Create(device));
		renderer = Take(Test::SceneRenderer::Create(device));
		preview = std::make_unique<AssetPreviewPanel>(device, *gui);
		views = Take(SceneViewPanel::Create(device, *gui));
	}

	~Harness()
	{
		(void)device->WaitIdle();
		views.reset();
		preview.reset();
		renderer.reset();
		gui.reset();
	}

	Result<void> Render(Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& output,
	                    std::optional<Rendering::CameraType> type, std::optional<Rendering::CameraData> camera)
	{
		if (type == Rendering::CameraType::Preview)
		{
			++previewCalls;
			Expect(&scene != &author->GetScene(), "Preview rendered the authoring Scene");
			Expect(camera && camera->settings.type == Rendering::CameraType::Preview,
			       "Preview lost its pipeline camera type");
			Expect(std::isfinite(camera->position.x) && std::isfinite(camera->position.z),
			       "Framing produced a nonfinite camera");
			previewCamera = camera->identity;
			if (duringRender)
			{
				auto callback = std::move(duringRender);
				duringRender = {};
				callback();
			}
			if (failRender)
			{
				return Result<void>::Failure("Injected pipeline failure");
			}
		}
		Camera captureCamera;
		auto data = scene.CollectPreviewRenderData(captureCamera,
		                                           Float32(output->description.width) / output->description.height);
		if (!data)
		{
			return Result<void>::Failure(data.error);
		}
		if (camera)
		{
			data.value.viewProjection = camera->viewProjection;
			data.value.cameraPosition = camera->position;
		}
		if (type == Rendering::CameraType::Preview)
		{
			vertexCount = 0;
			lastPalette.clear();
			for (const auto& object : data.value.objects)
			{
				vertexCount += object.meshSnapshot->data.positions.size();
				if (!object.jointPalette.empty())
				{
					lastPalette = object.jointPalette;
				}
			}
		}
		auto submitted =
		    renderer->DrawOffscreen(output, data.value.viewProjection, data.value.objects, data.value.lighting,
		                            data.value.cameraPosition, Rhi::ResourceState::ShaderRead);
		if (!submitted)
		{
			return Result<void>::Failure(submitted.error);
		}
		Done(submitted.value->Wait());
		if (type == Rendering::CameraType::Preview)
		{
			lastPixels = Readback(device, output->color, Rhi::ResourceState::ShaderRead);
		}
		return {};
	}

	void Frame(Float32 delta = 1.f / 60.f, bool draw = true)
	{
		Done(gui->BeginFrame(delta, 1100, 850, UInt32(1100 * dpi), UInt32(850 * dpi)));
		Done(preview->BeginFrame());
		Done(preview->Prepare(*workspace, browser, selection, documents->Current(), projectKey));
		if (docked)
		{
			ImGui::Begin("Other Inspector Tab");
			ImGui::TextUnformatted("Inactive preview tab test");
			ImGui::End();
		}
		if (drawProject)
		{
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({640, 410}, ImGuiCond_Always);
			bool projectOpen = true;
			Done(projectPanel.Draw(*workspace, browser, selection, hierarchy, projectOpen, false, diagnostic, {}));
		}
		bool showScene = true, showGame = true;
		ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({640, 410}, ImGuiCond_Always);
		Done(views->Draw(*author, *documents->Current(), hierarchy, play, nullptr, "preview-test", showScene, showGame,
		                 focused, diagnostic));
		ImGui::SetWindowPos("Game", {0, 420});
		ImGui::SetWindowSize("Game", {640, 420});
		if (draw)
		{
			if (!docked)
			{
				ImGui::SetNextWindowPos({650, 0}, ImGuiCond_Always);
				ImGui::SetNextWindowSize({inspectorWidth, inspectorHeight}, ImGuiCond_Always);
			}
			inspector.Draw(*workspace, browser, selection, show, scriptTypes.empty() ? nullptr : &scriptTypes, {}, {},
			               preview->ReservedHeight(),
			               [&](Float32 maximumHeight) { preview->Draw(*workspace, registry, nullptr, maximumHeight); });
		}
		if (extraUi)
		{
			extraUi();
		}
		const EditorRenderViewport render =
		    [&](Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& output,
		        std::optional<Rendering::CameraType> type, std::optional<Rendering::CameraData> camera)
		{ return Render(scene, output, type, camera); };
		Done(views->RenderPending(*author, play, render));
		Done(preview->RenderPending(delta, render));
		Done(gui->EndFrame());
		Expect(Probe::callbacks == 0, "Preview invoked a gameplay lifecycle callback");
	}

	ImGuiWindow* ContentWindow()
	{
		for (auto* window : GImGui->Windows)
		{
			if (window->Active && std::string(window->Name).find("/Content_") != std::string::npos)
			{
				return window;
			}
		}
		throw std::runtime_error("Inspector Preview content child is not active");
	}

	void Activate(const char* label)
	{
		auto* window = ContentWindow();
		ImGui::FocusWindow(window);
		ImGui::ActivateItemByID(window->GetID(label));
		Frame();
	}

	void Choose(const char* combo, const String& label, UInt64 pushId = 0)
	{
		Activate(combo);
		Frame();
		ImGuiWindow* popup = nullptr;
		for (auto* window : GImGui->Windows)
		{
			if (window->Active && std::string(window->Name).find("##Combo_") == 0)
			{
				popup = window;
			}
		}
		Expect(popup != nullptr, "Preview combo did not open");
		ImGui::FocusWindow(popup);
		ImGuiID seed = popup->ID;
		if (std::string(combo) == "##Shape")
		{
			pushId = label == "Cube" ? 1 : label == "Plane" ? 2 : 0;
		}
		if (std::string(combo) == "##Channel")
		{
			pushId = label == "RGB"     ? 1
			         : label == "R"     ? 2
			         : label == "G"     ? 3
			         : label == "B"     ? 4
			         : label == "Alpha" ? 5
			                            : 0;
		}
		if (pushId || std::string(combo) == "##Shape" || std::string(combo) == "##Channel")
		{
			const int value = static_cast<int>(pushId);
			seed = ImHashData(&value, sizeof(value), seed);
		}
		ImGui::ActivateItemByID(ImHashStr(label.c_str(), 0, seed));
		Frame();
		Frame();
		Expect(!ImGui::IsPopupOpen(static_cast<ImGuiID>(0), ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
		       "Preview combo selection left its popup open");
		std::cout << "Selected " << label.c_str() << "; rendered vertices " << vertexCount << "; frames "
		          << preview->RenderCount() << '\n';
	}

	void Select(const VirtualPath& path)
	{
		selection.SelectAsset(path);
		Frame();
		Frame();
		Expect(preview->State().HasImage(), preview->State().Diagnostic().c_str());
		Expect(!documents->PromptPending() && documents->Current()->Dirty(),
		       "Viewing an asset changed the unsaved document");
		Expect(author->GetScene().RootGameObjects().size() == 2, "Preview added objects to the authoring Scene");
	}

	Rhi::FencePtr SubmitUi()
	{
		const UInt32 width = UInt32(1100 * dpi), height = UInt32(850 * dpi);
		canvas = Take(device->CreateTexture({width, height, Rhi::Format::RgbA8Unorm,
		                                     Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource}));
		auto command = Take(device->CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(canvas, Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment));
		Rhi::RenderTarget target;
		target.color = canvas;
		Done(command->BeginRendering(target));
		Done(gui->Record(command, canvas->desc.format, width, height));
		Done(command->EndRendering());
		Done(command->End());
		return Take(device->Submit(command));
	}

	void Capture(const char* name)
	{
		Done(SubmitUi()->Wait());
		Rendering::FrameCapture capture;
		Done(capture.Configure(evidence / name));
		Done(capture.Capture(device, canvas, Rhi::ResourceState::ColorAttachment));
		Done(capture.Finish());
	}

	void Mouse(ImVec2 position, bool down)
	{
		ImGui::GetIO().AddMousePosEvent(position.x, position.y);
		ImGui::GetIO().AddMouseButtonEvent(0, down);
		Frame();
	}

	void TogglePreview(bool currentlyExpanded)
	{
		auto* parent = ImGui::FindWindowByName("Inspector");
		const auto seed = ImHashStr("AssetPreview", 0, parent->ID);
		ImGui::ActivateItemByID(ImHashStr(currentlyExpanded ? "v Preview" : "> Preview", 0, seed));
		Frame();
	}

	void ResizePreview(Float32 deltaY)
	{
		const auto divider = preview->DividerRect();
		const ImVec2 start{divider.x + 50, divider.y + 8};
		Mouse(start, false);
		Mouse(start, true);
		Mouse({start.x, start.y + deltaY}, true);
		Mouse({start.x, start.y + deltaY}, false);
		Frame();
	}

	UInt8 FirstTextureRed()
	{
		const auto pixels = Readback(device, canvas, Rhi::ResourceState::ColorAttachment);
		const auto layout = ComputeViewportLayout(preview->ImageRect(), dpi, {4, 2}, 1, {}, true);
		const auto x = UInt32((layout.displayRect.x + layout.displayRect.width * .125f) * dpi);
		const auto y = UInt32((layout.displayRect.y + layout.displayRect.height * .25f) * dpi);
		return pixels[(Size(y) * canvas->desc.width + x) * 4];
	}

	void PublishAnimation(Float64 duration, bool remove)
	{
		auto* database = workspace->Database();
		const auto metadata = Take(database->Metadata(modelPath));
		const auto status = Take(database->CurrentImport(modelPath));
		Vector<ImportedArtifact> artifacts;
		for (const auto& record : database->ImportedArtifacts())
		{
			if (record.guid != metadata.guid || (remove && record.kind == "AnimationClip"))
			{
				continue;
			}
			auto bytes = Take(files.Read(record.path));
			if (record.isMainObject || record.kind == "AnimationClip")
			{
				auto data = nlohmann::json::from_cbor(bytes.begin(), bytes.end());
				if (record.isMainObject && remove)
				{
					data["animationClipLocalFileIds"] = nlohmann::json::array();
				}
				if (record.kind == "AnimationClip")
				{
					data["durationSeconds"] = duration;
				}
				const auto encoded = nlohmann::json::to_cbor(data);
				bytes.assign(encoded.data(), encoded.data() + encoded.size());
			}
			artifacts.push_back({record.localFileId, record.kind, std::move(bytes), record.outputKey,
			                     record.displayName, record.visibleInProject});
		}
		Done(database->PublishImport(modelPath, metadata.importer, metadata.importerVersion,
		                             "preview-test-" + ToUrsaString(std::to_string(++publishedPreviewVersion)),
		                             status.settings, status.dependencies, artifacts, status.mainObjectLocalFileId));
		preview->Reload();
		Frame();
		Frame();
	}
};

#if defined(URSA_ENABLE_DOTNET)
void ManagedPreviewContracts(Harness& h)
{
	using namespace Ursa::Scripting;
	auto& files = h.workspace->ContentFiles();
	Done(files.Mount("mng", std::filesystem::u8path(URSA_PREVIEW_MANAGED_OUTPUT), MountAccess::ReadOnly));
	const auto jsonBytes = [](const nlohmann::json& json)
	{
		const auto text = json.dump();
		const auto* bytes = reinterpret_cast<const UInt8*>(text.data());
		return Vector<UInt8>(bytes, bytes + text.size());
	};
	const auto fingerprint = [](const Vector<UInt8>& bytes)
	{
		UInt64 hash = 14695981039346656037ull;
		for (auto value : bytes)
		{
			hash ^= value;
			hash *= 1099511628211ull;
		}
		std::ostringstream text;
		text << std::hex << std::setfill('0') << std::setw(16) << hash;
		return text.str();
	};
	nlohmann::json catalog{{"Version", 1u}, {"Scripts", nlohmann::json::array()}};
	UInt64 localId = 0;
	for (const auto& type : Vector<std::pair<String, String>>{{"SessionProbe", "test.session-probe"},
	                                                          {"ReloadDependency", "test.reload-dependency"}})
	{
		catalog["Scripts"].push_back(
		    {{"SourceGuid", "11111111-1111-4111-8111-111111111111"},
		     {"LocalFileId", ++localId},
		     {"Kind", localId == 1 ? "Behaviour" : "ScriptableObject"},
		     {"ScriptType", ToStdString(type.second)},
		     {"Version", 1u},
		     {"Group", "Game"},
		     {"ClrTypeName", ToStdString("Ursa.SessionFixtures." + type.first + ", Ursa.SessionFixtures")}});
	}
	catalog["Scripts"].push_back(
	    {{"SourceGuid", "11111111-1111-4111-8111-111111111111"},
	     {"LocalFileId", 3u},
	     {"Kind", "ScriptableObject"},
	     {"ScriptType", "auto:11111111-1111-4111-8111-111111111111:Ursa.Examples.GeneratedFormatAsset"},
	     {"Version", 1u},
	     {"Group", "Game"},
	     {"ClrTypeName", "Ursa.Examples.GeneratedFormatAsset, Ursa.SessionFixtures"}});
	const auto catalogBytes = jsonBytes(catalog);
	Done(files.WriteAtomic(Path("prj:PreviewCatalog.json"), catalogBytes));
	nlohmann::json content{{"Version", 2u},
	                       {"RequiredBindings", Scripting::Bindings::ScriptBindingRequirements(false)},
	                       {"Group", "Game"},
	                       {"TypeCatalog", "prj:PreviewCatalog.json"},
	                       {"CatalogFingerprint", fingerprint(catalogBytes)},
	                       {"Assemblies", nlohmann::json::array()}};
	for (const char* name : {"Ursa.SessionFixtures", "Ursa.SessionDependency"})
	{
		const auto path = Take(VirtualPath::Parse("mng:TestScripts/" + String(name) + ".dll"));
		content["Assemblies"].push_back({{"Name", name},
		                                 {"Path", ToStdString(path.ToString())},
		                                 {"Fingerprint", fingerprint(Take(files.Read(path)))}});
	}
	Done(files.WriteAtomic(Path("prj:PreviewGame.json"), jsonBytes(content)));
	auto host = Take(DotNetHost::Create());
	ManagedHostFiles framework;
	framework.runtimeConfig = Path("mng:Ursa.ManagedHost.runtimeconfig.json");
	framework.entryAssembly = Path("mng:Ursa.ManagedHost.dll");
	framework.dependencies = {Path("mng:Ursa.ManagedHost.deps.json"), Path("mng:Ursa.Runtime.dll"),
	                          Path("mng:Ursa.Editor.dll"), Path("mng:Ursa.Rendering.dll"),
	                          Path("mng:Ursa.Bindings.dll")};
	Done(host->Connect(files, framework,
	                   [](UInt32 level, const String& message)
	                   {
		                   if (level >= 3)
		                   {
			                   std::cerr << message.c_str() << '\n';
		                   }
	                   }));
	Done(host->LoadEditorApi(Path("mng:Ursa.Editor.dll")));
	const auto session = Take(host->CreateScriptSession(true));
	Done(host->LoadScriptContent(session, Path("prj:PreviewGame.json")));
	Done(host->LoadScriptAssembly(Take(files.Read(Path("mng:TestScripts/Ursa.EditorFixtures.dll")))));
	Done(host->ValidateScriptSession(session));
	const auto descriptors = Take(host->PrepareEditorExtensions(session));
	h.preview->SetExtensionResolver(
	    [&](ManagedEditorContext& context)
	    {
		    const auto found = std::find_if(descriptors.begin(), descriptors.end(),
		                                    [&](const auto& item) { return item.target == context.targetIdentity; });
		    if (found == descriptors.end())
		    {
			    return;
		    }
		    const auto id = found->id;
		    h.preview->ConfigureExtension(
		        id,
		        [&, id](UInt64 instance, ManagedPreviewPhase phase, const ViewportRect& rect,
		                ManagedEditorContext& scope)
		        { return InvokeManagedEditorPreview(*host, session, id, instance, phase, rect, scope); },
		        context);
	    });
	const auto meshPath = Path("prj:CustomPreview.ursamesh");
	const auto mesh = Take(CreateMaterialPreviewMesh(MaterialPreviewShape::Cube));
	Done(SaveMeshResource(h.files, meshPath, ResourceId::Generate(), *mesh));
	const auto sourcePath = Path("prj:CustomPreview.ursaasset");
	Done(h.files.WriteAtomic(sourcePath, {}));
	Done(h.workspace->Refresh());
	ScriptTypeDescriptor type;
	type.scriptType = "test.reload-dependency";
	type.sourceGuid = Take(ResourceId::Parse("11111111-1111-4111-8111-111111111111"));
	type.localFileId = 2;
	type.kind = ScriptTypeKind::ScriptableObject;
	type.fields.push_back({"value", "SignedInteger", "Unsupported", "System.Int32", "7"});
	type.fields.push_back(
	    {"self", "ScriptableObjectReference", "Unsupported", "Ursa.SessionFixtures.ReloadDependency", "null"});
	h.scriptTypes.push_back(type);
	const auto guid = Take(h.workspace->Database()->Metadata(sourcePath)).guid;
	Done(SaveScriptableAssetSource(h.files, sourcePath, Take(CreateScriptableAssetWithDefaults(guid, type))));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	const auto authorCount = h.author->GetScene().RootGameObjects().size();
	const auto authorRevision = h.documents->Current()->Revision();
	h.Select(meshPath);
	Expect(h.preview->State().Target().custom && h.previewCalls > 0 &&
	           h.vertexCount == mesh->Snapshot()->data.positions.size(),
	       "Managed Mesh Preview did not render its private scene through the Preview camera");
	const auto beforeDrag = h.lastPixels;
	const auto rect = h.preview->ImageRect();
	h.Mouse({rect.x + rect.width * .4f, rect.y + rect.height * .5f}, false);
	h.Mouse({rect.x + rect.width * .4f, rect.y + rect.height * .5f}, true);
	h.Mouse({rect.x + rect.width * .7f, rect.y + rect.height * .5f}, true);
	h.Mouse({rect.x + rect.width * .7f, rect.y + rect.height * .5f}, false);
	Expect(Difference(beforeDrag, h.lastPixels) > 100,
	       "Actual Preview mouse drag did not change the managed camera image");
	const auto beforeLight = h.lastPixels;
	h.Activate("Toggle preview light");
	Expect(Difference(beforeLight, h.lastPixels) > 100,
	       "Managed private Preview light did not affect the PBR material image");
	h.Activate("Toggle preview light");
	for (const auto dpi : {1.f, 1.5f, 2.f})
	{
		h.dpi = dpi;
		h.inspectorWidth = 240;
		h.Frame();
		h.Frame();
		h.Capture(("12-Custom-Mesh-DPI-" + std::to_string(int(dpi * 100)) + ".png").c_str());
	}
	h.Activate("Fail next Preview");
	std::cout << "Managed Preview injected failure returned\n" << std::flush;
	Expect(h.preview->State().HasImage() && !h.preview->State().Diagnostic().empty(),
	       "Same-target managed failure discarded its last successful image or used default Preview");
	h.preview->Reload();
	std::cout << "Managed Preview reload requested\n" << std::flush;
	h.Frame();
	h.Frame();
	std::cout << "Managed Preview reload rendered\n" << std::flush;
	Expect(h.preview->State().HasImage() && h.preview->State().Diagnostic().empty(),
	       "Managed Preview did not recover with a new instance on reload");
	const auto renders = h.preview->RenderCount();
	const auto viewingTime = h.preview->ViewingTime();
	h.Frame(10, false);
	Expect(h.preview->RenderCount() == renders && h.preview->ViewingTime() == viewingTime,
	       "Hidden managed Preview continued rendering or advancing time");
	h.Frame();
	Expect(h.preview->ViewingTime() == viewingTime, "Managed Preview caught up hidden wall-clock time");
	h.TogglePreview(true);
	const auto collapsedRenders = h.preview->RenderCount();
	const auto collapsedTime = h.preview->ViewingTime();
	h.Frame(5);
	Expect(h.preview->RenderCount() == collapsedRenders && h.preview->ViewingTime() == collapsedTime,
	       "Collapsed managed Preview continued updating");
	h.TogglePreview(false);
	const auto fullHeight = h.inspectorHeight;
	h.inspectorHeight = 70;
	h.Frame();
	const auto clippedRenders = h.preview->RenderCount();
	const auto clippedTime = h.preview->ViewingTime();
	h.Frame(5);
	Expect(h.preview->RenderCount() == clippedRenders && h.preview->ViewingTime() == clippedTime,
	       "A managed Preview without a usable image region continued rendering or advancing time");
	h.inspectorHeight = fullHeight;
	h.Frame();
	Expect(h.preview->ViewingTime() == clippedTime, "Managed Preview caught up time after its image region returned");
	h.Select(sourcePath);
	std::cout << "Managed ScriptableObject Preview selected\n" << std::flush;
	Expect(h.preview->State().Target().custom, "ScriptableObject did not select its registered Preview");
	for (const auto dpi : {1.f, 1.5f, 2.f})
	{
		h.dpi = dpi;
		h.Frame();
		h.Frame();
		h.Capture(("13-Custom-Image-DPI-" + std::to_string(int(dpi * 100)) + ".png").c_str());
		const auto canvasPixels = Readback(h.device, h.canvas, Rhi::ResourceState::ColorAttachment);
		const auto area = h.preview->ImageRect();
		const auto sample = [&](Float32 x, Float32 y, Size channel)
		{
			const auto column = UInt32((area.x + area.width * x) * dpi);
			const auto row = UInt32((area.y + area.height * y) * dpi);
			return canvasPixels[(Size(row) * h.canvas->desc.width + column) * 4 + channel];
		};
		Expect(sample(.25f, .25f, 0) > 125 && sample(.25f, .25f, 0) < 150 && sample(.25f, .75f, 0) >= 20 &&
		           sample(.25f, .75f, 0) <= 24,
		       "Actual managed GUI texture lost semi-transparent/transparent pixels or DPI placement");
	}
	h.Select(h.materialPath);
	Expect(!h.preview->State().Target().custom,
	       "An Inspector without HasPreviewGUI suppressed built-in Material fallback");
	const auto generatedPath = Path("prj:Generated.previewfixture");
	Done(h.files.WriteAtomic(generatedPath, {1}));
	Done(h.workspace->Refresh());
	const auto generatedGuid = Take(h.workspace->Database()->Metadata(generatedPath)).guid;
	const auto dataId = Take(StableImportObjectId("test.Preview", "data"));
	const auto importedMeshId = Take(StableImportObjectId("test.Preview", "mesh"));
	auto generated = Take(CreateScriptableAssetWithDefaults(generatedGuid, type));
	Done(generated.SetDescription(generatedGuid, dataId, type.scriptType, 1, generated.Payload()));
	Vector<ImportedArtifact> outputs{
	    {dataId, "ScriptableObject", Take(EncodeScriptableAssetArtifact(generated)), "data"},
	    {importedMeshId, "Mesh", Take(EncodeMeshResource(generatedGuid, *mesh, importedMeshId)), "mesh"}};
	Done(h.workspace->Database()->PublishImport(generatedPath, "test.Preview", 1, "preview-fixture",
	                                            nlohmann::json::object(), {}, outputs, dataId));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	h.Select(generatedPath);
	Expect(h.preview->State().Target().custom && !h.preview->State().Target().identity.localFileId,
	       "Imported main-object alias did not dispatch its ScriptableObject Preview");
	h.Choose("##Subasset", "mesh (Mesh)");
	Expect(h.preview->State().Target().custom && h.preview->State().Target().identity.localFileId == importedMeshId &&
	           h.vertexCount == mesh->Snapshot()->data.positions.size(),
	       "Imported Mesh sub-asset did not receive its typed custom Preview");
	h.Capture("14-Custom-Imported-Subasset.png");
	// Select the published object through the actual Project controls, independently of the footer picker.
	h.selection.SelectAsset(generatedPath);
	h.drawProject = true;
	h.Frame();
	h.Frame();
	ImGuiWindow* projectContent = nullptr;
	for (auto* window : GImGui->Windows)
	{
		if (window->Active && std::string(window->Name).find("Project/") == 0 &&
		    std::string(window->Name).find("ProjectFolderContents") != std::string::npos &&
		    std::string(window->Name).find("ProjectBreadcrumbs") == std::string::npos)
		{
			projectContent = window;
		}
	}
	Expect(projectContent != nullptr, "Project asset list child is unavailable");
	const auto assetSeed = ImHashStr(generatedPath.relative.c_str(), 0, projectContent->ID);
	ImGui::FocusWindow(projectContent);
	ImGui::ActivateItemByID(ImHashStr("##ImportedObjects", 0, assetSeed));
	h.Frame();
	h.Frame();
	const String projectObjectId = ToUrsaString(std::to_string(importedMeshId));
	ImGui::FocusWindow(projectContent);
	ImGui::ActivateItemByID(ImHashStr("mesh (Mesh)", 0, ImHashStr(projectObjectId.c_str(), 0, assetSeed)));
	h.Frame();
	h.Frame();
	Expect(h.selection.assetLocalFileId == importedMeshId &&
	           h.preview->State().Target().identity.localFileId == importedMeshId,
	       "Project sub-asset selection did not reach the resource Inspector and Preview");
	const auto resourceInspector =
	    Take(ImportedAssetInspector::Open(*h.workspace, {generatedGuid, importedMeshId}, "Mesh"));
	auto resourceContext = resourceInspector->Context();
	Expect(!resourceContext.canEditTarget && resourceContext.targetMesh && !resourceContext.document &&
	           !h.inspector.SelectedDocument(h.selection) && !Take(h.inspector.SaveSelectedSource(h.selection)),
	       "Published sub-asset Inspector exposed an author document or source save command");
	h.Capture("14b-Project-Subasset-Inspector.png");
	h.drawProject = false;
	const auto stableRegistrations = h.gui->RegisteredTextureCount();
	for (UInt32 iteration = 0; iteration < 8; ++iteration)
	{
		h.Select(sourcePath);
		h.Select(meshPath);
	}
	Expect(h.gui->RegisteredTextureCount() <= stableRegistrations + 2,
	       "Repeated custom 2D/3D target changes leaked UI image registrations");
	const auto originalProjectSession = h.preview->State().Target().projectSession;
	{
		auto alternate = Take(ProjectWorkspace::FromFilesystem({}, h.files));
		Done(h.preview->Prepare(*alternate, h.browser, h.selection, nullptr, "managed-other-project"));
		Expect(!h.preview->State().HasImage() && h.preview->State().Target().projectSession != originalProjectSession,
		       "Switching projects retained a custom image for the same asset GUID");
		Done(h.preview->CloseExtension());
	}
	h.Frame();
	h.Frame();
	const auto rootsBeforeRelease = h.gui->RegisteredTextureCount();
	h.duringRender = [&]
	{
		h.selection.Clear();
		Done(h.preview->Prepare(*h.workspace, h.browser, h.selection, h.documents->Current(), h.projectKey));
	};
	h.Frame();
	Expect(!h.preview->State().HasImage(),
	       "A custom render completion published after selection changed during submission");
	h.Frame();
	Expect(h.gui->RegisteredTextureCount() < rootsBeforeRelease, "Custom Preview close retained registered GPU images");
	auto bad = Take(CreateScriptableAssetWithDefaults(guid, type));
	auto badPayload = bad.Payload();
	badPayload["Fields"]["value"] = -1;
	Done(bad.SetDescription(guid, 2, type.scriptType, 1, badPayload));
	Done(SaveScriptableAssetSource(h.files, sourcePath, bad));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	h.Select(meshPath);
	h.selection.SelectAsset(sourcePath);
	h.Frame();
	h.Frame();
	Expect(!h.preview->State().HasImage() && h.preview->State().Diagnostic().find(guid.ToString()) != String::npos,
	       "A new failing custom target displayed the previous asset or omitted its target identity");
	Expect(h.author->GetScene().RootGameObjects().size() == authorCount &&
	           h.documents->Current()->Revision() == authorRevision && h.documents->Current()->Dirty() &&
	           Probe::callbacks == 0,
	       "Managed Preview changed author objects, Dirty or invoked gameplay callbacks");
	auto submitted = h.SubmitUi();
	h.selection.Clear();
	h.Frame();
	h.Frame();
	Done(submitted->Wait());
	h.preview->SetExtensionResolver({});
	Done(host->CloseScriptSession(session));
	Done(host->CloseEndpoint());
	std::cout << "Managed Preview GPU images, mouse, DPI, narrow Inspector, hidden, reload and submitted-frame cleanup "
	             "passed\n";
}
#endif

void ContentContracts(Harness& h)
{
	auto document = Take(OpenMaterialDocument(h.materialPath, h.documents->Session(), *h.workspace));
	auto material = document->Payload<MaterialDraft>();
	Done(material->Material().SetBaseColor({.05f, .1f, .9f, 1}));
	document->MarkDirty();
	EditorSelectionContext selected;
	selected.SelectAsset(h.materialPath);
	auto target = ResolveAssetPreviewTarget(selected, h.browser.Find(h.materialPath), document.get(), 1);
	auto content = Take(AssetPreviewContent::Load(target, *h.workspace, h.registry));
	Expect(content->Material() == material->MaterialPtr() && content->Material()->Parameters().baseColor.blue > .8f,
	       "Preview lost a valid unsaved material value");
	Expect(Take(h.files.Read(h.materialPath)) == h.materialBytes && document->Dirty() && !h.documents->PromptPending(),
	       "Reading a draft saved it or asked to replace the active document");
	auto materialPreview = Take(MaterialPreview::Create(h.device, *h.gui, 160));
	Done(materialPreview->SetShape(MaterialPreviewShape::Sphere));
	Camera camera;
	camera.position = {0, 0, 3};
	camera.target = {};
	const EditorRenderViewport render =
	    [&](Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& output,
	        std::optional<Rendering::CameraType> type, std::optional<Rendering::CameraData> selectedCamera)
	{ return h.Render(scene, output, type, selectedCamera); };
	Done(materialPreview->Render(material->MaterialPtr(), camera, render));
	const auto blue = h.lastPixels;
	Done(material->Material().SetBaseColor({1, 1, 1, 1}));
	Done(materialPreview->Render(material->MaterialPtr(), camera, render));
	const auto white = h.lastPixels;
	Expect(Difference(blue, white) > 100, "Unsaved material parameter did not change GPU pixels");
	Done(material->SetTextureId(MaterialTextureSlot::BaseColor,
	                            Take(h.workspace->Database()->Metadata(h.texturePath)).guid, h.documents->Session()));
	Done(materialPreview->Render(material->MaterialPtr(), camera, render));
	Expect(Difference(white, h.lastPixels) > 100, "Material texture assignment did not change GPU pixels");
	const auto draftTexturePath = Path("prj:Draft.ursatex");
	const auto draftTextureId = ResourceId::Generate();
	const auto savedTexture = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Linear, {255, 255, 255, 255}));
	Done(SaveTextureResource(h.files, draftTexturePath, draftTextureId, *savedTexture));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	auto textureDocument =
	    Take(EditorDocument::Create(ResourceKind::Texture, draftTextureId, draftTexturePath,
	                                [](VirtualFileSystem&, const VirtualPath&, ResourceId) { return Result<void>{}; }));
	auto pixels = Take(TextureDraft::Create(1, 1, TextureColorSpace::Linear, {17, 33, 65, 129}));
	textureDocument->SetPayload(pixels);
	textureDocument->MarkDirty();
	selected.SelectAsset(draftTexturePath);
	target = ResolveAssetPreviewTarget(selected, h.browser.Find(draftTexturePath), textureDocument.get(), 1);
	content = Take(AssetPreviewContent::Load(target, *h.workspace, h.registry));
	Expect(content->Texture()->GetWidth() == 1 && content->Texture()->GetPixels()[0] == 17 &&
	           pixels->GetPixels()[3] == 129,
	       "Texture draft was replaced with saved pixels or mutated");
	const auto package = Take(
	    PakBuilder::Encode({{Path("prj:Packaged.ursatex"), Take(h.files.Read(draftTexturePath))},
	                        {Path("prj:Packaged.ursatex.meta"), Take(h.files.Read(Path("prj:Draft.ursatex.meta")))}}));
	VirtualFileSystem storage;
	Done(storage.Mount("out", h.evidence, MountAccess::ReadWrite));
	Done(storage.WriteAtomic(Path("out:TextureNoSource.pak"), package));
	auto packaged = Take(OpenEngineFileSystem());
	Done(Take(PakArchive::Open(h.evidence / "TextureNoSource.pak"))->Mount(packaged));
	auto readOnly = Take(ProjectWorkspace::FromFilesystem({}, packaged));
	AssetPreviewTarget readOnlyTarget;
	readOnlyTarget.kind = AssetPreviewKind::Texture;
	readOnlyTarget.identity = {draftTextureId, 0};
	readOnlyTarget.path = Path("prj:Packaged.ursatex");
	auto packedTexture = Take(AssetPreviewContent::Load(readOnlyTarget, *readOnly, h.registry));
	Expect(packedTexture->Texture() && !packedTexture->SourceTexture() && packedTexture->Texture()->GetMipLevels() == 1,
	       "Read-only texture without source did not retain imported pixels");
	auto prefab = Take(OpenSceneDocument(h.prefabPath, ResourceKind::Prefab, h.registry, h.documents->Session()));
	auto draft = prefab->Payload<SceneDraft>();
	const auto sourceRoot = draft->GetPrefabAsset()->Root();
	LocalTransform pose;
	pose.position = {3, 0, 0};
	Done(sourceRoot->SetLocalTransform(pose));
	prefab->MarkDirty();
	const auto before = Take(h.files.Read(h.prefabPath));
	selected.SelectAsset(h.prefabPath);
	target = ResolveAssetPreviewTarget(selected, h.browser.Find(h.prefabPath), prefab.get(), 1);
	content = Take(AssetPreviewContent::Load(target, *h.workspace, h.registry));
	auto another = Take(AssetPreviewContent::Load(target, *h.workspace, h.registry));
	Expect(content->RenderScene() != another->RenderScene() && content->RenderScene() != &draft->GetScene(),
	       "Prefab copies share a Scene");
	Expect(Take(content->GeometryBounds()).minimum.x > 2 && Take(sourceRoot->GetLocalTransform()).position.x == 3,
	       "Prefab preview lost its unsaved transform or modified the authoring transform");
	for (const auto& object : content->RenderScene()->RootGameObjects())
	{
		if (auto* script = object->FindComponent<Scripting::ScriptBehaviourComponent>())
		{
			Expect(!script->InstanceToken(), "Preview instantiated managed script behaviour");
		}
	}
	Expect(Take(h.files.Read(h.prefabPath)) == before && Probe::callbacks == 0,
	       "Prefab preview saved or updated the source");
	const auto mesh = Take(CreateMaterialPreviewMesh(MaterialPreviewShape::Cube));
	Done(SaveMeshResource(h.files, Path("prj:Standalone.ursamesh"), ResourceId::Generate(), *mesh));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	h.Select(Path("prj:Standalone.ursamesh"));
	Expect(h.preview->Content()->MeshAsset()->Snapshot()->data.positions.size() == 24,
	       "Standalone mesh did not use its own geometry");
	auto empty = Take(SceneDraft::Create(ResourceKind::Prefab, h.registry, h.documents->Session()));
	Done(empty->Save(h.files, Path("prj:Empty.ursaprefab"), ResourceId::Generate()));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	h.selection.SelectAsset(Path("prj:Empty.ursaprefab"));
	h.Frame();
	h.Frame();
	Expect(!h.preview->State().HasImage() && h.preview->State().Diagnostic().find("geometry") != String::npos,
	       "Empty geometry did not produce a clear empty state");
	Vector<UInt8> largePixels(1024 * 512 * 4);
	for (Size i = 0; i < largePixels.size(); i += 4)
	{
		largePixels[i] = UInt8((i / 4) % 251);
		largePixels[i + 1] = 80;
		largePixels[i + 2] = 160;
		largePixels[i + 3] = 255;
	}
	const auto large = Take(MaterialTexture::Create(1024, 512, TextureColorSpace::Srgb, std::move(largePixels)));
	Done(SaveTextureResource(h.files, Path("prj:Large.ursatex"), ResourceId::Generate(), *large));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	h.Select(Path("prj:Large.ursatex"));
	h.Activate("100%");
	h.Capture("10-Texture-Pan-Before.png");
	const auto beforePan = Readback(h.device, h.canvas, Rhi::ResourceState::ColorAttachment);
	const auto image = h.preview->ImageRect();
	const ImVec2 middle{image.x + image.width * .5f, image.y + image.height * .5f};
	h.Mouse(middle, false);
	h.Mouse(middle, true);
	h.Mouse({middle.x + 70, middle.y + 20}, true);
	h.Mouse({middle.x + 70, middle.y + 20}, false);
	h.Capture("11-Texture-Pan-After.png");
	Expect(Difference(beforePan, Readback(h.device, h.canvas, Rhi::ResourceState::ColorAttachment)) > 1000,
	       "100% texture pan did not change clipped pixels");
}

void ImportedUiContracts(Harness& h)
{
	const auto guid = Take(h.workspace->Database()->Metadata(h.modelPath)).guid;
	const auto directory = h.browser.Find(h.modelPath)->VisibleChildren();
	Expect(!directory.empty() && std::none_of(directory.begin(), directory.end(), [](const auto& object)
	                                          { return object.kind == "ModelNode" || object.isMainObject; }),
	       "Project displayed an internal node or duplicate main object");
	const auto mesh = Take(h.workspace->Database()->ImportedObject(guid, h.meshId));
	h.Select(h.modelPath);
	h.drawProject = true;
	h.Frame();
	h.Frame();
	ImGuiWindow* list = nullptr;
	for (auto* window : GImGui->Windows)
	{
		if (window->Active && std::string(window->Name).find("Project/") == 0 &&
		    std::string(window->Name).find("ProjectFolderContents") != std::string::npos &&
		    std::string(window->Name).find("ProjectBreadcrumbs") == std::string::npos)
		{
			list = window;
		}
	}
	Expect(list, "Actual Project contents window is missing");
	const auto seed = ImHashStr(h.modelPath.relative.c_str(), 0, list->ID);
	ImGui::FocusWindow(list);
	ImGui::ActivateItemByID(ImHashStr("##ImportedObjects", 0, seed));
	h.Frame();
	h.Frame();
	Expect(!h.selection.assetLocalFileId, "Expanding Project changed the selected object");
	const auto childId =
	    ImHashStr(mesh.Label().c_str(), 0, ImHashStr(ToUrsaString(std::to_string(h.meshId)).c_str(), 0, seed));
	const auto roots = h.author->GetScene().RootGameObjects();
	Done(h.hierarchy.Select(h.author->GetScene(), roots.back()));
	const auto oldHierarchy = h.hierarchy.SelectedId();
	h.selection.beforeChange = [](const auto&) { return false; };
	ImGui::FocusWindow(list);
	ImGui::ActivateItemByID(childId);
	h.Frame();
	h.Frame();
	Expect(!h.selection.assetLocalFileId && h.hierarchy.SelectedId() == oldHierarchy &&
	           h.preview->State().Target().identity.localFileId == 0,
	       "Cancelled Project child selection changed hierarchy, highlight or preview");
	h.selection.beforeChange = {};
	ImGui::FocusWindow(list);
	ImGui::ActivateItemByID(childId);
	h.Frame();
	h.Frame();
	Expect(h.selection.assetLocalFileId == h.meshId && h.preview->State().Target().identity.localFileId == h.meshId,
	       "Project object click did not select the exact preview target");
	h.Capture("15-Named-Project-Mesh.png");
	ImGui::FocusWindow(list);
	ImGui::ActivateItemByID(ImHashStr(h.modelPath.FileName().c_str(), 0, seed));
	h.Frame();
	h.Frame();
	Expect(!h.selection.assetLocalFileId && h.selection.assetRole == EditorAssetSelectionRole::Source &&
	           !h.preview->State().Target().identity.localFileId,
	       "Project source row did not restore the source selection role");
	ImGui::FocusWindow(list);
	ImGui::ActivateItemByID(childId);
	h.Frame();
	h.Frame();
	h.drawProject = false;
	const auto choices = AssetObjectChoices(*h.workspace);
	Expect(
	    std::count_if(choices.begin(), choices.end(),
	                  [&](const auto& object) { return object.guid == guid && object.isMainObject; }) == 1 &&
	        std::none_of(choices.begin(), choices.end(), [](const auto& object) { return object.kind == "ModelNode"; }),
	    "Resource chooser duplicated the main alias or exposed hierarchy nodes");
	ManagedEditorContext context;
	context.workspace = h.workspace.get();
	Bindings::EditorInvocation invocation(context, 1);
	const auto managedChoices = Bindings::EditorServices::AssetChoices(invocation);
	const auto managedMesh =
	    std::find_if(managedChoices.begin(), managedChoices.end(), [&](const auto& choice)
	                 { return choice.reference.asset.guid == guid && choice.reference.asset.localFileId == h.meshId; });
	Expect(managedMesh != managedChoices.end() && managedMesh->isSubAsset &&
	           managedMesh->name == mesh.Label() + " - " + h.modelPath.ToString(),
	       "C# AssetChoices disagreed with the Project descriptor or exact child identity");
	Scene dropScene;
	const auto receiver = Take(dropScene.CreateGameObject("Drop receiver"));
	auto* filter = Take(receiver->AddComponent<MeshFilterComponent>());
	auto resources = Take(ResourceSession::Open(h.workspace->ContentFiles(), h.workspace->AssetResolver()));
	using OptionalField = Bindings::EditorServices::OptionalReference;
	using FieldResult = Ursa::Scripting::Bindings::CallbackResult<OptionalField>;
	std::unique_ptr<FieldResult> managedField;
	ImVec2 meshDrop, managedDrop, materialDrop;
	auto* materialReceiver = Take(receiver->AddComponent<MeshRendererComponent>());
	Done(materialReceiver->SetSharedMaterials({nullptr}));
	h.drawProject = true;
	h.extraUi = [&]
	{
		ImGui::SetNextWindowPos({650, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({440, 260}, ImGuiCond_Always);
		ImGui::Begin("Native Mesh Drop");
		meshDrop = ImGui::GetCursorScreenPos();
		meshDrop.x += 80;
		meshDrop.y += ImGui::GetFrameHeight() * .5f;
		Take(DrawMeshInspector(*filter, *h.workspace, *resources));
		managedDrop = ImGui::GetCursorScreenPos();
		managedDrop.x += 80;
		managedDrop.y += ImGui::GetFrameHeight() * .5f;
		const String fieldLabel = "Script Mesh";
		const Ursa::Scripting::Bindings::Utf8View label{reinterpret_cast<const UInt8*>(fieldLabel.data()),
		                                                fieldLabel.size(), 1, 0};
		OptionalField empty{};
		FieldResult next;
		next.status = Ursa::Scripting::Bindings::Editor::NativeEditorBindings_AssetField(
		    invocation.Token(), &label, managedField ? &managedField->value : &empty,
		    Ursa::Scripting::Bindings::Wire::Enum_Ursa_Editor_EditorReferenceKind::V_Mesh, &next.value, &next.outcome);
		Expect(bool(next), next.Error().c_str());
		managedField = std::make_unique<FieldResult>(std::move(next));

		ImGui::End();
		ImGui::SetNextWindowPos({650, 280}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({440, 280}, ImGuiCond_Always);
		ImGui::Begin("Native Material Drop");
		materialDrop = ImGui::GetCursorScreenPos();
		materialDrop.x += 80;
		materialDrop.y += ImGui::GetFrameHeightWithSpacing() * 3 + ImGui::GetFrameHeight() * .5f;
		Take(DrawMeshInspector(*materialReceiver, *h.workspace, *resources));
		ImGui::End();
	};
	h.Frame();
	h.Frame();
	const auto itemCenter = [&](ImGuiWindow* window, ImGuiID id)
	{
		ImGui::FocusWindow(window);
		GImGui->NavWindow = window;
		GImGui->NavId = id;
		GImGui->NavLayer = ImGuiNavLayer_Main;
		GImGui->NavInitRequest = false;
		GImGui->NavInitResult.Clear();
		h.Frame();
		const auto rect = window->NavRectRel[ImGuiNavLayer_Main];
		const ImRect absolute{{rect.Min.x + window->Pos.x, rect.Min.y + window->Pos.y},
		                      {rect.Max.x + window->Pos.x, rect.Max.y + window->Pos.y}};
		ImGui::ScrollToRect(window, absolute, ImGuiScrollFlags_KeepVisibleEdgeY);
		h.Frame();
		const auto center = window->NavRectRel[ImGuiNavLayer_Main].GetCenter();
		return ImVec2{window->Pos.x + center.x, window->Pos.y + center.y};
	};
	const auto drag = [&](ImGuiID sourceId, const char* targetWindow = "Native Mesh Drop",
	                      const char* targetField = "Mesh", bool material = false)
	{
		const auto source = itemCenter(list, sourceId);
		auto* target = ImGui::FindWindowByName(targetWindow);
		ImGui::FocusWindow(target);
		h.Frame();
		const auto destination = material                               ? materialDrop
		                         : String(targetField) == "Script Mesh" ? managedDrop
		                                                                : meshDrop;
		ImGui::FocusWindow(list);
		h.Mouse(source, false);
		h.Mouse(source, true);
		h.Mouse({source.x + 12, source.y}, true);
		const auto* payload = ImGui::GetDragDropPayload();
		Expect(payload && payload->IsDataType("URSA_ASSET_REFERENCE"),
		       "Actual Project drag did not create an asset payload");
		const auto sent = *static_cast<const AssetReference*>(payload->Data);
		std::cout << "Project drag " << sent.guid.ToString().c_str() << ':' << sent.localFileId << " from " << source.x
		          << ',' << source.y << " to " << destination.x << ',' << destination.y << " field " << targetField
		          << '\n'
		          << std::flush;
		h.Mouse(destination, true);
		h.Mouse(destination, true);
		h.Capture("15b-Project-Drag.png");
		h.Mouse(destination, false);
		h.Frame();
	};
	drag(childId);
	Expect(filter->SharedMesh() && filter->SharedMesh()->AssetIdentity().guid == guid &&
	           filter->SharedMesh()->AssetIdentity().localFileId == h.meshId,
	       "Project Mesh drag did not assign the exact generated object");
	const auto assignedMesh = filter->SharedMesh();
	const auto textureSeed = ImHashStr(h.texturePath.relative.c_str(), 0, list->ID);
	drag(ImHashStr(h.texturePath.FileName().c_str(), 0, textureSeed));
	Expect(filter->SharedMesh() == assignedMesh, "Dropping a Texture replaced a Mesh field");
	drag(childId, "Native Mesh Drop", "Script Mesh");
	Expect(managedField->value.present && managedField->value.F_value.F_Asset.F_value.F_LocalFileId == h.meshId,
	       "Project Mesh drag did not assign the precise C# AssetField reference");
	drag(ImHashStr(h.texturePath.FileName().c_str(), 0, textureSeed), "Native Mesh Drop", "Script Mesh");
	Expect(managedField->value.F_value.F_Asset.F_value.F_LocalFileId == h.meshId,
	       "C# AssetField accepted a Texture for a Mesh field");
	const auto materialObject =
	    std::find_if(directory.begin(), directory.end(), [](const auto& object) { return object.kind == "Material"; });
	Expect(materialObject != directory.end(), "Model fixture has no material output");
	const auto materialRowId =
	    ImHashStr(materialObject->Label().c_str(), 0,
	              ImHashStr(ToUrsaString(std::to_string(materialObject->localFileId)).c_str(), 0, seed));
	drag(materialRowId, "Native Material Drop", "Element", true);
	const auto assignedMaterial = materialReceiver->SharedMaterials().front();
	Expect(assignedMaterial && assignedMaterial->AssetIdentity().guid == guid &&
	           assignedMaterial->AssetIdentity().localFileId == materialObject->localFileId,
	       "Project Material drag did not assign its precise output reference");
	drag(childId, "Native Material Drop", "Element", true);
	Expect(materialReceiver->SharedMaterials().front() == assignedMaterial, "A Mesh drop replaced a Material field");
	const auto exactField = Bindings::EditorServices::ReadReference(invocation, managedField->value.F_value);
	const nlohmann::json scriptPayload{
	    {"FormatVersion", 1},
	    {"ScriptType", "test.precise-fields"},
	    {"Version", 1},
	    {"Fields",
	     {{"mesh",
	       {{"$ursaReference",
	         {{"Kind", "Mesh"},
	          {"Id", ""},
	          {"Resource", {{"kind", "asset"}, {"asset", AssetReferenceToJson(exactField.asset)}}}}}}}}}};
	Take(receiver->AddComponent<Scripting::ScriptBehaviourComponent>("test.precise-fields"))
	    ->SetSerializedPayload(ToUrsaString(scriptPayload.dump()));
	const auto checkRoundTrip = [&](const Scene& scene)
	{
		const auto root = scene.RootGameObjects().front();
		const auto meshIdentity = root->FindComponent<MeshFilterComponent>()->SharedMesh()->AssetIdentity();
		const auto materialIdentity =
		    root->FindComponent<MeshRendererComponent>()->SharedMaterials().front()->AssetIdentity();
		const auto savedFields = nlohmann::json::parse(
		    ToStdString(root->FindComponent<Scripting::ScriptBehaviourComponent>()->SerializedPayload()));
		Expect(meshIdentity.guid == guid && meshIdentity.localFileId == h.meshId &&
		           materialIdentity.localFileId == materialObject->localFileId &&
		           savedFields.at("Fields") == scriptPayload.at("Fields"),
		       "Scene/Prefab save and reopen changed a native or C# field's exact imported reference");
	};
	Done(SaveSceneResource(h.files, Path("prj:FieldRoundTrip.ursascene"), ResourceId::Generate(), dropScene,
	                       h.registry));
	ComponentLoadContext sceneContext(*resources);
	const auto reopenedScene =
	    Take(LoadSceneResource(h.files, Path("prj:FieldRoundTrip.ursascene"), h.registry, sceneContext));
	checkRoundTrip(*reopenedScene.scene);
	Done(SavePrefabResource(h.files, Path("prj:FieldRoundTrip.ursaprefab"), ResourceId::Generate(), dropScene, receiver,
	                        h.registry));
	ComponentLoadContext prefabContext(*resources);
	const auto reopenedPrefab =
	    Take(LoadPrefabResource(h.files, Path("prj:FieldRoundTrip.ursaprefab"), h.registry, prefabContext));
	checkRoundTrip(*reopenedPrefab.scene);
	h.extraUi = {};
	h.drawProject = false;
	for (const auto& object : directory)
	{
		if (object.kind != "Skeleton" && object.kind != "AnimationClip")
		{
			continue;
		}
		const auto inspector =
		    Take(ImportedAssetInspector::Open(*h.workspace, {guid, object.localFileId}, object.kind));
		auto readonly = inspector->Context();
		Expect(!readonly.canEditTarget && readonly.assetSelection->localFileId == object.localFileId &&
		           !readonly.document,
		       "Skeleton or clip Inspector lost its exact read-only target");
		Bindings::EditorInvocation metadata(readonly, 1);
		const auto fields = metadata.properties->Snapshot().at("Fields");
		if (object.kind == "Skeleton")
		{
			const auto skeleton = Take(h.documents->Session().LoadSkeleton(guid, object.localFileId));
			Expect(fields.at("jointCount") == skeleton->joints.size(),
			       "Skeleton Inspector joint count differs from its loaded resource");
		}
		else
		{
			const auto clip = Take(h.documents->Session().LoadAnimationClip(guid, object.localFileId));
			Expect(fields.at("channelCount") == clip->channels.size() &&
			           fields.at("durationSeconds") == clip->durationSeconds,
			       "Animation Inspector metadata differs from its loaded clip");
		}
		h.selection.SelectAsset(h.modelPath, object.localFileId, object.kind, guid);
		h.Frame();
		h.Frame();
		if (object.kind == "Skeleton")
		{
			Expect(!h.preview->State().HasImage() && !h.preview->Content(),
			       "Selecting a Skeleton fell back to a model preview");
			const auto skeleton = Take(h.documents->Session().LoadSkeleton(guid, object.localFileId));
			for (auto* window : GImGui->Windows)
			{
				if (!window->Active || std::string(window->Name).find("Inspector/AssetProperties") != 0)
				{
					continue;
				}
				const int joint = 0;
				ImGui::FocusWindow(window);
				ImGui::ActivateItemByID(
				    ImHashStr(skeleton->joints.front().name.c_str(), 0, ImHashData(&joint, sizeof(joint), window->ID)));
				break;
			}
			h.Frame();
			h.Frame();
		}
		else
		{
			Expect(h.preview->State().Target().kind == AssetPreviewKind::Animation &&
			           h.preview->Content()->SelectedClip() == object.localFileId &&
			           h.selection.assetLocalFileId == object.localFileId,
			       "Clip selection did not use its exact identity with the main model rendering context");
		}
		h.Capture(object.kind == "Skeleton" ? "16-Skeleton-Inspector.png" : "17-Clip-Inspector-Preview.png");
	}
	h.Select(h.modelPath);
	auto model = Take(ModelImportView::Open(h.modelPath, {}));
	Done(model->Reimport(*h.workspace->Database(), h.workspace->Importer(), true));
	const auto settings = Take(model->SettingsInspector(*h.workspace->Database()));
	auto properties = Take(SerializedObject::Create(settings->Properties()));
	auto normals = Take(properties->FindProperty("/Fields/generateNormals"));
	Done(normals.SetValue(!Take(normals.Value()).get<bool>()));
	Done(properties->Apply());
	const auto modelBytes = Take(h.files.Read(Take(h.workspace->Database()->ImportedObject(guid)).path));
	const auto document = nlohmann::json::from_cbor(modelBytes.begin(), modelBytes.end());
	for (const auto& node : document.at("nodes"))
	{
		const auto nodeId = node.at("localFileId").get<UInt64>();
		Done(model->SelectNode(nodeId));
		Expect(model->SelectedNode() == nodeId && model->HasModifiedSettings(),
		       "Node inspection changed identity or discarded importer draft");
	}
	Expect(!model->SelectNode(123) && model->HasModifiedSettings(),
	       "Missing hierarchy node replaced the selected node");
	h.extraUi = [&]
	{
		ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({640, 850}, ImGuiCond_Always);
		ImGui::Begin("Imported Model Hierarchy");
		ImGui::GetStateStorage()->SetInt(ImGui::GetID("Hierarchy"), 1);
		Done(model->Draw(*h.workspace->Database(), h.workspace->Importer(), true, {}, {}, h.workspace.get(), {}, true));
		ImGui::End();
	};
	h.Frame();
	h.Frame();
	h.Capture("18-Model-Hierarchy-Node.png");
	h.extraUi = {};
	Expect(h.author->GetScene().RootGameObjects() == roots && h.documents->Current()->Dirty() &&
	           model->HasModifiedSettings(),
	       "Browsing hierarchy nodes mutated the author Scene or discarded its importer draft");
	Done(settings->Discard());
	const auto noGeometryPath = Path("prj:NoGeometry.fbx");
	Done(h.files.WriteAtomic(noGeometryPath, h.sourceBytes));
	Done(h.workspace->Refresh());
	auto candidate = Take(ModelImporter::BuildCandidate(*h.workspace->Database(), noGeometryPath, {}));
	for (const auto& entry : candidate.meshes)
	{
		candidate.displayNames.erase(entry.first);
	}
	candidate.meshes.clear();
	candidate.meshOrder.clear();
	candidate.meshMaterialBindings.clear();
	for (auto& node : candidate.nodes)
	{
		node.meshes.clear();
	}
	auto outputs = Take(candidate.Encode());
	const auto selectedClip = candidate.animationOrder.front();
	const auto publish = [&]
	{
		Done(h.workspace->Database()->PublishImport(noGeometryPath, "ursa.Model", ModelImporter::Version,
		                                            ModelImporter::ContentTarget, ModelImportSettings{}.ToJson(),
		                                            candidate.dependencies, outputs, candidate.mainObjectLocalFileId));
		h.preview->Reload();
		h.Frame();
		h.Frame();
	};
	publish();
	h.selection.SelectAsset(noGeometryPath, selectedClip, "AnimationClip", candidate.guid);
	h.Frame();
	h.Frame();
	Expect(!h.preview->State().HasImage() && h.selection.assetLocalFileId == selectedClip &&
	           h.preview->State().Diagnostic().find("geometry") != String::npos,
	       "An animation without geometry displayed an old model image or lacked a diagnostic");
	const auto clipInspector =
	    Take(ImportedAssetInspector::Open(*h.workspace, {candidate.guid, selectedClip}, "AnimationClip"));
	Expect(!clipInspector->Context().canEditTarget, "Animation metadata became unavailable without model geometry");
	for (auto& output : outputs)
	{
		if (output.localFileId == candidate.mainObjectLocalFileId)
		{
			output.bytes = {0};
		}
	}
	publish();
	Expect(!h.preview->State().HasImage() && h.selection.assetLocalFileId == selectedClip &&
	           !h.preview->State().Diagnostic().empty(),
	       "A failed animation model context displayed another target");
	Take(ImportedAssetInspector::Open(*h.workspace, {candidate.guid, selectedClip}, "AnimationClip"));
	Done(h.workspace->Database()->Remove(noGeometryPath));
}

void Run(const std::filesystem::path& evidence, bool managedOnly = false)
{
	Harness h(evidence, managedOnly);
#if defined(URSA_ENABLE_DOTNET)
	if (managedOnly)
	{
		ManagedPreviewContracts(h);
		return;
	}
#else
	(void)managedOnly;
#endif
	ImportedUiContracts(h);
	h.Select(h.materialPath);
	const auto sphere = h.lastPixels;
	h.Capture("01-Material-Sphere.png");
	h.Choose("##Shape", "Cube");
	const auto cube = h.lastPixels;
	h.Choose("##Shape", "Plane");
	Expect(Difference(sphere, cube) > 20 && Difference(cube, h.lastPixels) > 20,
	       "Shape widgets did not change rendered geometry");
	h.Select(h.otherMaterial);
	Expect(Difference(sphere, h.lastPixels) > 100, "Selecting B retained material A");
	h.Select(h.materialPath);
	const auto beforeOrbit = h.lastPixels;
	const auto image = h.preview->ImageRect();
	h.Mouse({image.x + image.width * .5f, image.y + image.height * .5f}, false);
	h.Mouse({image.x + image.width * .5f, image.y + image.height * .5f}, true);
	h.Mouse({image.x + image.width * .5f + 40, image.y + image.height * .5f + 15}, true);
	h.Mouse({image.x + image.width * .5f + 40, image.y + image.height * .5f + 15}, false);
	Expect(Difference(beforeOrbit, h.lastPixels) > 5, "Real mouse orbit did not change the preview");
	const auto beforeZoom = h.lastPixels;
	ImGui::GetIO().AddMouseWheelEvent(0, 2);
	h.Frame();
	Expect(Difference(beforeZoom, h.lastPixels) > 5, "Preview wheel zoom did not change pixels");
	h.Activate("Reset view");
	Expect(!h.focused && Take(h.files.Read(h.materialPath)) == h.materialBytes,
	       "Preview input reached Scene/Game or changed material bytes");
	const auto staticFrames = h.preview->RenderCount();
	h.Frame();
	h.Frame();
	Expect(h.preview->RenderCount() == staticFrames, "Static preview rendered without a change");
	h.Select(h.prefabPath);
	const auto bounds = Take(h.preview->Content()->GeometryBounds());
	Expect(!bounds.empty && bounds.maximum.x < 2, "Disabled Prefab subtree affected framing");
	h.Capture("02-Prefab.png");
	h.Select(Path("prj:Two.obj"));
	h.Capture("03-Whole-Model.png");
	const auto fullModel = Take(h.preview->Content()->RenderScene()->CollectPreviewRenderData(Camera{}, 1));
	Expect(fullModel.objects.size() == 2 && fullModel.objects[0].material != fullModel.objects[1].material &&
	           std::abs(fullModel.objects[0].parameters.baseColor.red - fullModel.objects[1].parameters.baseColor.red) >
	               .5f,
	       "Whole model lost a node or per-node material binding");
	Vector<UInt64> ids;
	for (const auto& artifact : h.workspace->Database()->ImportedArtifacts())
	{
		if (artifact.guid == h.preview->State().Target().identity.guid && artifact.kind == "Mesh")
		{
			ids.push_back(artifact.localFileId);
		}
	}
	Expect(ids.size() >= 2, "Multi-mesh fixture lost sub-assets");
	h.Choose("##Subasset",
	         Take(h.workspace->Database()->ImportedObject(h.preview->State().Target().identity.guid, ids[0])).Label());
	h.Frame();
	Expect(h.preview->State().Target().identity.localFileId == ids[0], "Mesh picker did not resolve local identity");
	const auto indicesA = h.preview->Content()->MeshAsset()->Snapshot()->data.indices.size();
	h.Choose("##Subasset",
	         Take(h.workspace->Database()->ImportedObject(h.preview->State().Target().identity.guid, ids[1])).Label());
	h.Frame();
	Expect(h.preview->State().Target().identity.localFileId == ids[1] &&
	           h.preview->Content()->MeshAsset()->Snapshot()->data.indices.size() != indicesA,
	       "Subasset preview reused geometry/statistics for another local file ID");
	h.Capture("04-Mesh.png");
	h.Select(h.staticGltfPath);
	const auto staticGltfFrame = Take(h.preview->Content()->RenderScene()->CollectPreviewRenderData(Camera{}, 1));
	Expect(!staticGltfFrame.objects.empty() &&
	           std::any_of(staticGltfFrame.objects.begin(), staticGltfFrame.objects.end(),
	                       [](const Rendering::RenderObject& object)
	                       {
		                       return object.material && object.material->IsShaderBacked() &&
		                              object.material->ShaderId() == Shader::BuiltinPbrShaderId();
	                       }),
	       "Static GLB preview did not render through its imported PBR material");
	const auto [minimumPreviewPixel, maximumPreviewPixel] =
	    std::minmax_element(h.lastPixels.begin(), h.lastPixels.end());
	Expect(minimumPreviewPixel != h.lastPixels.end() && *maximumPreviewPixel - *minimumPreviewPixel > 16,
	       "Static GLB PBR material produced no visible preview pixel range");
	h.Capture("04b-Static-GLB-PBR.png");
	h.Select(h.staticTextGltfPath);
	const auto externalGltfFrame = Take(h.preview->Content()->RenderScene()->CollectPreviewRenderData(Camera{}, 1));
	Expect(!externalGltfFrame.objects.empty() &&
	           std::any_of(externalGltfFrame.objects.begin(), externalGltfFrame.objects.end(),
	                       [](const Rendering::RenderObject& object)
	                       {
		                       return object.material && object.material->IsShaderBacked() &&
		                              object.material->ShaderId() == Shader::BuiltinPbrShaderId();
	                       }),
	       "External glTF preview did not render through its imported PBR material");
	h.Capture("04c-Static-glTF-PBR.png");
	const auto reimportedTextGltf =
	    Take(ModelImporter::Import(*h.workspace->Database(), h.staticTextGltfPath, ModelImportSettings{}, true));
	Vector<UInt64> reimportedMeshIds;
	for (const auto& artifact : reimportedTextGltf.subAssets)
	{
		if (artifact.kind == "Mesh")
		{
			reimportedMeshIds.push_back(artifact.localFileId);
		}
	}
	Expect(reimportedTextGltf.guid == h.staticTextGltfGuid && reimportedMeshIds == h.staticTextGltfMeshIds,
	       "External glTF reimport changed stable model or Mesh identities");
	h.Select(h.modelPath);
	Expect(!h.preview->Content()->SelectedClip(), "New model did not start in bind pose");
	const auto clips = h.preview->Content()->Clips();
	Expect(!clips.empty(), "Animated preview lost clips");
	h.Choose("##AnimationClip", clips[0].name, clips[0].localFileId);
	Expect(h.preview->Clock().Time() == 0 && !h.preview->Clock().Playing(), "New clip did not start paused at zero");
	const auto start = h.lastPixels;
	const auto startPalette = h.lastPalette;
	h.Activate("Play");
	h.Frame(.25f);
	h.Frame(.25f);
	Expect(h.preview->Clock().Playing() && h.preview->Clock().Time() > 0 && Difference(start, h.lastPixels) > 5,
	       "Visible clock did not animate real skinned pixels");
	bool changedPalette = false;
	for (Size i = 0; i < startPalette.size(); ++i)
	{
		for (UInt32 row = 0; row < 4; ++row)
		{
			for (UInt32 col = 0; col < 4; ++col)
			{
				changedPalette |= std::abs(startPalette[i].At(row, col) - h.lastPalette[i].At(row, col)) > .00001f;
			}
		}
	}
	Expect(changedPalette, "Animated GPU pixels changed without sampling a different joint palette");
	SkinnedMeshRendererComponent* previewSkin = nullptr;
	for (const auto& root : h.preview->Content()->RenderScene()->RootGameObjects())
	{
		previewSkin = FindSkinnedRenderer(root);
		if (previewSkin)
		{
			break;
		}
	}
	Expect(previewSkin && previewSkin->SharedMesh(), "Animated preview has no skinned mesh binding");
	const auto previewMeshSnapshot = previewSkin->SharedMesh()->Snapshot();
	Expect(bool(previewMeshSnapshot), "Animated preview skinned mesh resource was destroyed");
	const auto& previewBinding = previewMeshSnapshot->data;
	const ResourceId modelGuid = Take(h.workspace->Database()->Metadata(h.modelPath)).guid;
	AnimatorComponent runtimeAnimator;
	Done(runtimeAnimator.SetAnimation(modelGuid, previewBinding.skeletonReference.localFileId, clips[0].localFileId,
	                                  h.preview->Content()->Resources()));
	Done(runtimeAnimator.SetTime(h.preview->Clock().Time()));
	AnimatorComponent* previewAnimator = nullptr;
	for (const auto& root : h.preview->Content()->RenderScene()->RootGameObjects())
	{
		previewAnimator = FindAnimator(root);
		if (previewAnimator)
		{
			break;
		}
	}
	Expect(previewAnimator &&
	           previewAnimator->JointGlobalTransforms().size() == runtimeAnimator.JointGlobalTransforms().size(),
	       "Preview and runtime Animator did not expose the same sampled joint pose");
	for (Size joint = 0; joint < runtimeAnimator.JointGlobalTransforms().size(); ++joint)
	{
		for (Size element = 0; element < runtimeAnimator.JointGlobalTransforms()[joint].values.size(); ++element)
		{
			Expect(std::abs(previewAnimator->JointGlobalTransforms()[joint].values[element] -
			                runtimeAnimator.JointGlobalTransforms()[joint].values[element]) < .00001f,
			       "Preview and runtime Animator poses differ at the same clip time");
		}
	}
	h.Capture("05-Animation-Playing.png");
	const auto hiddenTime = h.preview->Clock().Time();
	const auto hiddenFrames = h.preview->RenderCount();
	h.Frame(.5f, false);
	h.Frame(.5f, false);
	Expect(h.preview->Clock().Time() == hiddenTime && h.preview->RenderCount() == hiddenFrames,
	       "Hidden preview advanced or rendered");
	h.Frame(.5f);
	Expect(h.preview->Clock().Time() == hiddenTime, "Restoring preview caught up hidden wall time");
	h.TogglePreview(true);
	const auto collapsedTime = h.preview->Clock().Time(),
	           collapsedFrames = static_cast<Float64>(h.preview->RenderCount());
	h.Frame(.5f);
	Expect(h.preview->Clock().Time() == collapsedTime && h.preview->RenderCount() == collapsedFrames,
	       "Collapsed preview updated");
	h.TogglePreview(false);
	const auto dock = ImHashStr("PreviewVisibilityDock");
	ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_None);
	ImGui::DockBuilderSetNodePos(dock, {650, 0});
	ImGui::DockBuilderSetNodeSize(dock, {440, 850});
	ImGui::DockBuilderDockWindow("Inspector", dock);
	ImGui::DockBuilderDockWindow("Other Inspector Tab", dock);
	ImGui::DockBuilderFinish(dock);
	h.docked = true;
	h.Frame();
	h.Frame();
	auto* other = ImGui::FindWindowByName("Other Inspector Tab");
	auto* dockNode = other->DockNode;
	Expect(dockNode && dockNode->TabBar, "Preview custom dock was not created");
	ImGui::TabBarQueueFocus(dockNode->TabBar, ImGui::TabBarFindTabByID(dockNode->TabBar, other->TabId));
	h.Frame();
	h.Frame();
	Expect(!ImGui::FindWindowByName("Inspector")->DockTabIsVisible, "Inspector test tab did not become inactive");
	const auto tabTime = h.preview->Clock().Time();
	const auto tabFrames = h.preview->RenderCount();
	h.Frame(.5f);
	Expect(h.preview->Clock().Time() == tabTime && h.preview->RenderCount() == tabFrames,
	       "Inactive Inspector tab advanced preview");
	ImGui::DockBuilderRemoveNode(dock);
	h.docked = false;
	h.Frame();
	h.Frame();
	h.inspectorHeight = 70;
	h.Frame(.5f);
	const auto zeroTime = h.preview->Clock().Time();
	h.Frame(.5f);
	Expect(h.preview->Clock().Time() == zeroTime, "Zero-area preview advanced animation");
	h.inspectorHeight = 850;
	h.Frame();
	const auto timeline = h.preview->TimelineRect();
	Expect(timeline.width > 0, "Animation timeline has no usable rect");
	h.Capture("05b-Animation-Restored.png");
	h.Mouse({timeline.x + timeline.width * .5f, timeline.y + timeline.height * .5f}, false);
	h.Mouse({timeline.x + timeline.width * .5f, timeline.y + timeline.height * .5f}, true);
	h.Mouse({timeline.x + timeline.width + 20, timeline.y + timeline.height * .5f}, true);
	h.Mouse({timeline.x + timeline.width + 20, timeline.y + timeline.height * .5f}, false);
	std::cout << "Scrub " << h.preview->Clock().Time() << '/' << h.clipDuration << "; playing "
	          << h.preview->Clock().Playing() << "; rect y " << timeline.y << '\n';
	Expect(!h.preview->Clock().Playing() && std::abs(h.preview->Clock().Time() - h.clipDuration) < .001,
	       "Timeline endpoint scrub wrapped to zero or kept playing");
	h.Capture("06-Animation-End.png");
	h.preview->Reload();
	h.Frame();
	h.Frame();
	Expect(h.preview->Content()->SelectedClip() == clips[0].localFileId && !h.preview->Clock().Playing() &&
	           std::abs(h.preview->Clock().Time() - h.clipDuration) < .001,
	       "Reimport did not retain stable clip identity/time paused");
	h.PublishAnimation(h.clipDuration * .5, false);
	Expect(h.preview->Content()->SelectedClip() == clips[0].localFileId && !h.preview->Clock().Playing() &&
	           std::abs(h.preview->Clock().Time() - h.clipDuration * .5) < .00001,
	       "Shortened reimport did not clamp the paused time");
	h.PublishAnimation(0, false);
	Expect(!h.preview->Content()->Clips().empty() && h.preview->Content()->Clips()[0].duration == 0 &&
	           !h.preview->Content()->SelectedClip(),
	       "Zero-duration reimport remained playable");
	h.PublishAnimation(h.clipDuration, true);
	Expect(h.selection.assetMissing && h.selection.assetLocalFileId == clips[0].localFileId && !h.preview->Content() &&
	           !h.preview->State().HasImage() && !h.preview->Clock().Playing(),
	       "Removed clip did not retain a missing exact selection or left an old preview visible");
	Take(ModelImporter::Import(*h.workspace->Database(), h.modelPath, ModelImportSettings{}, true));
	h.preview->Reload();
	h.Frame();
	h.Frame();
	h.Choose("##AnimationClip", "Bind pose");
	Expect(!h.preview->Content()->SelectedClip() && h.preview->Clock().Duration() == 0,
	       "Bind pose did not clear animation state");
	h.Select(h.texturePath);
	h.Capture("07-Transparent-Texture.png");
	h.Activate("100%");
	h.Activate("Fit");
	h.Choose("##Channel", "Alpha");
	h.Capture("08-Texture-Alpha.png");
	Expect(std::abs(int(h.FirstTextureRed()) - 128) < 4, "Alpha GPU display was gamma transformed");
	h.Choose("##Channel", "RGB");
	h.Capture("08-Texture-RGB.png");
	Expect(std::abs(int(h.FirstTextureRed()) - 55) < 4, "sRGB GPU display decoded twice or ignored color space");
	h.Activate("Mip");
	ImGui::GetIO().AddKeyEvent(ImGuiKey_RightArrow, true);
	h.Frame();
	ImGui::GetIO().AddKeyEvent(ImGuiKey_RightArrow, false);
	h.Frame();
	Expect(h.preview->DisplayedTextureSize().width < 4, "Mip widget did not select actual imported mip pixels");
	h.Activate("Fit");
	h.Activate("Source");
	h.Choose("##Channel", "RGBA");
	for (const auto dpi : {1.f, 1.5f, 2.f})
	{
		h.dpi = dpi;
		h.inspectorWidth = 210;
		h.Select(h.texturePath);
		h.Activate("Source");
		h.Choose("##Channel", "RGBA");
		h.Frame();
		h.Frame();
		Expect(h.preview->ImageRect().width > 0 && h.preview->ImageRect().height > 0,
		       "Narrow/DPI preview lost image region");
		h.Capture(("09-Texture-DPI-" + std::to_string(int(dpi * 100)) + ".png").c_str());
		for (const auto& path : {h.materialPath, Path("prj:Two.obj"), h.prefabPath, h.modelPath})
		{
			h.Select(path);
			Expect(h.preview->ImageRect().width > 0 && h.preview->ImageRect().height > 0,
			       "3D narrow/DPI preview lost its image region");
			if (path == h.modelPath)
			{
				const auto clip = h.preview->Content()->Clips()[0];
				h.Choose("##AnimationClip", clip.name, clip.localFileId);
				const auto time = h.preview->TimelineRect();
				h.Mouse({time.x + time.width * .5f, time.y + time.height * .5f}, false);
				h.Mouse({time.x + time.width * .5f, time.y + time.height * .5f}, true);
				h.Mouse({time.x + time.width + 20, time.y + time.height * .5f}, true);
				h.Mouse({time.x + time.width + 20, time.y + time.height * .5f}, false);
				Expect(std::abs(h.preview->Clock().Time() - clip.duration) < .00001 && !h.preview->Clock().Playing(),
				       "Narrow/DPI timeline lost its exact endpoint");
				const auto endTime = h.preview->Clock().Time();
				const auto endFrames = h.preview->RenderCount();
				h.Frame(2, false);
				Expect(h.preview->Clock().Time() == endTime && h.preview->RenderCount() == endFrames,
				       "Hidden narrow/DPI preview advanced or rendered");
				h.Frame();
				Expect(h.preview->Clock().Time() == endTime, "DPI restore changed the sampled endpoint");
			}
			h.Capture(
			    ("09-" + ToStdString(path.FileName()) + "-DPI-" + std::to_string(int(dpi * 100)) + ".png").c_str());
		}
		h.Choose(
		    "##Subasset",
		    Take(h.workspace->Database()->ImportedObject(h.preview->State().Target().identity.guid, h.meshId)).Label());
		h.Frame();
		Expect(h.preview->State().Target().identity.localFileId == h.meshId && h.preview->ImageRect().width > 0 &&
		           h.preview->ImageRect().height > 0,
		       "Narrow/DPI Mesh selection did not produce the requested sub-asset");
		Expect(!h.lastPalette.empty(), "Skinned Mesh preview did not sample its bind-pose palette");
		Size foreground = 0;
		for (Size pixel = 0; pixel < h.lastPixels.size(); pixel += 4)
		{
			foreground += std::abs(int(h.lastPixels[pixel]) - int(h.lastPixels[0])) > 5 ||
			              std::abs(int(h.lastPixels[pixel + 1]) - int(h.lastPixels[1])) > 5 ||
			              std::abs(int(h.lastPixels[pixel + 2]) - int(h.lastPixels[2])) > 5;
		}
		Expect(foreground > 20, "Skinned Mesh preview published an empty GPU image");
		h.Capture(("09-Mesh-DPI-" + std::to_string(int(dpi * 100)) + ".png").c_str());
	}
	h.dpi = 1;
	h.inspectorWidth = 440;
	h.Frame();
	h.Select(h.materialPath);
	const auto good = h.preview->RenderCount();
	h.failRender = true;
	const auto oldPipeline = h.preview->State().DisplayedTarget().pipelineVersion;
	h.preview->Reload(true);
	h.Frame();
	Expect(h.preview->State().HasImage() && !h.preview->State().Diagnostic().empty() &&
	           h.preview->RenderCount() == good,
	       "Pipeline failure discarded same-target last good frame");
	Expect(h.preview->State().IsStale() && h.preview->State().DisplayedTarget().pipelineVersion == oldPipeline &&
	           h.preview->State().Target().pipelineVersion == oldPipeline + 1,
	       "Failed pipeline candidate was presented as the published version");
	h.Frame();
	const auto failedCalls = h.previewCalls;
	h.Frame();
	h.Frame();
	Expect(h.previewCalls == failedCalls, "Unchanged failed preview retried every frame");
	h.selection.SelectAsset(h.otherMaterial);
	h.Frame();
	Expect(!h.preview->State().HasImage(), "Failed target B displayed target A image");
	h.failRender = false;
	h.preview->Reload();
	h.Frame();
	h.Frame();
	Expect(h.preview->State().HasImage(), "Preview did not recover after failure");
	Expect(!h.preview->State().IsStale() &&
	           h.preview->State().DisplayedTarget().pipelineVersion == h.preview->State().Target().pipelineVersion,
	       "Successful recovery retained stale pipeline metadata");
	const auto beforeLate = h.preview->RenderCount();
	h.duringRender = [&] { h.preview->Reload(true); };
	h.preview->Reload();
	h.Frame();
	Expect(h.preview->RenderCount() == beforeLate && h.preview->State().IsStale(),
	       "Render completion published after its generation was invalidated");
	h.Frame();
	h.Frame();
	Expect(!h.preview->State().IsStale(), "Current request failed to recover after a late result was discarded");
	h.Select(h.materialPath);
	auto invalidShader = Take(ReadJsonResource(h.files, h.materialPath, ResourceKind::Material));
	invalidShader["shaderId"] = AssetReferenceToJson({ResourceId::Generate(), 0});
	const auto beforeShaderFailure = h.preview->RenderCount();
	Done(WriteJsonResource(h.files, h.materialPath, invalidShader));
	h.preview->Reload();
	h.Frame();
	Expect(h.preview->State().HasImage() && !h.preview->State().Diagnostic().empty() &&
	           h.preview->RenderCount() == beforeShaderFailure,
	       "Invalid Shader was replaced by a default material or discarded the last good frame");
	Done(h.files.WriteAtomic(h.materialPath, h.materialBytes));
	h.preview->Reload();
	h.Frame();
	h.Frame();
	h.Select(h.texturePath);
	const auto originalTextureSettings = Take(TextureImporter::Settings(*h.workspace->Database(), h.texturePath));
	const auto originalImportVersion = h.preview->State().DisplayedImportVersion();
	Expect(!originalImportVersion.empty(), "Imported preview did not record its successful artifact version");
	auto pendingTextureSettings = originalTextureSettings;
	pendingTextureSettings.sRgb = !pendingTextureSettings.sRgb;
	Done(TextureImporter::SetSettings(*h.workspace->Database(), h.texturePath, pendingTextureSettings));
	h.Frame();
	h.Frame();
	Expect(!h.preview->Content()->Diagnostic().empty() &&
	           h.preview->Content()->Texture()->GetColorSpace() == TextureColorSpace::Srgb,
	       "Unapplied settings were presented as a new imported texture");
	Expect(h.preview->State().DisplayedImportVersion() == originalImportVersion,
	       "Unapplied settings relabeled the published artifact version");
	Done(TextureImporter::SetSettings(*h.workspace->Database(), h.texturePath, originalTextureSettings));
	const auto originalPng = Take(h.files.Read(h.texturePath));
	Done(h.files.WriteAtomic(h.texturePath, {1, 2, 3}));
	h.preview->Reload();
	h.Frame();
	h.Frame();
	Expect(h.preview->State().HasImage() && !h.preview->Content()->Diagnostic().empty(),
	       "Failed image decode discarded the last imported result");
	Done(h.files.WriteAtomic(h.texturePath, originalPng));
	h.preview->Reload();
	h.Frame();
	h.Frame();
	h.Select(h.prefabPath);
	Done(h.files.Remove(h.materialPath));
	h.preview->Reload();
	h.Frame();
	Expect(h.preview->State().HasImage() && !h.preview->State().Diagnostic().empty(),
	       "Missing dependency discarded same-target last good preview");
	Done(h.files.WriteAtomic(h.materialPath, h.materialBytes));
	h.preview->Reload();
	h.Frame();
	h.Frame();
	h.Select(h.materialPath);
	Done(h.files.Remove(h.materialPath));
	h.Frame();
	Expect(!h.preview->State().HasImage(), "Deleted selection retained a previous asset frame");
	Done(h.files.WriteAtomic(h.materialPath, h.materialBytes));
	Done(h.workspace->Refresh());
	Done(h.browser.Refresh(*h.workspace));
	const auto registeredBeforeStress = h.gui->RegisteredTextureCount();
	for (int i = 0; i < 12; ++i)
	{
		h.Select(i % 2 ? h.materialPath : h.texturePath);
	}
	Expect(h.gui->RegisteredTextureCount() <= registeredBeforeStress + 2,
	       "Repeated selection leaked registered UI textures");
	ContentContracts(h);
	h.Select(h.materialPath);
	const auto initialHeight = h.preview->ReservedHeight();
	const auto initialImageHeight = h.preview->ImageRect().height;
	Expect(h.preview->DividerRect().height >= 10, "Preview divider has an undersized drag target");
	h.ResizePreview(-50);
	Expect(h.preview->ReservedHeight() > initialHeight + 20, "Preview divider did not resize the host");
	Expect(h.preview->ImageRect().height > initialImageHeight + 20, "Preview image did not grow with the divider");
	const auto preferredHeight = h.preview->ReservedHeight();
	h.inspectorHeight = 260;
	h.Frame();
	h.Frame();
	auto* inspectorWindow = ImGui::FindWindowByName("Inspector");
	Expect(h.preview->ImageRect().y + h.preview->ImageRect().height <= inspectorWindow->InnerRect.Max.y + 1 &&
	           inspectorWindow->ScrollMax.y == 0,
	       "Short Inspector pushed Preview outside its visible area");
	Expect(h.preview->ReservedHeight() == preferredHeight, "Shrinking Inspector overwrote preferred Preview height");
	h.ResizePreview(20);
	Expect(h.preview->ReservedHeight() < 220, "Dragging a constrained Preview used its hidden preferred height");
	h.inspectorHeight = 850;
	h.Frame();
	h.ResizePreview(-2000);
	Expect(h.preview->ReservedHeight() < h.inspectorHeight - 40 && inspectorWindow->ScrollMax.y == 0,
	       "Preview drag exceeded Inspector height or scrolled its divider away");
	h.ResizePreview(2000);
	Expect(h.preview->ReservedHeight() == 120, "Preview drag did not stop at its minimum height");
	h.ResizePreview(-200);
	h.Capture("09-Preview-Resize.png");
	const auto savedHeight = h.preview->ReservedHeight();
	h.TogglePreview(true);
	h.preview.reset();
	h.preview = std::make_unique<AssetPreviewPanel>(h.device, *h.gui);
	h.Frame();
	Expect(h.preview->ReservedHeight() < 100 && !h.preview->State().HasImage(),
	       "Restart did not restore collapsed Preview");
	h.TogglePreview(false);
	h.Frame();
	Expect(std::abs(h.preview->ReservedHeight() - savedHeight) < 1, "Restart lost saved Preview height");
	Done(h.files.WriteAtomic(Path("user:Settings/AssetPreviews/preview-test.json"), {1, 2, 3}));
	h.preview.reset();
	h.preview = std::make_unique<AssetPreviewPanel>(h.device, *h.gui);
	h.Frame();
	h.Frame();
	Expect(h.preview->ReservedHeight() == 280 && h.preview->State().HasImage(),
	       "Malformed Preview settings prevented bounded defaults");
	for (const auto* invalid : {R"({"height":700})", R"({"height":700,"expanded":"invalid"})",
	                            R"({"height":null,"expanded":false})", R"({"height":1e100,"expanded":false})"})
	{
		const String text(invalid);
		Done(h.files.WriteAtomic(Path("user:Settings/AssetPreviews/preview-test.json"),
		                         Vector<UInt8>(text.data(), text.data() + text.size())));
		h.preview.reset();
		h.preview = std::make_unique<AssetPreviewPanel>(h.device, *h.gui);
		h.Frame();
		Expect(h.preview->ReservedHeight() == 280 && !h.preview->SettingsDiagnostic().empty(),
		       "Partially valid settings were applied instead of atomic defaults");
	}
	const String bounded = R"({"height":1800,"expanded":true})";
	Done(h.files.WriteAtomic(Path("user:Settings/AssetPreviews/preview-other.json"),
	                         Vector<UInt8>(bounded.data(), bounded.data() + bounded.size())));
	h.projectKey = "preview-other";
	h.dpi = 2;
	h.Frame();
	Expect(h.preview->ReservedHeight() == 1200 && h.preview->SettingsDiagnostic().empty(),
	       "Project-specific logical height was not clamped independently of DPI");
	h.projectKey = "preview-test";
	h.dpi = 1;
	h.Frame();
	Expect(h.preview->ReservedHeight() == 280, "Project switch inherited another project's preview settings");
	h.settings->failRead = true;
	h.preview.reset();
	h.preview = std::make_unique<AssetPreviewPanel>(h.device, *h.gui);
	h.Frame();
	Expect(h.preview->ReservedHeight() == 280 && !h.preview->SettingsDiagnostic().empty(),
	       "Unreadable settings did not use complete defaults with a diagnostic");
	h.settings->failRead = false;
	const auto beforeFailedWrite = Take(h.files.Read(Path("user:Settings/AssetPreviews/preview-test.json")));
	h.settings->failWrite = true;
	h.TogglePreview(true);
	Expect(h.preview->ReservedHeight() < 100 && !h.preview->SettingsDiagnostic().empty() &&
	           Take(h.files.Read(Path("user:Settings/AssetPreviews/preview-test.json"))) == beforeFailedWrite,
	       "Failed settings save discarded in-memory state or changed persisted settings");
	h.preview->Expand();
	Expect(h.preview->ReservedHeight() == 280 && !h.preview->SettingsDiagnostic().empty(),
	       "Expanding Preview after a failed save did not retain its in-memory height");
	h.settings->failWrite = false;
	const auto scenePosition = ImGui::FindWindowByName("Scene")->Pos;
	ImGui::LoadIniSettingsFromMemory("[Window][Material Preview]\nPos=20,20\nSize=256,256\nCollapsed=0\n");
	h.Frame();
	Expect(!ImGui::FindWindowByName("Material Preview") && ImGui::FindWindowByName("Scene")->Pos.x == scenePosition.x &&
	           ImGui::FindWindowByName("Scene")->Pos.y == scenePosition.y,
	       "Legacy layout record created a window or reset another panel");
	h.selection.Clear();
	h.Frame();
	Expect(!h.preview->State().HasImage(), "Deselection retained an asset frame");
	h.Frame();
	const auto registrationsAtRest = h.gui->RegisteredTextureCount();
	for (UInt32 iteration = 0; iteration < 12; ++iteration)
	{
		h.Select(iteration % 2 ? h.texturePath : h.prefabPath);
		auto submittedUi = h.SubmitUi();
		if (iteration % 4 == 0)
		{
			auto stressSettings = originalTextureSettings;
			stressSettings.sRgb = iteration % 8 == 0;
			Take(TextureImporter::Import(*h.workspace->Database(), h.texturePath, stressSettings, "texture-rgba8",
			                             true));
		}
		h.preview->Reload();
		h.Frame();
		h.Frame();
		Done(submittedUi->Wait());
	}
	h.Select(h.prefabPath);
	std::weak_ptr<GameObject> previewRoot = h.preview->Content()->RenderScene()->RootGameObjects()[0];
	auto submittedUi = h.SubmitUi();
	h.selection.Clear();
	h.Frame();
	h.Frame();
	Done(submittedUi->Wait());
	Expect(previewRoot.expired() && !h.preview->Content() && h.gui->RegisteredTextureCount() == registrationsAtRest,
	       "Submitted-frame selection/reload retained private scene or registered preview images");
	Expect(Take(h.files.Read(h.modelPath)) == h.sourceBytes && Take(h.files.Read(h.materialPath)) == h.materialBytes &&
	           Take(h.files.Read(Path("prj:Unsaved.ursascene"))) == h.sceneBytes,
	       "Viewing changed source bytes");
	Expect(Probe::callbacks == 0, "Private preview ran lifecycle callbacks");
	Done(h.device->WaitIdle());
	std::cout << "Inspector preview: five asset types, actual widgets/mouse, GPU skinning, isolation, DPI, failure "
	             "recovery passed\n";
}
} // namespace

int main(int argc, char** argv)
{
	try
	{
		const auto evidence =
		    argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path("AssetPreviewEvidence");
		const bool managedOnly = argc > 2 && std::string(argv[2]) == "--managed-only";
		Run(evidence / "Loose", managedOnly);
		const auto engine = Take(OpenEngineFileSystem());
		VirtualFileSystem output;
		Done(output.Mount("out", evidence, MountAccess::ReadWrite));
		Done(PakBuilder::Build(engine, {engineScheme}, output, Path("out:PreviewEngine.pak")));
		auto packaged = std::make_shared<VirtualFileSystem>(Take(OpenEngineFileSystem(evidence / "PreviewEngine.pak")));
		ConfigureEngineFileSystem(packaged);
		Run(evidence / "Pak", managedOnly);
		ConfigureEngineFileSystem(nullptr);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
