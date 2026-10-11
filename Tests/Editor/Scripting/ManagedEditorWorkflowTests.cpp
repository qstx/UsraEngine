#include <Application/EditorApplication.hpp>
#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/SerializedObject.hpp>
#include <chrono>
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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

const char* source =
    "using Ursa; namespace Workflow; [UrsaScriptType(\"workflow.probe\")] public sealed class Probe : Behaviour { "
    "private static int count; public int value = Defaults(); private static int Defaults() => 5; "
    "public override void Awake() { GameObject.Name = $\"Runtime:{++count}:{value}\"; } }\n";

class WorkflowEditor final : public EditorApplication
{
public:
	bool Verified() const
	{
		return completed && responsiveFrames >= 2;
	}

protected:
	Result<void> OnStart() override
	{
		auto path = Take(VirtualPath::Parse("prj:Workflow.ursascene"));
		auto document = Take(NewSceneDocument(path, ResourceKind::Scene, Project().components, Documents()->Session()));
		draft = document->Payload<SceneDraft>();
		owner = Take(draft->GetScene().CreateGameObject("Editor root"));
		document->MarkDirty();
		Done(Documents()->RequestOpen(std::move(document)));
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
		return {};
	}

	Result<void> OnGui() override
	{
		auto rendered = EditorApplication::OnGui();
		if (!rendered)
		{
			return rendered;
		}
		try
		{
			if (std::chrono::steady_clock::now() > deadline)
			{
				throw std::runtime_error("Editor workflow timed out at phase " + std::to_string(phase) +
				                         ", running=" + std::to_string(RunningScene() != nullptr) +
				                         ", assets=" + ToStdString(AssetStatus()) + ": " + ToStdString(ScriptStatus()));
			}
			if (ScriptWorkPending())
			{
				++responsiveFrames;
				return {};
			}
			if (phase == 0)
			{
				const auto* descriptor = Project().componentEditors.FindType("Workflow.Probe, Ursa.Game");
				if (!descriptor)
				{
					return {};
				}
				ComponentInspectorContext context;
				context.object = owner;
				context.scene = &draft->GetScene();
				context.roots = draft->GetScene().RootGameObjects();
				script = static_cast<Scripting::ScriptBehaviourComponent*>(Take(descriptor->create(context)));
				auto payload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
				Expect(payload["Fields"]["value"] == 5, "Add Component did not use the script constructor default");
				auto properties = Take(SerializedObject::Create(*script));
				Done(Take(properties->FindProperty("/Fields/value")).SetValue(19));
				Done(properties->Apply());
				WriteSource(String(source) + "public class Broken { invalid }\n");
				Done(CompileScripts());
				phase = 1;
			}
			else if (phase == 1)
			{
				Expect(!RunningScene() && ScriptStatus().find("prj:Probe.cs") != String::npos,
				       "Compilation diagnostics did not point to the source virtual path");
				Expect(Project().componentEditors.FindType("Workflow.Probe, Ursa.Game") != nullptr,
				       "Failed compilation removed the previous editing registration");
				RequestPlayToggle();
				phase = 2;
			}
			else if (phase == 2)
			{
				Expect(!RunningScene(), "Run accepted failed latest script inputs");
				WriteSource(source);
				Done(CompileScripts());
				phase = 3;
			}
			else if (phase == 3)
			{
				Expect(!RunningScene() && Take(owner->Name()) == "Editor root" &&
				           nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["value"] == 19,
				       "Reload changed an editing object or lost an unsaved field");
				RequestPlayToggle();
				phase = 4;
			}
			else if (phase == 4 && !RunningScene())
			{
				RequestPlayToggle();
			}
			else if (phase == 4 && RunningScene())
			{
				const auto roots = RunningScene()->RootGameObjects();
				GameObject::Ptr runtimeOwner;
				for (const auto& root : roots)
				{
					if (root->Id() == owner->Id())
					{
						runtimeOwner = root;
					}
				}
				if (!runtimeOwner || Take(runtimeOwner->Name()) == "Editor root")
				{
					return {};
				}
				if (Take(runtimeOwner->Name()) != "Runtime:1:19" || Take(owner->Name()) != "Editor root")
				{
					throw std::runtime_error(
					    "Unexpected Run clone: runtime=" + ToStdString(Take(runtimeOwner->Name())) + ", edit=" +
					    ToStdString(Take(owner->Name())) + ", payload=" + ToStdString(script->SerializedPayload()));
				}
				RequestPlayToggle();
				phase = 5;
			}
			else if (phase == 5 && RunningScene())
			{
				RequestPlayToggle();
			}
			else if (phase == 5 && !RunningScene())
			{
				Expect(Documents()->Current()->Dirty() && Take(owner->Name()) == "Editor root",
				       "Stop lost the original editing document");
				if (++runs < 3)
				{
					RequestPlayToggle();
					phase = 4;
				}
				else
				{
					Done(Documents()->SaveCurrent());
					completed = true;
					window->RequestClose();
				}
			}
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(ToUrsaString(error.what()));
		}
		return {};
	}

private:
	void WriteSource(const String& text)
	{
		Vector<UInt8> bytes(text.begin(), text.end());
		Done(Workspace()->Index().Filesystem().WriteAtomic(Take(VirtualPath::Parse("prj:Probe.cs")), bytes));
	}

	std::shared_ptr<SceneDraft> draft;
	GameObject::Ptr owner;
	Scripting::ScriptBehaviourComponent* script = nullptr;
	std::chrono::steady_clock::time_point deadline;
	UInt32 phase = 0;
	UInt32 runs = 0;
	UInt32 responsiveFrames = 0;
	bool completed = false;
};
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("ManagedEditorWorkflow-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(root / "Assets");
		std::ofstream(root / "Assets" / "Probe.cs") << source;
		WorkflowEditor editor;
		Done(editor.OpenProject(root));
		Done(editor.Run(1280, 720, "Managed Editor Workflow", false));
		Expect(editor.Verified(), "Editor did not render while compiling or did not finish the workflow");
		std::cout << "Editor workflow: compile/error/recovery/reload/unsaved clone/3 Run-Stop cycles/save passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
