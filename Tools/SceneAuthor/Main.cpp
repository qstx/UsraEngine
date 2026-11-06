#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

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
} // namespace

// Data-driven scene authoring uses exactly the Editor's SceneDraft save and
// validation path. It has no knowledge of a particular project or script type.
int main(int count, char** values)
{
	try
	{
		if (count != 3)
		{
			throw std::runtime_error("Usage: UrsaSceneAuthor <Assets directory> <manifest.json>");
		}
		const auto assets = std::filesystem::weakly_canonical(std::filesystem::u8path(values[1]));
		if (!std::filesystem::is_directory(assets))
		{
			throw std::runtime_error("Assets directory does not exist");
		}
		std::ifstream input(std::filesystem::u8path(values[2]));
		nlohmann::json manifest;
		input >> manifest;
		if (manifest.value("format", "") != "UrsaSceneAuthoring" || manifest.value("version", 0) != 1)
		{
			throw std::runtime_error("Unsupported scene authoring manifest");
		}
		VirtualFileSystem files;
		Done(files.MountProvider("prj", Take(DirectoryFileProvider::Open(assets)), MountAccess::ReadWrite));
		auto resources = Take(ResourceSession::Open(files));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		for (const auto& entry : manifest.at("scenes"))
		{
			const auto path = Take(VirtualPath::Parse(ToUrsaString(entry.at("path").get<std::string>())));
			if (path.scheme != "prj" || path.relative.size() < 11 ||
			    path.relative.substr(path.relative.size() - 10) != ".ursascene")
			{
				throw std::runtime_error("Authoring output must be a project Scene path");
			}
			const auto guid = Take(ResourceId::Parse(ToUrsaString(entry.at("guid").get<std::string>())));
			auto draft = Take(Editor::SceneDraft::Create(ResourceKind::Scene, registry, *resources));
			auto& scene = draft->GetScene();
			for (const auto& item : entry.at("objects"))
			{
				auto object = Take(scene.CreateGameObject(ToUrsaString(item.at("name").get<std::string>())));
				if (item.value("camera", false))
				{
					Done(scene.SetActiveCamera(Take(scene.AddComponent<CameraComponent>(object))));
				}
				for (const auto& script : item.value("behaviours", nlohmann::json::array()))
				{
					const auto id = Take(scene.AddComponent<Scripting::ScriptBehaviourComponent>(
					    object, ToUrsaString(script.at("type").get<std::string>())));
					static_cast<Scripting::ScriptBehaviourComponent*>(scene.GetComponent(id))
					    ->SetSerializedPayload(ToUrsaString(script.at("payload").dump()));
				}
			}
			if (Take(files.Exists(path)))
			{
				throw std::runtime_error("Refusing to overwrite an existing Scene");
			}
			Done(draft->Save(files, path, guid));
			std::cout << "Editor SceneDraft saved and validated " << path.ToString().c_str() << '\n';
		}
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
