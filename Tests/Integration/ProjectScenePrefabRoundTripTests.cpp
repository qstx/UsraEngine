#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
using namespace Ursa;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

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

Vector<UInt8> ReadFile(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		throw std::runtime_error("Could not open project test asset: " + ToStdString(PathToUrsaString(path)));
	}
	const std::string data(std::istreambuf_iterator<char>(input), {});
	Vector<UInt8> bytes;
	bytes.reserve(data.size());
	for (unsigned char byte : data)
	{
		bytes.push_back(byte);
	}
	return bytes;
}

void SeedMemoryFileSystem(VirtualFileSystem& filesystem, const std::filesystem::path& assets)
{
	auto provider = std::make_shared<MemoryFileProvider>();
	for (const String& relative : {String("Snake/Snake.ursascene"), String("Snake/Snake.ursascene.meta"),
	                               String("Snake/Board.ursaprefab"), String("Snake/Board.ursaprefab.meta")})
	{
		Done(provider->WriteAtomic(relative, ReadFile(assets / std::filesystem::u8path(ToStdString(relative))), false));
	}
	Done(filesystem.MountProvider("prj", provider, MountAccess::ReadWrite));
}

const Scripting::ScriptBehaviourComponent& ScriptComponent(GameObject::Ptr object, const String& typeName)
{
	const auto* behavior = object->FindComponent<Scripting::ScriptBehaviourComponent>();
	Expect(behavior && behavior->TypeName() == typeName, "Serialized C# component identity changed");
	return *behavior;
}

void ValidateController(const Scripting::ScriptBehaviourComponent& behavior)
{
	const auto payload = nlohmann::json::parse(ToStdString(behavior.SerializedPayload()));
	Expect(payload.value("ScriptType", std::string{}) == "demo.snake-controller" &&
	           payload.value("SourceGuid", std::string{}) == "7f6ce254-214b-45bb-afa8-e3bb99f58c88" &&
	           payload.value("LocalFileId", UInt64{}) == 4788986422269556870ull &&
	           payload["Fields"].value("width", 0) == 16 && payload["Fields"].value("height", 0) == 16 &&
	           payload["Fields"].value("seed", 0u) == 1u && payload["Fields"]["segmentObjects"].size() == 256 &&
	           payload["Fields"]["foodObject"].contains("$ursaReference"),
	       "C# Snake controller payload changed during scene serialization");
}

void ValidateBoard(GameObject::Ptr board)
{
	Expect(board->Components().empty() && board->Children().size() == 257,
	       "Board must contain its base and all 256 tiles before gameplay");
	for (const auto& child : board->Children())
	{
		Expect(child->FindComponent<MeshFilterComponent>() && child->FindComponent<MeshRendererComponent>() &&
		           !child->FindComponent<Scripting::ScriptBehaviourComponent>(),
		       "Board geometry must be authored rather than created by a script");
	}
}

void ValidatePresentation(Scene& scene)
{
	auto root = scene.RootGameObjects().front();
	const auto payload = nlohmann::json::parse(
	    ToStdString(ScriptComponent(root, "Ursa.Demo.SnakeController, Ursa.Game").SerializedPayload()));
	const auto& fields = payload["Fields"];
	Size visible = 0;
	Set<String> identities;
	for (const auto& value : fields["segmentObjects"])
	{
		const auto id = Take(ResourceId::Parse(ToUrsaString(value["$ursaReference"]["Id"].get<std::string>())));
		auto segment = Take(scene.GameObjectById(id));
		Expect(identities.insert(id.ToString()).second && segment->FindComponent<MeshRendererComponent>(),
		       "Snake segment references must resolve to distinct authored mesh objects");
		visible += Take(segment->ActiveSelf()) ? 1 : 0;
	}
	Expect(visible == 3, "Edit mode must show the three initial Snake segments");
	const auto foodId =
	    Take(ResourceId::Parse(ToUrsaString(fields["foodObject"]["$ursaReference"]["Id"].get<std::string>())));
	auto food = Take(scene.GameObjectById(foodId));
	Expect(Take(food->ActiveSelf()) && food->FindComponent<MeshRendererComponent>(),
	       "Edit mode must show the authored food object");
}
} // namespace

int main(int count, char** arguments)
{
	using namespace Ursa;
	try
	{
		auto filesystem = Take(OpenEngineFileSystem(Take(ResolveEngineSourceRoot())));
		if (count == 3 && String(arguments[1]) == "--save-assets")
		{
			Done(filesystem.MountProvider("prj",
			                              Take(DirectoryFileProvider::Open(std::filesystem::u8path(arguments[2]))),
			                              MountAccess::ReadWrite));
		}
		else
		{
			Expect(count == 1, "Expected no arguments or --save-assets <assets directory>");
			SeedMemoryFileSystem(filesystem, std::filesystem::u8path(URSA_PROJECT_ASSETS_DIR));
		}
		auto resources = Take(ResourceSession::Open(filesystem));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		const VirtualPath scenePath = Take(VirtualPath::Parse("prj:Snake/Snake.ursascene"));
		const VirtualPath prefabPath = Take(VirtualPath::Parse("prj:Snake/Board.ursaprefab"));

		ComponentLoadContext sceneContext(*resources);
		auto linked = Take(LoadLinkedSceneResource(filesystem, scenePath, registry, sceneContext));
		const ResourceId sceneId = linked.id;
		auto root = linked.scene->RootGameObjects().front();
		Expect(Take(root->Tag()) == "Untagged" && !Take(root->IsStatic()),
		       "Legacy Scene did not default missing GameObject Tag and Static fields");
		Expect(!root->SetTag("") && Take(root->Tag()) == "Untagged",
		       "An invalid GameObject Tag was accepted or replaced the prior value");
		Done(root->SetTag("SceneRoundTrip"));
		Done(root->SetStatic(true));
		Expect(root->Id().ToString() == "75727361-0000-0000-0100-000000000001", "Snake Scene root ID changed");
		ValidateController(ScriptComponent(root, "Ursa.Demo.SnakeController, Ursa.Game"));
		Expect(linked.document.value("instances", nlohmann::json::array()).size() == 1,
		       "Snake Scene lost its Board Prefab reference");
		ValidatePresentation(*linked.scene);
		const ResourceId prefabInstance = Take(ResourceId::Parse(
		    ToUrsaString(linked.document["instances"][0]["id"].get<std::string>())));
		const auto instanceScope = linked.scopedIds.find(prefabInstance.ToString());
		Expect(instanceScope != linked.scopedIds.end() && !instanceScope->second.empty(),
		       "Loaded Board Prefab instance has no source-to-instance object map");
		String overriddenSourceObject;
		Float32 savedPrefabPositionX = 0.f;
		for (const auto& [sourceObjectText, instanceObjectText] : instanceScope->second)
		{
			auto instanceObjectId = ResourceId::Parse(instanceObjectText);
			if (!instanceObjectId)
			{
				continue;
			}
			auto instanceObject = linked.scene->GameObjectById(instanceObjectId.value);
			if (!instanceObject)
			{
				continue;
			}
			auto localTransform = linked.scene->GetLocalTransform(instanceObject.value);
			if (!localTransform)
			{
				continue;
			}
			localTransform.value.position.x += 0.125f;
			Done(linked.scene->SetLocalTransform(instanceObject.value, localTransform.value));
			overriddenSourceObject = sourceObjectText;
			savedPrefabPositionX = localTransform.value.position.x;
			break;
		}
		Expect(!overriddenSourceObject.empty(), "Could not edit an instantiated Board Prefab object");
		Done(SaveLinkedSceneResource(filesystem, scenePath, linked, registry));
		const auto& positionOverride =
		    linked.document["instances"][0]["overrides"]["properties"][ToStdString(overriddenSourceObject)]
		                   ["/localTransform/position/0"];
		Expect(positionOverride.is_number() &&
		           std::abs(positionOverride.get<Float32>() - savedPrefabPositionX) < 0.00001f,
		       "Saving an edited Prefab instance did not persist its local-position delta");
		linked.scene->Clear();

		ComponentLoadContext prefabContext(*resources);
		auto prefab = Take(LoadPrefabResource(filesystem, prefabPath, registry, prefabContext));
		Expect(prefab.id.ToString() == "75727361-0000-0000-0300-000000000001",
		       "Snake Board Prefab resource ID changed");
		auto board = prefab.scene->RootGameObjects().front();
		Expect(Take(board->Tag()) == "Untagged" && !Take(board->IsStatic()),
		       "Legacy Prefab did not default missing GameObject Tag and Static fields");
		Done(board->SetTag("PrefabRoundTrip"));
		Done(board->SetStatic(true));
		ValidateBoard(board);
		Done(SavePrefabResource(filesystem, prefabPath, prefab.id, *prefab.scene, board, registry));
		prefab.scene->Clear();

		ComponentLoadContext reloadedSceneContext(*resources);
		auto reloadedScene = Take(LoadLinkedSceneResource(filesystem, scenePath, registry, reloadedSceneContext));
		Expect(reloadedScene.id == sceneId && reloadedScene.scene->RootGameObjects().front()->Id().ToString() ==
		                                          "75727361-0000-0000-0100-000000000001",
		       "Snake Scene IDs changed after saving and reopening");
		auto reloadedRoot = reloadedScene.scene->RootGameObjects().front();
		Expect(Take(reloadedRoot->Tag()) == "SceneRoundTrip" && Take(reloadedRoot->IsStatic()),
		       "GameObject Tag or Static state changed after saving and reopening a Scene");
		ValidateController(
		    ScriptComponent(reloadedScene.scene->RootGameObjects().front(), "Ursa.Demo.SnakeController, Ursa.Game"));
		ValidatePresentation(*reloadedScene.scene);
		const auto reloadedScope = reloadedScene.scopedIds.find(prefabInstance.ToString());
		Expect(reloadedScope != reloadedScene.scopedIds.end(), "Reopened Scene lost its Prefab instance scope");
		const auto reloadedObjectMapping = reloadedScope->second.find(overriddenSourceObject);
		Expect(reloadedObjectMapping != reloadedScope->second.end(),
		       "Reopened Scene lost the Prefab source-to-instance object mapping");
		const ResourceId reloadedInstanceObjectId = Take(ResourceId::Parse(reloadedObjectMapping->second));
		auto reloadedInstanceObject = Take(reloadedScene.scene->GameObjectById(reloadedInstanceObjectId));
		const LocalTransform reloadedInstanceTransform =
		    Take(reloadedScene.scene->GetLocalTransform(reloadedInstanceObject));
		Expect(std::abs(reloadedInstanceTransform.position.x - savedPrefabPositionX) < 0.00001f,
		       "Reopened Scene did not reapply the saved Prefab instance position delta");
		reloadedScene.scene->Clear();

		ComponentLoadContext reloadedPrefabContext(*resources);
		auto reloadedPrefab = Take(LoadPrefabResource(filesystem, prefabPath, registry, reloadedPrefabContext));
		Expect(reloadedPrefab.id.ToString() == "75727361-0000-0000-0300-000000000001",
		       "Snake Prefab ID changed after saving and reopening");
		auto reloadedBoard = reloadedPrefab.scene->RootGameObjects().front();
		Expect(Take(reloadedBoard->Tag()) == "PrefabRoundTrip" && Take(reloadedBoard->IsStatic()),
		       "GameObject Tag or Static state changed after saving and reopening a Prefab");
		ValidateBoard(reloadedBoard);
		reloadedPrefab.scene->Clear();
		std::cout << "C# Snake Scene and Board Prefab round-trip passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
