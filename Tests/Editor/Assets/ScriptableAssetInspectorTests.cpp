#include <Inspector/AssetInspectorPanel.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <imgui_internal.h>
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
} // namespace

int main(int count, char** arguments)
{
	try
	{
		VirtualFileSystem files;
		Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto path = Take(VirtualPath::Parse("prj:Settings.ursaasset"));
		Done(files.WriteAtomic(path, {}));
		auto workspace = Take(ProjectWorkspace::FromFilesystem({}, files));
		Done(workspace->Refresh());
		Done(workspace->SetRuntimeContentView(workspace->Index().Filesystem()));
		const auto guid = Take(workspace->Database()->Metadata(path)).guid;
		ScriptTypeDescriptor type;
		type.sourceGuid = ResourceId::Generate();
		type.localFileId = 42;
		type.scriptType = "test.InspectorSettings";
		type.clrTypeName = "Tests.InspectorSettings, Ursa.Game";
		type.kind = ScriptTypeKind::ScriptableObject;
		type.fields.push_back({"enabled", "Boolean", "Unsupported", "System.Boolean", "true"});
		auto asset = Take(CreateScriptableAssetWithDefaults(guid, type));
		auto payload = asset.Payload();
		payload["Fields"]["unknown"] = "preserve across Inspector saves";
		Done(asset.SetDescription(guid, type.localFileId, type.scriptType, type.version, payload));
		Done(SaveScriptableAssetSource(files, path, asset));
		const auto secondPath = Take(VirtualPath::Parse("prj:OtherSettings.ursaasset"));
		Done(files.WriteAtomic(secondPath, {}));
		Done(workspace->Refresh());
		const auto secondGuid = Take(workspace->Database()->Metadata(secondPath)).guid;
		auto secondAsset = Take(CreateScriptableAssetWithDefaults(secondGuid, type));
		Done(SaveScriptableAssetSource(files, secondPath, secondAsset));
		Done(workspace->Refresh());
		ProjectBrowserModel browser;
		Done(browser.Refresh(*workspace));
		EditorSelectionContext selection;
		selection.SelectAsset(path, 0, {}, guid);
		const auto firstSelection = *selection.PrimaryEntry();
		EditorSelectionEntry secondSelection;
		secondSelection.kind = EditorSelectionKind::Asset;
		secondSelection.assetPath = secondPath;
		secondSelection.assetGuid = secondGuid;
		Expect(selection.SelectEntries({firstSelection, secondSelection}, 0),
		       "Could not establish ScriptableObject multi-selection for Inspector interaction");
		Vector<ScriptTypeDescriptor> types{type};
		auto device = Take(Rhi::CreateDevice({}));
		auto gui = Take(ImGuiBridge::Create(device));
		AssetInspectorPanel panel;
		selection.beforeChange = [&panel](const EditorSelectionContext& next)
		{ return !panel.HasModifiedSource() || panel.RetainsModifiedTargets(next); };
		bool open = true;
		UInt32 imports = 0;
		UInt32 footerDraws = 0;
		ResourceId footerTarget;
		const auto frame = [&]()
		{
			Done(gui->BeginFrame(1.f / 60.f, 720, 480));
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({720, 480}, ImGuiCond_Always);
			ImGui::SetNextWindowFocus();
			panel.Draw(
			    *workspace, browser, selection, open, &types, {},
			    [&](const VirtualPath& imported)
			    {
				    Expect(imported == path || imported == secondPath,
				           "Inspector requested import of an unselected asset");
				    ++imports;
			    },
			    72.f,
			    [&](Float32)
			    {
				    ++footerDraws;
				    const auto* primary = selection.PrimaryEntry();
				    footerTarget = primary ? primary->assetGuid : ResourceId{};
			    });
			Done(gui->EndFrame());
		};
		frame();
		frame();
		auto* inspector = ImGui::FindWindowByName("Inspector");
		ImGuiWindow* propertiesWindow = nullptr;
		for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::strncmp(window->Name, "Inspector/AssetProperties", std::strlen("Inspector/AssetProperties")) == 0)
			{
				propertiesWindow = window;
				break;
			}
		}
		Expect(inspector && propertiesWindow && guid != type.sourceGuid,
		       "Inspector identity fixture was not independent");
		// Exercise the actual widgets through ImGui navigation; no desktop input is injected.
		const ImGuiID field = ImHashStr("enabled", 0, ImHashStr("/Fields/enabled", 0, propertiesWindow->ID));
		ImGui::ActivateItemByID(field);
		frame();
		Expect(Take(LoadScriptableAssetSource(files, path, guid)).Payload()["Fields"]["enabled"] == true &&
		           Take(LoadScriptableAssetSource(files, secondPath, secondGuid)).Payload()["Fields"]["enabled"] ==
		               true,
		       "An unsaved Inspector edit changed the persistent source");
		Expect(panel.HasModifiedSource(), "Multi-object editing did not track dirty ScriptableObject drafts");
		const bool selectionAccepted = selection.SelectEntries({secondSelection}, 0);
		Expect(!selectionAccepted && selection.entries.size() == 2 && selection.PrimaryEntry()->assetGuid == guid,
		       "Changing the Inspector target group discarded an unsaved multi-object draft");
		ImGui::ActivateItemByID(ImHashStr("Save Selected ScriptableObjects", 0, propertiesWindow->ID));
		frame();
		const auto saved = Take(LoadScriptableAssetSource(files, path, guid));
		const auto secondSaved = Take(LoadScriptableAssetSource(files, secondPath, secondGuid));
		Expect(imports == 2 && saved.Payload()["Fields"]["enabled"] == false &&
		           secondSaved.Payload()["Fields"]["enabled"] == false,
		       "Multi-object Inspector did not edit and save every selected ScriptableObject draft");
		Expect(footerDraws == 4 && footerTarget == guid,
		       "Multi-object asset editing changed or hid the primary asset preview footer");
		Expect(saved.SourceGuid() == guid && saved.Payload()["Fields"]["unknown"] == payload["Fields"]["unknown"],
		       "Inspector saving changed the asset identity or discarded unknown fields");

		if (count > 1)
		{
			auto color =
			    Take(device->CreateTexture({720, 480, Rhi::Format::RgbA8Unorm,
			                                Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource}));
			auto command = Take(device->CreateCommandList());
			Done(command->Begin());
			Done(command->Barrier(color, Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment));
			Rhi::RenderTarget target;
			target.color = color;
			Done(command->BeginRendering(target));
			Done(gui->Record(command, color->desc.format, 720, 480));
			Done(command->EndRendering());
			Done(command->End());
			Done(Take(device->Submit(command))->Wait());
			Rendering::FrameCapture capture;
			Done(capture.Configure(std::filesystem::u8path(arguments[1])));
			Done(capture.Capture(device, color, Rhi::ResourceState::ColorAttachment));
			Done(capture.Finish());
		}
		Done(device->WaitIdle());
		std::cout << "ScriptableObject Inspector widget edit/save and GUID isolation passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
