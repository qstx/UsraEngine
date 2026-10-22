#include <Assets/NativeAssetInspector.hpp>
#include <Assets/SceneDraft.hpp>
#include <Inspector/AssetInspectorPanel.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <imgui.h>
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
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		auto sources = std::make_shared<MemoryFileProvider>();
		Done(files.MountProvider(projectScheme, sources, MountAccess::ReadWrite));
		Done(files.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		auto workspace = Take(ProjectWorkspace::FromFilesystem({}, files));
		auto authorDocuments = Take(EditorDocuments::Create(*workspace));
		ComponentRegistry components;
		Done(RegisterBuiltinComponents(components));
		const auto path = Take(VirtualPath::Parse("prj:Prefab.ursaprefab"));
		auto prefab = Take(NewSceneDocument(path, ResourceKind::Prefab, components, authorDocuments->Session()));
		auto originalRoot = prefab->Payload<SceneDraft>()->GetPrefabAsset()->Root();
		Take(originalRoot->AddComponent<CameraComponent>());
		const auto rootId = originalRoot->Id();
		Done(authorDocuments->RequestOpen(std::move(prefab)));
		Done(authorDocuments->SaveCurrent());
		auto authorScene = Take(NewSceneDocument(Take(VirtualPath::Parse("prj:Author.ursascene")), ResourceKind::Scene,
		                                         components, authorDocuments->Session()));
		const auto authorId = authorScene->Id();
		Done(authorDocuments->RequestOpen(std::move(authorScene)));
		Done(authorDocuments->SaveCurrent());
		const auto originalBytes = Take(files.Read(path));
		auto inspector = Take(NativeAssetInspector::Open(*workspace, path, ResourceKind::Prefab, &components));
		Expect(inspector->Document().Id() != authorId && !inspector->Document().Dirty(),
		       "Source Inspector reused the active author document");
		ImGui::CreateContext();
		ImGui::GetIO().DisplaySize = {720, 480};
		ImGui::GetIO().DeltaTime = 1.f / 60.f;
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		UInt32 imports = 0;
		const auto frame = [&]
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({720, 480}, ImGuiCond_Always);
			ImGui::Begin("Source Prefab Inspector");
			Take(inspector->Draw(
			    [&](const VirtualPath& source)
			    {
				    Expect(source == path, "Source save imported another asset");
				    ++imports;
			    }));
			ImGui::End();
			ImGui::Render();
		};
		frame();
		frame();
		auto* window = ImGui::FindWindowByName("Source Prefab Inspector");
		ImGui::ActivateItemByID(ImHashStr("active", 0, ImHashStr("/Fields/active", 0, window->ID)));
		frame();
		Expect(inspector->Document().Dirty() && !authorDocuments->Current()->Dirty() &&
		           Take(files.Read(path)) == originalBytes,
		       "Default source properties wrote bytes or dirtied the active Scene before Save");
		ImGui::ActivateItemByID(ImHashStr("Save Asset", 0, window->ID));
		frame();
		Expect(imports == 1 && !inspector->Document().Dirty() && !authorDocuments->Current()->Dirty(),
		       "Source Save did not use its own document or queued the wrong number of imports");
		auto reopened = Take(NativeAssetInspector::Open(*workspace, path, ResourceKind::Prefab, &components));
		const auto root = reopened->Document().Payload<SceneDraft>()->GetPrefabAsset()->Root();
		Expect(root->Id() == rootId && !Take(root->ActiveSelf()) && root->FindComponent<CameraComponent>(),
		       "Source save/reopen lost the root identity, property or component");
		auto properties = Take(SerializedObject::Create(reopened->Properties()));
		Done(Take(properties->FindProperty("/Fields/name")).SetValue("Rejected name"));
		Done(Take(properties->FindProperty("/Fields/layer")).SetValue(UInt32{32}));
		Expect(!properties->Apply() && Take(root->Name()) != "Rejected name",
		       "Invalid Prefab layer partially applied another field");
		const auto metadataPath = Take(VirtualPath::Parse(path.ToString() + ".meta"));
		const auto originalMetadata = Take(files.Read(metadataPath));
		auto changedMetadata = originalMetadata;
		changedMetadata.push_back('\n');
		Done(files.WriteAtomic(metadataPath, changedMetadata));
		Expect(!reopened->Save() && !properties->Update(), "Source Inspector ignored an external metadata change");
		Done(files.WriteAtomic(metadataPath, originalMetadata));
		reopened.reset();
		Expect(!properties->Update(), "Closed source Prefab retained its property lease");
		AssetInspectorPanel panel;
		ProjectBrowserModel browser;
		Done(browser.Refresh(*workspace));
		EditorSelectionContext selection;
		selection.SelectAsset(path);
		bool visible = true;
		const auto panelFrame = [&]
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({720, 480}, ImGuiCond_Always);
			panel.Draw(*workspace, browser, selection, visible, nullptr, {}, {}, 0, {}, {}, {}, &components);
			ImGui::Render();
		};
		panelFrame();
		panelFrame();
		const auto panelWindow = ImGui::FindWindowByName("Inspector");
		const auto editPanel = [&]
		{
			ImGui::ActivateItemByID(ImHashStr("active", 0, ImHashStr("/Fields/active", 0, panelWindow->ID)));
			panelFrame();
			Expect(panel.HasModifiedSource(), "Project asset selection did not route to its source property draft");
		};
		const auto promptFrame = [&](bool open)
		{
			ImGui::NewFrame();
			if (open)
			{
				ImGui::OpenPopup("Unsaved Asset");
			}
			const auto result = panel.DrawSourceSelectionPrompt([&](const VirtualPath&) { ++imports; }, true);
			ImGui::Render();
			return result;
		};
		editPanel();
		promptFrame(true);
		auto* prompt = ImGui::FindWindowByName("Unsaved Asset");
		ImGui::ActivateItemByID(ImHashStr("Cancel", 0, prompt->ID));
		Expect(promptFrame(false) == ImporterSelectionResult::Cancelled && panel.HasModifiedSource(),
		       "Cancelling source selection lost its draft");
		promptFrame(true);
		ImGui::ActivateItemByID(ImHashStr("Discard", 0, prompt->ID));
		Expect(promptFrame(false) == ImporterSelectionResult::Accepted && !panel.HasModifiedSource(),
		       "Discard did not release the isolated source draft");
		panelFrame();
		panelFrame();
		editPanel();
		promptFrame(true);
		ImGui::ActivateItemByID(ImHashStr("Save", 0, prompt->ID));
		Expect(promptFrame(false) == ImporterSelectionResult::Accepted && !panel.HasModifiedSource() && imports == 2 &&
		           !authorDocuments->Current()->Dirty(),
		       "Source selection Save did not target the source document");
		panel.Reset();
		auto matchingDocument =
		    Take(OpenSceneDocument(path, ResourceKind::Prefab, components, authorDocuments->Session()));
		Done(authorDocuments->RequestOpen(std::move(matchingDocument)));
		auto shared = Take(
		    NativeAssetInspector::Open(*workspace, path, ResourceKind::Prefab, &components, authorDocuments.get()));
		Expect(!shared->OwnsDocument() && &shared->Document() == authorDocuments->Current(),
		       "Opening the same source document forked another editable draft");
		auto sharedProperties = Take(SerializedObject::Create(shared->Properties()));
		Done(Take(sharedProperties->FindProperty("/Fields/name")).SetValue("Shared Prefab source"));
		Done(sharedProperties->Apply());
		shared->Document().MarkDirty();
		Expect(Take(authorDocuments->Current()->Payload<SceneDraft>()->GetPrefabAsset()->Root()->Name()) ==
		           "Shared Prefab source",
		       "The source Inspector edited a copy of the already open document");
		const auto saveVersion = authorDocuments->Current()->SaveVersion();
		Done(shared->Save());
		Expect(authorDocuments->Current()->SaveVersion() == saveVersion + 1 && !authorDocuments->Current()->Dirty(),
		       "The shared source document save was not observable by its Inspector");
		Done(authorDocuments->RequestClose());
		Expect(!shared->Active() && !sharedProperties->Update() && !shared->Save(),
		       "An old Inspector used the document that replaced or closed its source");
		ImGui::DestroyContext();

		VirtualFileSystem readOnly;
		Done(readOnly.MountProvider(projectScheme, sources, MountAccess::ReadOnly));
		Done(readOnly.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(),
		                            MountAccess::ReadWrite));
		auto lockedWorkspace = Take(ProjectWorkspace::FromFilesystem({}, readOnly));
		auto locked = Take(NativeAssetInspector::Open(*lockedWorkspace, path, ResourceKind::Prefab, &components));
		auto lockedProperties = Take(SerializedObject::Create(locked->Properties()));
		Done(Take(lockedProperties->FindProperty("/Fields/name")).SetValue("Read-only write"));
		Expect(!lockedProperties->Apply() && !locked->Save(), "Read-only source accepted a property or Save write");
		std::cout
		    << "Source Prefab controls, isolated documents, atomic fields, conflicts and read-only saves passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
