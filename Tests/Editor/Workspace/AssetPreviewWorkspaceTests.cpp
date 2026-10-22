#include <Application/EditorApplication.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <imgui_internal.h>
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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

struct SavedLayout
{
	Float32 height = 0;
	VirtualPath settings;
};

class PreviewEditor final : public EditorApplication
{
public:
	PreviewEditor(UInt32 selectedMode, SavedLayout& selectedLayout, std::filesystem::path selectedOutput,
	              bool packagedResources)
	    : mode(selectedMode), saved(selectedLayout), capturePath(std::move(selectedOutput)), packaged(packagedResources)
	{
		SetStartupWindow(mode == 2 ? "Material Preview" : "Inspector");
		SetStartupAsset(Take(VirtualPath::Parse("prj:Preview.ursamat")));
	}

	void CreateMaterial()
	{
		auto document = Take(
		    NewMaterialDocument(Take(VirtualPath::Parse("prj:Preview.ursamat")), MaterialType::Unlit, *Workspace()));
		Done(Documents()->RequestOpen(std::move(document)));
		Done(Documents()->SaveCurrent());
	}

	bool Verified() const
	{
		return verified;
	}

protected:
	Result<void> OnStart() override
	{
		if (packaged)
		{
			// Run prepares the editable project's content after creating the UI. Reapply
			// the read-only Engine package before opening the asset and render services.
			auto files = Take(OpenEngineFileSystem(capturePath.parent_path() / "PreviewWorkspaceEngine.pak"));
			Done(Workspace()->SetRuntimeContentView(files));
			ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(std::move(files)));
			Done(Documents()->RefreshResources());
			const auto providers = Workspace()->ContentFiles().Providers();
			bool foundPackage = false;
			for (const auto& provider : providers)
			{
				if (provider.scheme == engineScheme)
				{
					foundPackage =
					    provider.access == MountAccess::ReadOnly && provider.provider->SourceLabel().find("pak:") == 0;
					break;
				}
			}
			Expect(foundPackage, "Application replaced its Engine package with loose resources during startup");
		}
		if (mode == 0)
		{
			for (const auto& entry : Take(Workspace()->Index().Filesystem().List("user")))
			{
				initialUserSettings.insert(entry.path.ToString());
			}
		}
		if (mode == 1)
		{
			ImGui::LoadIniSettingsFromMemory("[Window][Material Preview]\nPos=20,20\nSize=256,256\nCollapsed=0\n");
		}
		if (mode == 3)
		{
			const String invalid = R"({"height":700,"expanded":"invalid"})";
			Done(Workspace()->Index().Filesystem().WriteAtomic(
			    saved.settings, Vector<UInt8>(invalid.data(), invalid.data() + invalid.size())));
		}
		return EditorApplication::OnStart();
	}

	Result<void> OnGui() override
	{
		auto result = EditorApplication::OnGui();
		if (!result)
		{
			return result;
		}
		try
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				throw std::runtime_error("Application Preview workspace timed out: " + ToStdString(ScriptStatus()) +
				                         "; " + ToStdString(AssetStatus()));
			}
			auto* preview = InspectorPreview();
			if (switchedProject)
			{
				Expect(!preview->Supported() && !preview->Content() && !preview->State().HasImage(),
				       "Live project switch retained the previous project's preview session");
				window->RequestClose();
				return {};
			}
			if (AssetWorkPending() || !preview->Supported() || (mode == 2 && !preview->State().HasImage()))
			{
				return {};
			}
			++frames;
			auto* inspector = ImGui::FindWindowByName("Inspector");
			auto* project = ImGui::FindWindowByName("Hierarchy");
			Expect(inspector && project && !ImGui::FindWindowByName("Material Preview"),
			       "Preview startup created an independent window or lost workspace panels");
			if (mode == 0)
			{
				if (frames == 2)
				{
					ImGui::DockBuilderDockWindow("Inspector", project->DockNode->ID);
					SetStartupWindow("Inspector");
				}
				if (frames == 3)
				{
					Collapse(inspector);
				}
				if (frames >= 5)
				{
					Expect(preview->ReservedHeight() < 100, "Application did not persist collapsed Preview");
					for (const auto& entry : Take(Workspace()->Index().Filesystem().List("user")))
					{
						if (entry.path.relative.find("Settings/AssetPreviews/") == 0 &&
						    initialUserSettings.find(entry.path.ToString()) == initialUserSettings.end())
						{
							saved.settings = entry.path;
						}
					}
					Expect(!saved.settings.relative.empty(), "Preview settings were not saved to user storage");
					// Height dragging is covered by the panel's real mouse test. Seed a non-default
					// persisted value here to independently verify full application restoration.
					saved.height = 335;
					const String settings =
					    nlohmann::json{{"height", saved.height}, {"expanded", false}}.dump().c_str();
					Done(Workspace()->Index().Filesystem().WriteAtomic(
					    saved.settings, Vector<UInt8>(settings.data(), settings.data() + settings.size())));
					verified = true;
				}
			}
			else if (frames >= 3)
			{
				Expect(inspector->DockNode == project->DockNode, "Restart reset the customized workspace dock");
				if (mode == 1 && frames == 3)
				{
					Expect(preview->ReservedHeight() < 100, "Inspector startup overwrote collapsed preference");
					preview->Expand();
					Expect(preview->ReservedHeight() == saved.height, "Application restart lost logical height");
					Collapse(inspector);
				}
				if (mode == 2)
				{
					Expect(preview->ReservedHeight() == saved.height && preview->State().HasImage(),
					       "Legacy startup failed to expand and render Inspector Preview");
					Expect(GImGui->NavWindow && GImGui->NavWindow->RootWindowDockTree == inspector->RootWindowDockTree,
					       "Legacy startup did not focus Inspector");
					if (captureRequested && !ScriptCompilationPending())
					{
						FinishScriptJobs();
						// Keep native shader staging paths within the same depth as the first fixture.
						const auto nextProject =
						    capturePath.parent_path().parent_path() / ToStdString(ResourceId::Generate().ToString());
						std::filesystem::create_directories(nextProject / "Assets");
						// Current draw data already references the old preview image. It must
						// remain recordable after project services and private content close.
						auto opened = OpenProject(nextProject);
						if (!opened && opened.error == "Wait for asset import/discovery before changing projects")
						{
							return {};
						}
						Done(opened);
						switchedProject = true;
						// Opening the second project performs a separate cold bootstrap; give its
						// first frame its own bounded deadline after that synchronous work finishes.
						deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
					}
				}
				if (mode == 3)
				{
					Expect(preview->ReservedHeight() == 280 && !preview->SettingsDiagnostic().empty(),
					       "Application startup partially restored corrupt Preview settings");
				}
				if (frames >= 5)
				{
					verified = true;
				}
			}
			if (verified)
			{
				ImGui::GetIO().WantSaveIniSettings = true;
				if (!captureRequested)
				{
					Done(SetFrameCapture(capturePath));
					captureRequested = true;
					std::cout << "Application framebuffer scale: " << ImGui::GetIO().DisplayFramebufferScale.x << '\n';
				}
				else if (mode != 2)
				{
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
	static void Collapse(ImGuiWindow* inspector)
	{
		const auto seed = ImHashStr("AssetPreview", 0, inspector->ID);
		ImGui::ActivateItemByID(ImHashStr("v Preview", 0, seed));
	}

	UInt32 mode;
	SavedLayout& saved;
	UInt32 frames = 0;
	Set<String> initialUserSettings;
	bool verified = false;
	bool captureRequested = false;
	bool switchedProject = false;
	std::filesystem::path capturePath;
	bool packaged;
	std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
};
} // namespace

int main(int count, char** arguments)
{
	try
	{
		Expect(count == 4, "Expected project directory, resource variant, and restart mode");
		const auto root = std::filesystem::absolute(std::filesystem::u8path(arguments[1]));
		const String variant(arguments[2]);
		const auto mode = static_cast<UInt32>(std::stoul(arguments[3]));
		Expect((variant == "Loose" || variant == "Pak") && mode < 4, "Invalid workspace restart stage");
		std::filesystem::create_directories(root / "Assets");
		SavedLayout layout;
		const auto layoutPath = root / "PreviewWorkspaceLayout.json";
		if (mode != 0)
		{
			std::ifstream input(layoutPath);
			const auto settings = nlohmann::json::parse(input);
			layout.height = settings.at("height").get<Float32>();
			layout.settings = Take(VirtualPath::Parse(ToUrsaString(settings.at("settings").get<std::string>())));
		}
		PreviewEditor editor(mode, layout, root / ("Workspace-" + std::to_string(mode) + ".png"), variant == "Pak");
		Done(editor.OpenProject(root));
		if (mode == 0)
		{
			editor.CreateMaterial();
		}
		if (variant == "Pak")
		{
			VirtualFileSystem storage;
			Done(storage.Mount(editorLibraryScheme, root / "Library", MountAccess::ReadOnly));
			const auto snapshot = Take(ReadRuntimeContent(storage, EngineBuildConfiguration(), "EditorBootstrap"));
			const auto files = Take(OpenRuntimeContentView(std::make_shared<VirtualFileSystem>(storage), snapshot));
			VirtualFileSystem output;
			Done(output.Mount("out", root, MountAccess::ReadWrite));
			Done(PakBuilder::Build(files, {engineScheme}, output,
			                       Take(VirtualPath::Parse("out:PreviewWorkspaceEngine.pak"))));
			ConfigureEngineFileSystem(
			    std::make_shared<VirtualFileSystem>(Take(OpenEngineFileSystem(root / "PreviewWorkspaceEngine.pak"))));
		}
		Done(editor.Run(1280, 900, "Inspector Preview Workspace", false));
		Expect(editor.Verified(), "Application Preview workspace checks did not finish");
		if (mode == 0)
		{
			std::ofstream output(layoutPath);
			output << nlohmann::json{{"height", layout.height}, {"settings", ToStdString(layout.settings.ToString())}};
			Expect(bool(output), "Cannot save restart fixture metadata");
		}
		std::cout << variant.c_str() << " application process restart mode " << mode << " passed: " << root << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
