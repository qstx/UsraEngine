#include <Application/EditorApplication.hpp>
#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
using Json = nlohmann::json;

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

VirtualPath Path(const char* path)
{
	return Take(VirtualPath::Parse(path));
}

const char* gameSource = R"(using System;
using Ursa;
namespace DebugAcceptance;
public sealed class DebugProbe : Behaviour
{
    public const int CodeVersion = 1;
    public int value = 9;
    public override void Awake()
    {
        int runtimeMarker = value + CodeVersion;
        GameObject.Name = "Debug:" + runtimeMarker;
        GC.KeepAlive(runtimeMarker);
    }
}
)";

const char* editorSource = R"(using System;
using Ursa.Editor;
namespace DebugAcceptance;
public static class EditorProbe
{
    [MenuItem("Tools/IDE Acceptance")]
    private static void Pulse()
    {
        int editorMarker = DebugProbe.CodeVersion + 40;
        Console.WriteLine("Editor marker:" + editorMarker);
        GC.KeepAlive(editorMarker);
    }
}
)";

class DebugAcceptanceEditor final : public EditorApplication
{
protected:
	Result<void> OnStart() override
	{
		try
		{
			auto document = Take(NewSceneDocument(Path("prj:Debug.ursascene"), ResourceKind::Scene,
			                                      Project().components, Documents()->Session()));
			draft = document->Payload<SceneDraft>();
			owner = Take(draft->GetScene().CreateGameObject("Debug edit owner"));
			auto camera = Take(owner->AddComponent<CameraComponent>());
			Done(draft->GetScene().SetActiveCamera(camera->Id()));
			document->MarkDirty();
			Done(Documents()->RequestOpen(std::move(document)));
			deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
			return {};
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(ToUrsaString(error.what()));
		}
	}

	Result<void> OnGui() override
	{
		auto drawn = EditorApplication::OnGui();
		if (!drawn)
		{
			return drawn;
		}
		try
		{
			if (std::chrono::steady_clock::now() > deadline)
			{
				return Result<void>::Failure("Managed debugger acceptance timed out");
			}
			if (!script && !ScriptWorkPending())
			{
				const auto* descriptor = Project().componentEditors.FindType("DebugAcceptance.DebugProbe, Ursa.Game");
				if (descriptor)
				{
					ComponentInspectorContext context;
					context.object = owner;
					context.scene = &draft->GetScene();
					context.roots = draft->GetScene().RootGameObjects();
					script = static_cast<Scripting::ScriptBehaviourComponent*>(Take(descriptor->create(context)));
				}
			}
			auto& files = Workspace()->Index().Filesystem();
			auto commandText = files.ReadText(Path("lib:IdeAcceptance/Command.json"));
			if (script && !ScriptWorkPending() && commandText)
			{
				const auto command = Json::parse(ToStdString(commandText.value));
				const UInt64 sequence = command.at("Sequence").get<UInt64>();
				if (sequence > lastCommand)
				{
					const auto action = command.at("Action").get<std::string>();
					if (action == "play" && !RunningScene())
					{
						RequestPlayToggle();
					}
					else if (action == "stop" && RunningScene())
					{
						RequestPlayToggle();
					}
					else if (action == "compile")
					{
						Done(CompileScripts());
					}
					else if (action == "menu")
					{
						ManagedExtensionDescriptor menu;
						menu.id = "menu:Tools/IDE Acceptance";
						menu.path = "Tools/IDE Acceptance";
						Take(InvokeEditorTool(menu));
					}
					else if (action == "close")
					{
						window->RequestClose();
					}
					else if (action != "play" && action != "stop")
					{
						throw std::runtime_error("Unknown debugger acceptance command");
					}
					lastCommand = sequence;
				}
			}
			String runtimeName;
			if (RunningScene())
			{
				for (const auto& root : RunningScene()->RootGameObjects())
				{
					if (root->Id() == owner->Id())
					{
						runtimeName = Take(root->Name());
					}
				}
			}
			Json state = {{"Pid", ExternalScriptEditor::ProcessId()},
			              {"Ready", script != nullptr},
			              {"Busy", ScriptWorkPending()},
			              {"Running", RunningScene() != nullptr},
			              {"RunningName", ToStdString(runtimeName)},
			              {"LastCommand", lastCommand},
			              {"ScriptStatus", ToStdString(ScriptStatus())},
			              {"DebugInfo", ToStdString(ScriptDebugInfo())},
			              {"Dirty", Documents()->Current()->Dirty()}};
			if (script)
			{
				state["Payload"] = Json::parse(ToStdString(script->SerializedPayload()));
			}
			const auto text = state.dump(2);
			if (text != previousState)
			{
				Done(files.WriteAtomic(Path("lib:IdeAcceptance/State.json"),
				                       Vector<UInt8>(text.data(), text.data() + text.size())));
				previousState = text;
			}
			return {};
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(ToUrsaString(error.what()));
		}
	}

private:
	std::shared_ptr<SceneDraft> draft;
	GameObject::Ptr owner;
	Scripting::ScriptBehaviourComponent* script = nullptr;
	UInt64 lastCommand = 0;
	std::string previousState;
	std::chrono::steady_clock::time_point deadline;
};
} // namespace

int main(int argumentCount, char** arguments)
{
	try
	{
		if (argumentCount != 2)
		{
			throw std::runtime_error("Usage: UrsaManagedDebugAcceptance <new-project-directory>");
		}
		const auto root =
		    std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::u8path(arguments[1])));
		const auto allowed = std::filesystem::weakly_canonical(std::filesystem::u8path(URSA_IDE_ACCEPTANCE_ROOT));
		const auto relative = root.lexically_relative(allowed);
		if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." || std::filesystem::exists(root))
		{
			throw std::runtime_error(
			    "Acceptance project must be a new child of this Engine build's IdeAcceptance directory");
		}
		std::filesystem::create_directories(root / "Assets/Editor");
		std::ofstream(root / "Assets/DebugProbe.cs") << gameSource;
		std::ofstream(root / "Assets/Editor/EditorProbe.cs") << editorSource;
		DebugAcceptanceEditor editor;
		Done(editor.OpenProject(root));
		Done(editor.Run(1280, 720, "Ursa Managed Debug Acceptance", false));
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
