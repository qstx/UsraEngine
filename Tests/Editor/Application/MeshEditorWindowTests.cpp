#include <Application/EditorApplication.hpp>
#include <Assets/MeshAssetEditor.hpp>
#include <Assets/ModelImportView.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Mesh/Primitive.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Serialization/MeshResource.hpp>
#include <Core/StringInterop.hpp>
#include <chrono>
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
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

class MeshEditor final : public EditorApplication
{
public:
	bool Verified() const
	{
		return completed && inspectorFrames >= 3 && runs == 2;
	}

protected:
	Result<void> OnStart() override
	{
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(100);
		return {};
	}

	Result<void> OnFrame(Float32) override
	{
		try
		{
			if (std::chrono::steady_clock::now() > deadline)
			{
				throw std::runtime_error("Mesh Editor workflow timed out at phase " + std::to_string(phase) + ": " +
				                         ToStdString(AssetStatus()) + " / " + ToStdString(ScriptStatus()) + " / " +
				                         ToStdString(InspectorStatus()) +
				                         ", prompt=" + std::to_string(Documents()->PromptPending()) +
				                         ", assetsPending=" + std::to_string(AssetWorkPending()) +
				                         ", scriptsPending=" + std::to_string(ScriptWorkPending()) +
				                         ", kind=" + std::to_string(int(Documents()->Current()->Kind())));
			}
			if (!RunningScene() && (AssetWorkPending() || ScriptWorkPending()))
			{
				return {};
			}
			if (phase == 0)
			{
				auto document =
				    Take(NewSceneDocument(Take(VirtualPath::Parse("prj:" + verificationFolder + "/Scene.ursascene")),
				                          ResourceKind::Scene, Project().components, Documents()->Session()));
				draft = document->Payload<SceneDraft>();
				auto cameraObject = Take(draft->GetScene().CreateGameObject("Camera"));
				LocalTransform cameraTransform;
				cameraTransform.position = {0, 0, 4};
				Done(cameraObject->SetLocalTransform(cameraTransform));
				auto camera = Take(cameraObject->AddComponent<CameraComponent>());
				Done(draft->GetScene().SetActiveCamera(camera->Id()));
				modelView = Take(ModelImportView::Open(Take(VirtualPath::Parse("prj:Models/Cube.fbx")), {}));
				Done(modelView->Reimport(*Workspace()->Database(), true));
				Done(modelView->Instantiate(*draft));
				owner = Take(
				    CreatePrimitive(draft->GetScene(), Documents()->Session(), PrimitiveType::Cube, "Mesh workflow"));
				filter = owner->FindComponent<MeshFilterComponent>();
				renderer = owner->FindComponent<MeshRendererComponent>();
				Done(hierarchy.Select(draft->GetScene(), owner));
				document->MarkDirty();
				Done(Documents()->RequestOpen(std::move(document)));
				phase = 1;
			}
			else if (phase == 1 && inspectorFrames >= 3)
			{
				auto mesh = Take(filter->Mesh());
				auto positions = mesh->Snapshot()->data.positions;
				positions[0].x -= 0.1f;
				Done(mesh->SetVertices(std::move(positions)));
				auto material = Take(renderer->Material());
				Done(material->SetBaseColor({0.1f, 0.6f, 0.3f, 1}));
				Done(renderer->SetMaterials({material, nullptr, material}));
				const auto saved =
				    Take(SaveMeshAs(*Workspace()->Database(), Documents()->Session(),
				                    Take(VirtualPath::Parse("prj:" + verificationFolder + "/Mesh.ursamesh")), mesh));
				Done(filter->SetSharedMesh(saved));
				Documents()->Current()->MarkDirty();
				RefreshAssets();
				phase = 2;
			}
			else if (phase == 2)
			{
				Done(Documents()->SaveCurrent());
				const auto id = Documents()->Current()->Id();
				auto reopened = Take(OpenSceneDocument(Documents()->Current()->Path(), ResourceKind::Scene,
				                                       Project().components, Documents()->Session()));
				Expect(reopened->Id() == id, "Scene save/reopen changed document identity");
				draft = reopened->Payload<SceneDraft>();
				owner = draft->GetScene().RootGameObjects().back();
				filter = owner->FindComponent<MeshFilterComponent>();
				renderer = owner->FindComponent<MeshRendererComponent>();
				Expect(filter && renderer && renderer->SharedMaterials().size() == 3 && !renderer->SharedMaterials()[1],
				       "Scene reopen lost Mesh or material slots");
				Done(Documents()->RequestOpen(std::move(reopened)));
				Done(hierarchy.Select(draft->GetScene(), owner));
				const auto path = Take(VirtualPath::Parse("prj:" + verificationFolder + "/Prefab.ursaprefab"));
				const auto prefabId = ResourceId::Generate();
				Done(SavePrefabResource(Workspace()->Index().Filesystem(), path, prefabId, draft->GetScene(), owner,
				                        Project().components));
				const auto metadata =
				    nlohmann::json{{"version", 1u},         {"guid", ToStdString(prefabId.ToString())},
				                   {"folder", false},       {"importer", ""},
				                   {"importerVersion", 0u}, {"settings", nlohmann::json::object()}}
				        .dump();
				Done(Workspace()->Index().Filesystem().WriteAtomic(
				    Take(VirtualPath::Parse(path.ToString() + ".meta")),
				    Vector<UInt8>(metadata.data(), metadata.data() + metadata.size())));
				ComponentLoadContext context(Documents()->Session());
				auto prefab = Take(
				    LoadPrefabAssetResource(Workspace()->Index().Filesystem(), path, Project().components, context));
				Expect(prefab.Root()->FindComponent<MeshRendererComponent>()->SharedMaterials().size() == 3,
				       "Prefab save/reopen lost slots");
				Documents()->Current()->MarkDirty();
				RequestPlayToggle();
				phase = 3;
			}
			else if (phase == 3 && RunningScene())
			{
				auto runtimeOwner = RunningScene()->RootGameObjects().back();
				auto* runtimeFilter = runtimeOwner->FindComponent<MeshFilterComponent>();
				auto* runtimeRenderer = runtimeOwner->FindComponent<MeshRendererComponent>();
				Expect(runtimeFilter && runtimeRenderer && runtimeFilter->SharedMesh() != filter->SharedMesh(),
				       "Run reused editing Mesh asset");
				Done(runtimeRenderer->SharedMaterial()->SetBaseColor({1, 0, 0, 1}));
				RequestPlayToggle();
				phase = 4;
			}
			else if (phase == 3)
			{
				if (!InspectorStatus().empty())
				{
					throw std::runtime_error(ToStdString(InspectorStatus()));
				}
				RequestPlayToggle();
			}
			else if (phase == 4 && !RunningScene())
			{
				Expect(Documents()->Current()->Dirty() &&
				           renderer->SharedMaterial()->Parameters().baseColor.green == 0.6f,
				       "Stop lost Dirty state or leaked runtime material changes");
				if (++runs < 2)
				{
					RequestPlayToggle();
					phase = 3;
				}
				else
				{
					completed = true;
					window->RequestClose();
				}
			}
			return {};
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(error.what());
		}
	}

	Result<void> OnGui() override
	{
		auto drawn = EditorApplication::OnGui();
		if (drawn && modelView)
		{
			drawn = modelView->Draw(*Workspace()->Database(), false);
		}
		if (phase == 3 && !RunningScene() && !ScriptWorkPending() && !AssetWorkPending())
		{
			RequestPlayToggle();
		}
		if (!drawn || !draft || RunningScene())
		{
			return drawn;
		}
		bool hiddenHierarchy = false;
		bool inspector = true;
		String diagnostic;
		auto inspected =
		    DrawSceneInspector(*Documents()->Current(), hierarchy, *Documents(), *Workspace(), Project().components,
		                       Project().componentEditors, diagnostic, nullptr, &hiddenHierarchy, &inspector);
		if (!inspected)
		{
			return Result<void>::Failure(inspected.error);
		}
		++inspectorFrames;
		return {};
	}

	void OnStop() override
	{
		if (Workspace() && Workspace()->Database())
		{
			auto folder = VirtualPath::Parse("prj:" + verificationFolder);
			if (folder)
			{
				auto removed = Workspace()->Database()->Remove(folder.value);
				auto cleaned =
				    removed ? Workspace()->Index().Filesystem().RemoveDirectory(folder.value, true) : removed;
				completed = completed && bool(cleaned);
			}
		}
	}

private:
	String verificationFolder = "MeshWorkflow-" + ResourceId::Generate().ToString();
	std::unique_ptr<ModelImportView> modelView;
	std::shared_ptr<SceneDraft> draft;
	GameObject::Ptr owner;
	MeshFilterComponent* filter = nullptr;
	MeshRendererComponent* renderer = nullptr;
	EditorHierarchy hierarchy;
	std::chrono::steady_clock::time_point deadline;
	UInt32 phase = 0;
	UInt32 runs = 0;
	UInt32 inspectorFrames = 0;
	bool completed = false;
};
} // namespace

int main(int count, char** values)
{
	try
	{
		Expect(count == 2, "Mesh Editor window test requires a prepared Demo fixture");
		MeshEditor editor;
		Done(editor.OpenProject(std::filesystem::u8path(values[1])));
		Done(editor.Run(1280, 720, "Mesh Editor workflow", false));
		Expect(editor.Verified(), "Mesh Editor window workflow was incomplete");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
