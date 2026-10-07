#include <ResourceFixtures.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <imgui_internal.h>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	const auto resourceRoot =
	    std::filesystem::current_path() / ("hierarchy_resources_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		Scene scene;
		const auto first = Take(scene.CreateGameObject());
		const auto child = Take(scene.CreateGameObject(first));
		const auto second = Take(scene.CreateGameObject());
		done(scene.ReorderGameObject(second, 0));
		done(scene.SetGameObjectName(first, "Root"));
		done(scene.SetGameObjectName(child, "Child"));
		done(scene.SetEnabled(child, false));
		Take(scene.AddComponent<BoxRenderComponent>(first));
		const auto firstId = Take(scene.PersistentId(first));
		const auto childId = Take(scene.PersistentId(child));
		const auto secondId = Take(scene.PersistentId(second));
		Editor::EditorHierarchy hierarchy;
		done(hierarchy.Select(scene, child));

		const auto path = std::filesystem::current_path() / "hierarchy.ursascene";
		const auto document = Take(EncodeSceneDocument(path, ResourceId::Generate(), scene, registry));
		std::filesystem::create_directories(resourceRoot);
		const auto resources = Take(ResourceSession::Open(resourceRoot));
		ComponentLoadContext context(*resources);
		auto loaded = Take(DecodeSceneDocument(path, document, registry, context));
		Expect(loaded.scene->RootGameObjects().size() == 2, "Root count changed after save/reopen");
		Expect(Take(loaded.scene->PersistentId(loaded.scene->RootGameObjects()[0])) == secondId,
		       "Root order changed after save/reopen");
		Expect(Take(loaded.scene->PersistentId(loaded.scene->RootGameObjects()[1])) == firstId,
		       "Root ID changed after save/reopen");
		const auto reopenedChild = Take(loaded.scene->GameObjectById(childId));
		Expect(Take(loaded.scene->GameObjectName(reopenedChild)) == "Child", "Child label was not restored");
		Expect(!Take(loaded.scene->IsEnabled(reopenedChild)), "Child enabled state changed");
		Expect(Take(loaded.scene->PersistentId(Take(hierarchy.SelectedGameObject(*loaded.scene)))) == childId,
		       "Hierarchy selection did not survive scene replacement");

		auto legacy = document;
		legacy.erase("sceneGraphVersion");
		auto oldScene = DecodeSceneDocument(path, legacy, registry, context);
		Expect(!oldScene &&
		           oldScene.error.find("Unsupported or missing GameObject Scene format version") != String::npos,
		       "Old Scene format was not rejected with a version diagnostic");

		const auto prefabPath = std::filesystem::current_path() / "hierarchy.ursaprefab";
		done(SavePrefabResource(prefabPath, ResourceId::Generate(), scene, first, registry));
		auto prefab = Take(LoadPrefabResource(prefabPath, registry, context));
		Expect(Take(prefab.scene->GameObjectName(Take(prefab.scene->GameObjectById(firstId)))) == "Root",
		       "Prefab root label was not restored");
		Expect(Take(prefab.scene->GameObjectName(Take(prefab.scene->GameObjectById(childId)))) == "Child",
		       "Prefab child label was not restored");

		auto* imgui = ImGui::CreateContext();
		ImGui::SetCurrentContext(imgui);
		ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
		ImGui::GetIO().DisplaySize = ImVec2(640, 480);
		ImGui::NewFrame();
		const auto displayed = hierarchy.Draw(*loaded.scene);
		Expect(displayed && !displayed.value, "Hierarchy display changed scene data unexpectedly");
		Expect(ImGui::FindWindowByName("Inspector") == nullptr, "Hierarchy opened a second Inspector window");
		ImGui::Render();
		done(loaded.scene->DestroyGameObject(reopenedChild));
		ImGui::NewFrame();
		auto cleared = hierarchy.Draw(*loaded.scene);
		Expect(cleared && !hierarchy.SelectedId(), "Deleting the selected GameObject left a stale Inspector selection");
		ImGui::Render();
		ImGui::DestroyContext(imgui);
		std::filesystem::remove_all(resourceRoot);
		std::cout << "Editor hierarchy persistence test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(resourceRoot);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
