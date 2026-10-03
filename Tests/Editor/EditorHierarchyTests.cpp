#include <Scene/EditorHierarchy.hpp>
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
	try
	{
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		Scene scene;
		const auto first = Take(scene.CreateNode());
		const auto child = Take(scene.CreateNode(first));
		const auto second = Take(scene.CreateNode());
		done(scene.ReorderNode(second, 0));
		done(scene.SetNodeLabel(first, "Root"));
		done(scene.SetNodeLabel(child, "Child"));
		done(scene.SetEnabled(child, false));
		Take(scene.AddComponent<BoxRenderComponent>(first));
		const auto firstId = Take(scene.NodePersistentId(first));
		const auto childId = Take(scene.NodePersistentId(child));
		const auto secondId = Take(scene.NodePersistentId(second));
		Editor::EditorHierarchy hierarchy;
		done(hierarchy.Select(scene, child));

		const auto path = std::filesystem::current_path() / "hierarchy.ursascene";
		const auto document = Take(EncodeSceneDocument(path, ResourceId::Generate(), scene, registry));
		const auto resources = Take(ResourceSession::Open({std::filesystem::current_path()}));
		ComponentLoadContext context(*resources);
		auto loaded = Take(DecodeSceneDocument(path, document, registry, context));
		Expect(loaded.scene->RootNodes().size() == 2, "Root count changed after save/reopen");
		Expect(Take(loaded.scene->NodePersistentId(loaded.scene->RootNodes()[0])) == secondId,
		       "Root order changed after save/reopen");
		Expect(Take(loaded.scene->NodePersistentId(loaded.scene->RootNodes()[1])) == firstId,
		       "Root ID changed after save/reopen");
		const auto reopenedChild = Take(loaded.scene->NodeByPersistentId(childId));
		Expect(Take(loaded.scene->NodeLabel(reopenedChild)) == "Child", "Child label was not restored");
		Expect(!Take(loaded.scene->IsEnabled(reopenedChild)), "Child enabled state changed");
		Expect(Take(loaded.scene->NodePersistentId(Take(hierarchy.SelectedNode(*loaded.scene)))) == childId,
		       "Hierarchy selection did not survive scene replacement");

		auto legacy = document;
		for (auto& node : legacy["nodes"])
		{
			node.erase("label");
		}
		auto oldScene = Take(DecodeSceneDocument(path, legacy, registry, context));
		Expect(Take(oldScene.scene->NodeLabel(Take(oldScene.scene->NodeByPersistentId(childId)))).empty(),
		       "Old scene without labels did not load");
		const auto resaved = Take(EncodeSceneDocument(path, ResourceId::Generate(), *oldScene.scene, registry));
		Expect(resaved["nodes"][1].contains("label"), "Re-saved old scene is missing a label field");

		const auto prefabPath = std::filesystem::current_path() / "hierarchy.ursaprefab";
		done(SavePrefabResource(prefabPath, ResourceId::Generate(), scene, first, registry));
		auto prefab = Take(LoadPrefabResource(prefabPath, registry, context));
		Expect(Take(prefab.scene->NodeLabel(Take(prefab.scene->NodeByPersistentId(firstId)))) == "Root",
		       "Prefab root label was not restored");
		Expect(Take(prefab.scene->NodeLabel(Take(prefab.scene->NodeByPersistentId(childId)))) == "Child",
		       "Prefab child label was not restored");

		auto* imgui = ImGui::CreateContext();
		ImGui::SetCurrentContext(imgui);
		ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
		ImGui::GetIO().DisplaySize = ImVec2(640, 480);
		ImGui::NewFrame();
		const auto displayed = hierarchy.Draw(*loaded.scene, registry);
		Expect(displayed && !displayed.value, "Hierarchy display changed scene data unexpectedly");
		ImGui::Render();
		ImGui::DestroyContext(imgui);
		std::cout << "Editor hierarchy persistence test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
