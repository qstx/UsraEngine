#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Serialization/SceneResource.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

struct RuntimeService
{
	int updates = 0;
};

struct CounterComponent : Ursa::Component
{
	CounterComponent(int& total, int step) : total(total), step(step)
	{
	}

	int& total;
	int step;
};

Ursa::Result<void> RegisterCounter(Ursa::ComponentRegistry& registry)
{
	Ursa::ComponentCodec codec;
	codec.type = "game.Counter";
	codec.version = 1;
	codec.matches = [](const Ursa::Component& value)
	{ return dynamic_cast<const CounterComponent*>(&value) != nullptr; };
	codec.save = [](const Ursa::Component& value, const Ursa::ComponentSaveContext&)
	{ return Ursa::Result<nlohmann::json>::Success({{"step", static_cast<const CounterComponent&>(value).step}}); };
	codec.create =
	    [](Ursa::Scene& scene, Ursa::NodeId node, const nlohmann::json& data, Ursa::ComponentLoadContext& context)
	{
		if (!data.is_object() || !data.contains("step") || !data["step"].is_number_integer())
		{
			return Ursa::Result<Ursa::ComponentId>::Failure("Invalid Counter step");
		}
		auto total = context.Service<int>();
		if (!total)
		{
			return Ursa::Result<Ursa::ComponentId>::Failure(total.error);
		}
		return scene.AddComponent<CounterComponent>(node, *total.value, data["step"].get<int>());
	};
	return registry.RegisterCodec(std::move(codec));
}

class LinkComponent final : public Ursa::Component
{
public:
	LinkComponent(RuntimeService& service, int value) : service(service), value(value)
	{
	}

	Ursa::Result<void> OnUpdate(const Ursa::FrameContext&) override
	{
		++service.updates;
		return {};
	}

	Ursa::NodeId targetNode;
	Ursa::ComponentId targetComponent;
	int value = 0;

private:
	RuntimeService& service;
};

Ursa::ComponentCodec MakeCodec()
{
	Ursa::ComponentCodec codec;
	codec.type = "test.Link";
	codec.version = 2;
	codec.matches = [](const Ursa::Component& component)
	{ return dynamic_cast<const LinkComponent*>(&component) != nullptr; };
	codec.save = [](const Ursa::Component& component, const Ursa::ComponentSaveContext& context)
	{
		const auto& link = static_cast<const LinkComponent&>(component);
		nlohmann::json data{{"value", link.value}, {"node", nullptr}, {"component", nullptr}};
		if (link.targetNode)
		{
			data["node"] = Ursa::ToStdString(Take(context.GetNodeId(link.targetNode)).ToString());
		}
		if (link.targetComponent)
		{
			data["component"] = Ursa::ToStdString(Take(context.GetComponentId(link.targetComponent)).ToString());
		}
		return Ursa::Result<nlohmann::json>::Success(std::move(data));
	};
	codec.create =
	    [](Ursa::Scene& scene, Ursa::NodeId node, const nlohmann::json& data, Ursa::ComponentLoadContext& context)
	{
		if (!data.is_object() || !data.contains("value") || !data["value"].is_number_integer())
		{
			return Ursa::Result<Ursa::ComponentId>::Failure("Invalid link value");
		}
		auto service = context.Service<RuntimeService>();
		if (!service)
		{
			return Ursa::Result<Ursa::ComponentId>::Failure(service.error);
		}
		return scene.AddComponent<LinkComponent>(node, *service.value, data["value"].get<int>());
	};
	codec.bind = [](Ursa::Scene& scene, Ursa::ComponentId component, const nlohmann::json& data,
	                Ursa::ComponentLoadContext& context) -> Ursa::Result<void>
	{
		auto& link = *static_cast<LinkComponent*>(scene.GetComponent(component));
		for (const char* field : {"node", "component"})
		{
			if (!data.contains(field) || (!data[field].is_null() && !data[field].is_string()))
			{
				return Ursa::Result<void>::Failure(Ursa::String("Invalid link ") + field);
			}
			if (data[field].is_null())
			{
				continue;
			}
			auto id = Ursa::ResourceId::Parse(Ursa::ToUrsaString(data[field].get<std::string>()));
			if (!id)
			{
				return Ursa::Result<void>::Failure(id.error);
			}
			if (Ursa::String(field) == "node")
			{
				auto target = context.Node(id.value);
				if (!target)
				{
					return Ursa::Result<void>::Failure(target.error);
				}
				link.targetNode = target.value;
			}
			else
			{
				auto target = context.Component(id.value);
				if (!target)
				{
					return Ursa::Result<void>::Failure(target.error);
				}
				link.targetComponent = target.value;
			}
		}
		return {};
	};
	return codec;
}

void TestCustomComponents(const std::filesystem::path& root)
{
	RuntimeService service;
	Ursa::ComponentRegistry registry;
	done(registry.RegisterCodec(MakeCodec()));
	done(RegisterCounter(registry));
	auto duplicate = registry.RegisterCodec(MakeCodec());
	Expect(!duplicate && duplicate.error.find("Duplicate") != Ursa::String::npos,
	       "Duplicate component type was accepted");
	Ursa::Scene source;
	const auto first = Take(source.CreateNode());
	const auto second = Take(source.CreateNode());
	const auto target = Take(source.AddComponent<LinkComponent>(second, service, 7));
	const auto linkId = Take(source.AddComponent<LinkComponent>(first, service, 42));
	auto* link = static_cast<LinkComponent*>(source.GetComponent(linkId));
	link->targetNode = second;
	link->targetComponent = target;
	const auto firstPersistent = Take(source.NodePersistentId(first));
	const auto secondPersistent = Take(source.NodePersistentId(second));
	const auto linkPersistent = Take(source.ComponentPersistentId(linkId));
	const auto targetPersistent = Take(source.ComponentPersistentId(target));
	const auto path = root / "custom.ursascene";
	done(Ursa::SaveSceneResource(path, Ursa::ResourceId::Generate(), source, registry));

	auto session = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext context(*session);
	context.Provide(service);
	auto loaded = Take(Ursa::LoadSceneResource(path, registry, context));
	auto restoredNode = Take(loaded.scene->NodeByPersistentId(firstPersistent));
	auto restoredTargetNode = Take(loaded.scene->NodeByPersistentId(secondPersistent));
	auto* restored = static_cast<LinkComponent*>(
	    loaded.scene->GetComponent(Take(loaded.scene->ComponentByPersistentId(linkPersistent))));
	Expect(restored && restored != link && restored->value == 42 && restored->Node() == restoredNode &&
	           restored->targetNode == restoredTargetNode &&
	           restored->targetComponent == Take(loaded.scene->ComponentByPersistentId(targetPersistent)),
	       "Custom fields or cross-object references were not restored");
	loaded.scene->BeginFrame();
	done(loaded.scene->Update({}));
	Expect(service.updates == 2, "Runtime service was not rebound to loaded components");

	Ursa::ComponentRegistry emptyRegistry;
	Ursa::ComponentLoadContext missingTypeContext(*session);
	auto missingType = Ursa::LoadSceneResource(path, emptyRegistry, missingTypeContext);
	Expect(!missingType && missingType.error.find(Ursa::PathToUrsaString(path)) != Ursa::String::npos &&
	           missingType.error.find(firstPersistent.ToString()) != Ursa::String::npos &&
	           missingType.error.find("test.Link") != Ursa::String::npos,
	       "Unregistered type error lacks file, node, or type");
	Ursa::ComponentLoadContext missingServiceContext(*session);
	auto missingService = Ursa::LoadSceneResource(path, registry, missingServiceContext);
	Expect(!missingService && missingService.error.find("Missing runtime service") != Ursa::String::npos,
	       "Missing runtime service was accepted");
	nlohmann::json document;
	{
		std::ifstream input(path);
		input >> document;
	}
	Expect(document.dump().find("service_") == Ursa::String::npos &&
	           document.dump().find(Ursa::ToStdString(secondPersistent.ToString())) != Ursa::String::npos,
	       "Scene stored a process dependency or omitted persistent references");
	document["nodes"][0]["components"][0]["version"] = 99;
	const auto unsupported = root / "unsupported.ursascene";
	{
		std::ofstream output(unsupported);
		output << document;
	}
	Ursa::ComponentLoadContext versionContext(*session);
	versionContext.Provide(service);
	auto badVersion = Ursa::LoadSceneResource(unsupported, registry, versionContext);
	Expect(!badVersion && badVersion.error.find(Ursa::PathToUrsaString(unsupported)) != Ursa::String::npos &&
	           badVersion.error.find(firstPersistent.ToString()) != Ursa::String::npos &&
	           badVersion.error.find("version") != Ursa::String::npos,
	       "Unsupported version error lacks file or node");
	std::filesystem::remove(path);
	std::filesystem::remove(unsupported);
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("UrsaCustomComponentTests-" + Ursa::ToStdString(Ursa::ResourceId::Generate().ToString()));
		std::filesystem::create_directory(root);
		TestCustomComponents(root);
		std::filesystem::remove(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
