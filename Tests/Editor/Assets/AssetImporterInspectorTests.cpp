#include <Assets/AssetImporterInspector.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/ModelImportView.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Workspace/EditorSelectionContext.hpp>
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

VirtualPath Path(const char* text)
{
	return Take(VirtualPath::Parse(text));
}

class FaultProvider final : public VirtualFileProvider
{
public:
	MemoryFileProvider files;
	bool rejectMetadata = false;

	String Identity() const override
	{
		return files.Identity();
	}

	String SourceLabel() const override
	{
		return files.SourceLabel();
	}

	Result<bool> Exists(const String& path) const override
	{
		return files.Exists(path);
	}

	Result<UInt64> Size(const String& path) const override
	{
		return files.Size(path);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return files.ReadRange(path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		return files.List();
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool create) override
	{
		return rejectMetadata && path.find(".meta") != String::npos
		           ? Result<void>::Failure("Injected metadata write failure")
		           : files.WriteAtomic(path, bytes, create);
	}

	Result<void> Append(const String& path, const Vector<UInt8>& bytes, bool create) override
	{
		return files.Append(path, bytes, create);
	}
};
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		auto sources = std::make_shared<FaultProvider>();
		Done(files.MountProvider(projectScheme, sources, MountAccess::ReadWrite));
		Done(files.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto source = Path("prj:Invalid.png");
		const auto sidecar = Path("prj:Invalid.png.meta");
		Done(files.WriteAtomic(source, {1, 2, 3}));
		auto workspace = Take(ProjectWorkspace::FromFilesystem({}, files));
		auto inspector = Take(AssetImporterInspector::Open(*workspace->Database(), source));
		const auto original = Take(files.Read(sidecar));
		ImGui::CreateContext();
		ImGui::GetIO().DisplaySize = {720, 480};
		ImGui::GetIO().DeltaTime = 1.f / 60.f;
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		UInt32 requests = 0;
		bool importFailed = false;
		const auto frame = [&]
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({720, 480}, ImGuiCond_Always);
			ImGui::Begin("Importer Inspector");
			Take(inspector->DrawDefault(*workspace,
			                            [&](const VirtualPath& path)
			                            {
				                            Expect(path == source, "Apply requested a different source");
				                            ++requests;
				                            importFailed =
				                                !workspace->Importer().Import(*workspace->Database(), path, true);
			                            }));
			ImGui::End();
			ImGui::Render();
		};
		frame();
		frame();
		auto* window = ImGui::FindWindowByName("Importer Inspector");
		Expect(window && !inspector->HasModified(), "Opening importer fields changed its settings");
		const ImGuiID alpha = ImHashStr("alpha", 0, ImHashStr("/Fields/alpha", 0, window->ID));
		ImGui::ActivateItemByID(alpha);
		frame();
		Expect(inspector->HasModified() && Take(files.Read(sidecar)) == original && requests == 0,
		       "Editing an actual importer checkbox wrote metadata or requested an import");
		ImGui::ActivateItemByID(ImHashStr("Revert", 0, window->ID));
		frame();
		Expect(!inspector->HasModified() && Take(files.Read(sidecar)) == original && requests == 0,
		       "Revert saved settings or requested an import");
		ImGui::ActivateItemByID(alpha);
		frame();
		ImGui::ActivateItemByID(ImHashStr("Apply", 0, window->ID));
		frame();
		Expect(requests == 1 && importFailed && !inspector->HasModified() &&
		           !Take(workspace->Database()->Metadata(source)).importSettings.at("alpha").get<bool>(),
		       "An import failure rolled back successfully applied settings or looked like a settings write failure");
		const auto saved = Take(files.Read(sidecar));
		auto properties = Take(SerializedObject::Create(inspector->Properties()));
		Done(Take(properties->FindProperty("/Fields/maxDimension")).SetValue(UInt64(0)));
		Done(properties->Apply());
		Expect(!inspector->Apply() && inspector->HasModified() && Take(files.Read(sidecar)) == saved,
		       "Invalid settings were persisted or lost their draft");
		auto invalidDraft = Take(SerializedObject::Create(inspector->Properties()));
		Expect(Take(Take(invalidDraft->FindProperty("/Fields/maxDimension")).Value()) == 0,
		       "Validation failure made the invalid field inaccessible for correction");
		Done(inspector->Revert());
		Done(properties->Update());
		Done(Take(properties->FindProperty("/Fields/alpha")).SetValue(true));
		Done(properties->Apply());
		sources->rejectMetadata = true;
		ImGui::ActivateItemByID(ImHashStr("Apply", 0, window->ID));
		frame();
		Expect(inspector->HasModified() && requests == 1 && Take(files.Read(sidecar)) == saved &&
		           !inspector->Diagnostic().empty(),
		       "A failed metadata write lost the draft, queued an import or changed the source metadata");
		sources->rejectMetadata = false;
		Done(inspector->Revert());
		Done(properties->Update());
		Done(Take(properties->FindProperty("/Fields/alpha")).SetValue(true));
		Done(properties->Apply());
		{
			auto lease = Take(files.AcquireExclusiveLease(Path("lib:Import.lock")));
			Expect(!inspector->Apply() && inspector->HasModified() && Take(files.Read(sidecar)) == saved,
			       "Importer Apply ignored the active project publication lease");
		}
		inspector.reset();
		inspector = Take(AssetImporterInspector::Open(*workspace->Database(), source));
		Expect(inspector->HasModified() && inspector->Settings()["alpha"] == true && Take(files.Read(sidecar)) == saved,
		       "Reopening an importer lost its unapplied recovery draft or wrote metadata");
		Expect(!properties->Update(), "A reopened importer reused the old target's property lease");
		properties = Take(SerializedObject::Create(inspector->Properties()));
		EditorSelectionContext selection;
		selection.SelectAsset(source);
		std::optional<EditorSelectionContext> pending;
		selection.beforeChange = [&](const EditorSelectionContext& next)
		{
			if (inspector->HasModified() &&
			    (next.kind != EditorSelectionKind::Asset || next.assetPath != source || next.assetLocalFileId))
			{
				pending = next;
				return false;
			}
			return true;
		};
		Expect(!selection.SelectAsset(Path("prj:Other.png")) && selection.assetPath == source && pending,
		       "An unapplied importer switched target before resolving its selection prompt");
		pending.reset();
		Expect(!selection.SelectAsset(source, 42, "Mesh") && !selection.assetLocalFileId && pending &&
		           pending->assetRole == EditorAssetSelectionRole::ImportedObject,
		       "A child selection bypassed the dirty importer guard or changed the accepted target");
		pending.reset();
		Expect(selection.assetPath == source && inspector->HasModified(),
		       "Cancelling target selection lost the old importer draft");
		const auto originalSelection = *selection.PrimaryEntry();
		EditorSelectionEntry secondAsset;
		secondAsset.kind = EditorSelectionKind::Asset;
		secondAsset.assetPath = Path("prj:Other.png");
		secondAsset.assetGuid = ResourceId::Generate();
		const auto originalRevision = selection.revision;
		selection.beforeChange = [&](const EditorSelectionContext& next)
		{
			const auto* proposedPrimary = next.PrimaryEntry();
			if (inspector->HasModified() && (!proposedPrimary || !proposedPrimary->SameIdentity(originalSelection)))
			{
				pending = next;
				return false;
			}
			return true;
		};
		Expect(!selection.SelectEntries({originalSelection, secondAsset}, 1) && pending &&
		           pending->entries.size() == 2 && pending->PrimaryEntry()->SameIdentity(secondAsset) &&
		           selection.entries.size() == 1 && selection.PrimaryEntry()->SameIdentity(originalSelection) &&
		           selection.revision == originalRevision,
		       "Dirty importer gate did not preserve the complete original selection and defer the full proposal");
		Done(inspector->Discard());
		Expect(selection.SelectEntries(pending->entries, pending->primaryIndex, pending->rangeAnchor) &&
		           selection.entries.size() == 2 && selection.PrimaryEntry()->SameIdentity(secondAsset) &&
		           selection.SelectAsset(Path("prj:Other.png")) && !inspector->HasModified() &&
		           Take(files.Read(sidecar)) == saved,
		       "Resolving an importer draft did not apply the full pending selection safely");
		Done(properties->Update());
		Done(Take(properties->FindProperty("/Fields/alpha")).SetValue(true));
		Done(properties->Apply());
		const auto external = Take(workspace->Database()->Metadata(source));
		auto changedSettings = external.importSettings;
		changedSettings["filter"] = "Point";
		Done(workspace->Database()->SetImporterSettings(source, external.importer, external.importerVersion,
		                                                changedSettings));
		inspector.reset();
		inspector = Take(AssetImporterInspector::Open(*workspace->Database(), source));
		Expect(inspector->HasModified() && !inspector->Apply() && !inspector->Diagnostic().empty(),
		       "A recovery draft silently replaced externally changed persistent settings");
		Done(inspector->Discard());
		const auto editAlpha = [&]
		{
			auto fields = Take(SerializedObject::Create(inspector->Properties()));
			auto alpha = Take(fields->FindProperty("/Fields/alpha"));
			Done(alpha.SetValue(!Take(alpha.Value()).get<bool>()));
			Done(fields->Apply());
		};
		const auto dialog = [&](bool open, bool canApply = true)
		{
			ImGui::NewFrame();
			if (open)
			{
				ImGui::OpenPopup("Unapplied Import Settings");
			}
			const auto result = inspector->DrawSelectionPrompt([&](const VirtualPath&) { ++requests; }, canApply);
			ImGui::Render();
			return result;
		};
		editAlpha();
		const auto beforeCancel = Take(files.Read(sidecar));
		const auto beforeCancelRequests = requests;
		Expect(dialog(true) == ImporterSelectionResult::Pending, "Selection prompt resolved without a command");
		auto* popup = ImGui::FindWindowByName("Unapplied Import Settings");
		Expect(popup != nullptr, "Actual importer selection modal was not created");
		ImGui::ActivateItemByID(ImHashStr("Cancel", 0, popup->ID));
		Expect(dialog(false) == ImporterSelectionResult::Cancelled && inspector->HasModified() &&
		           Take(files.Read(sidecar)) == beforeCancel && requests == beforeCancelRequests,
		       "Actual Cancel command discarded or applied the importer draft");
		const auto beforeDiscard = Take(files.Read(sidecar));
		dialog(true);
		ImGui::ActivateItemByID(ImHashStr("Discard", 0, popup->ID));
		Expect(dialog(false) == ImporterSelectionResult::Accepted && !inspector->HasModified() &&
		           Take(files.Read(sidecar)) == beforeDiscard,
		       "Actual Discard command persisted the settings or retained the draft");
		editAlpha();
		dialog(true, false);
		ImGui::ActivateItemByID(ImHashStr("Apply", 0, popup->ID));
		Expect(dialog(false, false) == ImporterSelectionResult::Pending && inspector->HasModified(),
		       "A busy importer selection prompt allowed Apply");
		ImGui::ActivateItemByID(ImHashStr("Apply", 0, popup->ID));
		const auto beforeApplyRequests = requests;
		Expect(dialog(false) == ImporterSelectionResult::Accepted && !inspector->HasModified() &&
		           requests == beforeApplyRequests + 1 && Take(files.Read(sidecar)) != beforeDiscard,
		       "Actual Apply command did not persist settings and queue exactly one import");
		const auto modelSource = Path("prj:Invalid.fbx");
		Done(files.WriteAtomic(modelSource, {1, 2, 3}));
		Done(workspace->Database()->Refresh());
		auto model = Take(ModelImportView::Open(modelSource, {}));
		auto modelInspector = Take(model->SettingsInspector(*workspace->Database()));
		UInt32 modelRequests = 0;
		bool customModel = false;
		const auto modelFrame = [&]
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({720, 480}, ImGuiCond_Always);
			ImGui::Begin("Embedded model settings");
			Done(model->Draw(
			    *workspace->Database(), workspace->Importer(), true, {},
			    [&](const VirtualPath& path)
			    {
				    Expect(path == modelSource, "Model Apply requested another source");
				    ++modelRequests;
			    },
			    workspace.get(), [&](ManagedEditorContext&) { return Result<bool>::Success(customModel); }, true));
			ImGui::End();
			ImGui::Render();
		};
		modelFrame();
		modelFrame();
		auto* modelWindow = ImGui::FindWindowByName("Embedded model settings");
		const auto normals = ImHashStr("generateNormals", 0, ImHashStr("/Fields/generateNormals", 0, modelWindow->ID));
		ImGui::ActivateItemByID(normals);
		modelFrame();
		Expect(modelInspector->HasModified() && modelRequests == 0,
		       "Default embedded model fields bypassed the importer draft");
		ImGui::ActivateItemByID(ImHashStr("Apply", 0, modelWindow->ID));
		modelFrame();
		Expect(!modelInspector->HasModified() && modelRequests == 1 &&
		           !Take(workspace->Database()->Metadata(modelSource)).importSettings["generateNormals"].get<bool>(),
		       "Embedded model settings did not use the shared Apply command");
		customModel = true;
		ImGui::ActivateItemByID(normals);
		modelFrame();
		Expect(!modelInspector->HasModified() && !ImGui::FindWindowByName("Model Import"),
		       "A custom model Inspector also drew the default fields or opened a second window");
		ImGui::DestroyContext();

		VirtualFileSystem readOnly;
		Done(readOnly.MountProvider(projectScheme, sources, MountAccess::ReadOnly));
		auto readOnlyDatabase = Take(AssetDatabase::Open(readOnly));
		Done(readOnlyDatabase->Refresh());
		auto readOnlyInspector = Take(AssetImporterInspector::Open(*readOnlyDatabase, source));
		auto locked = Take(SerializedObject::Create(readOnlyInspector->Properties()));
		auto lockedAlpha = Take(locked->FindProperty("/Fields/alpha"));
		Done(lockedAlpha.SetValue(!Take(lockedAlpha.Value()).get<bool>()));
		Expect(!locked->Apply() && !readOnlyInspector->HasModified(),
		       "Read-only importer source accepted a property transaction");
		readOnlyInspector.reset();
		Expect(!locked->Update(), "A closed importer target kept its property adapter alive");
		Expect(Take(files.Read(source)) == Vector<UInt8>({1, 2, 3}),
		       "Importer settings controls modified the image source");
		std::cout
		    << "Real importer fields, Apply/Revert, validation, write failure and import failure remain separate\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
