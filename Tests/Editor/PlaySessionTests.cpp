#include <Core/Types.hpp>
#include <Application/PlaySession.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

Vector<String> events;

struct TestService
{
	int value = 0;
};

class TestComponent final : public Component
{
public:
	TestComponent(TestService& runtimeService, int initialValue) : service(runtimeService), value(initialValue)
	{
	}

	Result<void> Start(const FrameContext&) override
	{
		events.push_back("start");
		return {};
	}

	Result<void> Update(const FrameContext&) override
	{
		events.push_back("update");
		if (service.value < 0)
		{
			return Result<void>::Failure("Simulated play failure");
		}
		++value;
		++service.value;
		return {};
	}

	Result<void> FixedUpdate(const FrameContext&) override
	{
		events.push_back("fixed");
		return {};
	}

	Result<void> LateUpdate(const FrameContext&) override
	{
		events.push_back("late");
		return {};
	}

	void OnRemove() override
	{
		events.push_back("remove");
		++service.value;
	}

	int Value() const
	{
		return value;
	}

private:
	TestService& service;
	int value;
};

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
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
	const auto root =
	    std::filesystem::current_path() / ("play_session_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::create_directories(root / "Assets");
		auto workspace = Take(Editor::ProjectWorkspace::Open(root));
		auto documents = Take(Editor::EditorDocuments::Create(*workspace));
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		ComponentCodec codec;
		codec.type = "test.PlayComponent";
		codec.matches = [](const Component& component)
		{ return dynamic_cast<const TestComponent*>(&component) != nullptr; };
		codec.save = [](const Component& component, const ComponentSaveContext&)
		{ return Result<nlohmann::json>::Success({{"value", static_cast<const TestComponent&>(component).Value()}}); };
		codec.create = [](Scene& scene, GameObject::Ptr node, const nlohmann::json& data, ComponentLoadContext& context)
		{
			auto service = context.Service<TestService>();
			if (!service)
			{
				return Result<ComponentId>::Failure(service.error);
			}
			return scene.AddComponent<TestComponent>(node, *service.value, data.at("value").get<int>());
		};
		done(registry.RegisterCodec(std::move(codec)));
		TestService editService;
		auto document = Take(Editor::NewSceneDocument(Take(VirtualPath::Parse("prj:play.ursascene")),
		                                              ResourceKind::Scene, registry, documents->Session(),
		                                              [&editService](ComponentLoadContext& context)
		                                              {
			                                              context.Provide(editService);
			                                              return Result<void>{};
		                                              }));
		auto draft = document->Payload<Editor::SceneDraft>();
		const auto rootNode = draft->GetScene().RootGameObjects().front();
		Take(draft->GetScene().AddComponent<TestComponent>(rootNode, editService, 12));
		const auto targetNode = Take(draft->GetScene().CreateGameObject(rootNode));
		const ComponentId targetCamera = Take(draft->GetScene().AddComponent<CameraComponent>(rootNode));
		const ComponentId scriptId = Take(draft->GetScene().AddComponent<Scripting::ScriptBehaviourComponent>(
		    rootNode, String("Demo.MoveWithInput")));
		auto* script = static_cast<Scripting::ScriptBehaviourComponent*>(draft->GetScene().GetComponent(scriptId));
		nlohmann::json scriptPayload = nlohmann::json::object();
		scriptPayload["FormatVersion"] = 1u;
		scriptPayload["ScriptType"] = "demo.move-with-input";
		scriptPayload["Version"] = 1u;
		scriptPayload["Fields"] = nlohmann::json::object();
		scriptPayload["Fields"]["targetObject"]["$ursaReference"]["Kind"] = "GameObject";
		scriptPayload["Fields"]["targetObject"]["$ursaReference"]["Id"] =
		    ToStdString(Take(draft->GetScene().PersistentId(targetNode)).ToString());
		scriptPayload["Fields"]["targetCamera"]["$ursaReference"]["Kind"] = "Component";
		scriptPayload["Fields"]["targetCamera"]["$ursaReference"]["Id"] =
		    ToStdString(Take(draft->GetScene().ComponentPersistentId(targetCamera)).ToString());
		script->SetSerializedPayload(ToUrsaString(scriptPayload.dump()));
		document->MarkDirty();
		const auto documentId = document->Id();
		done(documents->RequestOpen(std::move(document)));
		Editor::PlaySession play;
		FrameInput pressed;
		pressed.keys[static_cast<Ursa::Size>(Key::Space)] = true;
		Expect(!Editor::SelectPlayInput(pressed, false, false).KeyDown(Key::Space) &&
		           !Editor::SelectPlayInput(pressed, true, true).KeyDown(Key::Space) &&
		           Editor::SelectPlayInput(pressed, true, false).KeyDown(Key::Space),
		       "Editor keyboard input leaked to or was blocked from the game viewport");
		auto missing = play.Start(*draft, documentId, registry, documents->Session());
		Expect(!missing && !play.Active() && documents->Current()->Dirty(),
		       "Missing runtime service changed Edit mode");
		auto service = std::make_shared<TestService>();
		done(play.Start(*draft, documentId, registry, documents->Session(),
		                [service](ComponentLoadContext& context) -> Result<std::shared_ptr<void>>
		                {
			                context.Provide(*service);
			                return Result<std::shared_ptr<void>>::Success(service);
		                }));
		auto* playedScript = play.GetScene()->FindComponent<Scripting::ScriptBehaviourComponent>(
		    play.GetScene()->RootGameObjects().front());
		Expect(playedScript && playedScript->HasSerializedPayload(), "Run clone lost its managed script payload");
		const nlohmann::json playedPayload = nlohmann::json::parse(ToStdString(playedScript->SerializedPayload()));
		Expect(playedPayload.at("Fields").at("targetObject").at("$ursaReference").at("Id") ==
		               ToStdString(Take(play.GetScene()->PersistentId(
		                                    play.GetScene()->RootGameObjects().front()->Children().front()))
		                               .ToString()) &&
		           playedPayload.at("Fields").at("targetCamera").at("$ursaReference").at("Id") ==
		               ToStdString(
		                   Take(play.GetScene()->ComponentPersistentId(
		                            play.GetScene()->ComponentsOf(play.GetScene()->RootGameObjects().front()).value[1]))
		                       .ToString()),
		       "Run clone did not retain object/Component references in its independent Scene graph");
		documents->SetSaveBlocked(true);
		Expect(!documents->SaveCurrent(), "Play mode allowed saving");
		events.clear();
		done(play.Tick(1.f / 30.f, nullptr, {}));
		Expect(events.size() == 5 && events[0] == "start" && events[1] == "fixed" && events[2] == "fixed" &&
		           events[3] == "update" && events[4] == "late",
		       "Play frame lifecycle order changed");
		Expect(service->value == 1 && draft->GetScene().FindComponent<TestComponent>(rootNode)->Value() == 12,
		       "Play mutation reached the edit scene");
		play.Stop();
		documents->SetSaveBlocked(false);
		Expect(events.back() == "remove" && !play.Active() && documents->Current()->Dirty() &&
		           !std::filesystem::exists(root / "Assets" / "play.ursascene"),
		       "Stop did not restore the edit document state");
		auto failingService = std::make_shared<TestService>();
		failingService->value = -1;
		done(play.Start(*draft, documentId, registry, documents->Session(),
		                [failingService](ComponentLoadContext& context) -> Result<std::shared_ptr<void>>
		                {
			                context.Provide(*failingService);
			                return Result<std::shared_ptr<void>>::Success(failingService);
		                }));
		Expect(!play.Tick(1.f / 60.f, nullptr, {}), "Simulated play failure was not reported");
		play.Stop();
		Expect(documents->Current()->Dirty() && draft->GetScene().FindComponent<TestComponent>(rootNode)->Value() == 12,
		       "Failed play changed the pre-Run edit state");
		for (int run = 0; run < 2; ++run)
		{
			std::weak_ptr<TestService> runtimeService;
			done(play.Start(*draft, documentId, registry, documents->Session(),
			                [&runtimeService](ComponentLoadContext& context) -> Result<std::shared_ptr<void>>
			                {
				                auto ownedService = std::make_shared<TestService>();
				                runtimeService = ownedService;
				                context.Provide(*ownedService);
				                return Result<std::shared_ptr<void>>::Success(std::move(ownedService));
			                }));
			Expect(!runtimeService.expired(), "Play session released its runtime service during Start");
			done(play.Tick(1.f / 60.f, nullptr, {}));
			Expect(runtimeService.lock()->value == 1, "Play update did not use its owned runtime service");
			play.Stop();
			Expect(runtimeService.expired() && !play.Active(), "Stop retained the runtime service or scene");
		}
		done(documents->SaveCurrent());
		std::filesystem::remove_all(root);
		std::cout << "Editor Play session test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
