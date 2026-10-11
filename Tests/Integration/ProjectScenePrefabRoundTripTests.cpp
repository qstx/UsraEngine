#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
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
	           payload.value("SourceGuid", std::string{}) == "9bf784f0-7489-4622-9ce5-334ae31c0fd0" &&
	           payload.value("LocalFileId", UInt64{}) == 4788986422269556870ull &&
	           payload["Fields"].value("width", 0) == 16 && payload["Fields"].value("height", 0) == 16 &&
	           payload["Fields"].value("seed", 0u) == 1u,
	       "C# Snake controller payload changed during scene serialization");
}

void ValidateBoard(const Scripting::ScriptBehaviourComponent& behavior)
{
	const auto payload = nlohmann::json::parse(ToStdString(behavior.SerializedPayload()));
	Expect(payload.value("ScriptType", std::string{}) == "demo.snake-board" &&
	           payload.value("SourceGuid", std::string{}) == "9bf784f0-7489-4622-9ce5-334ae31c0fd0" &&
	           payload.value("LocalFileId", UInt64{}) == 5986131520760667172ull &&
	           payload["Fields"].value("width", 0) == 16 && payload["Fields"].value("height", 0) == 16,
	       "C# Snake Board payload changed during Prefab serialization");
}
} // namespace

int main()
{
	using namespace Ursa;
	try
	{
		VirtualFileSystem filesystem;
		SeedMemoryFileSystem(filesystem, std::filesystem::u8path(URSA_PROJECT_ASSETS_DIR));
		auto resources = Take(ResourceSession::Open(filesystem));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		const VirtualPath scenePath = Take(VirtualPath::Parse("prj:Snake/Snake.ursascene"));
		const VirtualPath prefabPath = Take(VirtualPath::Parse("prj:Snake/Board.ursaprefab"));

		ComponentLoadContext sceneContext(*resources);
		auto linked = Take(LoadLinkedSceneResource(filesystem, scenePath, registry, sceneContext));
		const ResourceId sceneId = linked.id;
		auto root = linked.scene->RootGameObjects().front();
		Expect(root->Id().ToString() == "75727361-0000-0000-0100-000000000001", "Snake Scene root ID changed");
		ValidateController(ScriptComponent(root, "Ursa.Demo.SnakeController, Ursa.Game"));
		Expect(linked.document.value("instances", nlohmann::json::array()).size() == 1,
		       "Snake Scene lost its Board Prefab reference");
		Done(SaveLinkedSceneResource(filesystem, scenePath, linked, registry));
		linked.scene->Clear();

		ComponentLoadContext prefabContext(*resources);
		auto prefab = Take(LoadPrefabResource(filesystem, prefabPath, registry, prefabContext));
		Expect(prefab.id.ToString() == "75727361-0000-0000-0300-000000000001",
		       "Snake Board Prefab resource ID changed");
		ValidateBoard(
		    ScriptComponent(prefab.scene->RootGameObjects().front(), "Ursa.Demo.SnakeBoardBehaviour, Ursa.Game"));
		Done(SavePrefabResource(filesystem, prefabPath, prefab.id, *prefab.scene,
		                        prefab.scene->RootGameObjects().front(), registry));
		prefab.scene->Clear();

		ComponentLoadContext reloadedSceneContext(*resources);
		auto reloadedScene = Take(LoadLinkedSceneResource(filesystem, scenePath, registry, reloadedSceneContext));
		Expect(reloadedScene.id == sceneId && reloadedScene.scene->RootGameObjects().front()->Id().ToString() ==
		                                          "75727361-0000-0000-0100-000000000001",
		       "Snake Scene IDs changed after saving and reopening");
		ValidateController(
		    ScriptComponent(reloadedScene.scene->RootGameObjects().front(), "Ursa.Demo.SnakeController, Ursa.Game"));
		reloadedScene.scene->Clear();

		ComponentLoadContext reloadedPrefabContext(*resources);
		auto reloadedPrefab = Take(LoadPrefabResource(filesystem, prefabPath, registry, reloadedPrefabContext));
		Expect(reloadedPrefab.id.ToString() == "75727361-0000-0000-0300-000000000001",
		       "Snake Prefab ID changed after saving and reopening");
		ValidateBoard(ScriptComponent(reloadedPrefab.scene->RootGameObjects().front(),
		                              "Ursa.Demo.SnakeBoardBehaviour, Ursa.Game"));
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
