#include <ResourceFixtures.hpp>
#include <Assets/TextureDraft.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	const auto root =
	    std::filesystem::current_path() / ("texture_draft_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::create_directories(root / "Assets");
		auto draft = Take(Editor::TextureDraft::Create(
		    2, 2, TextureColorSpace::Srgb, {10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255}));
		const auto before = draft->GetPixels();
		Expect(!draft->ReplacePixels(2, 2, TextureColorSpace::Srgb, {1, 2, 3}), "Invalid pixel count was accepted");
		Expect(draft->GetPixels() == before, "Invalid pixel count modified the edit draft");
		auto resizedDraft = Take(Editor::TextureDraft::Create(2, 2, TextureColorSpace::Srgb, before));
		done(resizedDraft->Resize(3, 3));
		const auto& expanded = resizedDraft->GetPixels();
		Expect(resizedDraft->GetWidth() == 3 && resizedDraft->GetHeight() == 3 && expanded.size() == 36 &&
		           expanded[0] == 10 && expanded[4] == 40 && expanded[8] == 255 && expanded[12] == 70 &&
		           expanded[16] == 100 && expanded[32] == 255,
		       "Texture expansion lost pixels, used the wrong row stride, or failed to update dimensions");
		done(resizedDraft->Resize(1, 2));
		Expect(resizedDraft->GetPixels() == Vector<UInt8>{10, 20, 30, 255, 70, 80, 90, 255},
		       "Texture shrink did not preserve the overlapping pixels");
		done(resizedDraft->SetColorSpace(TextureColorSpace::Linear));
		Expect(resizedDraft->GetColorSpace() == TextureColorSpace::Linear, "Texture color space did not change");
		done(resizedDraft->ReplacePixels(1, 1, TextureColorSpace::Srgb, {1, 2, 3, 4}));
		Expect(resizedDraft->GetWidth() == 1 && resizedDraft->GetHeight() == 1 &&
		           resizedDraft->GetColorSpace() == TextureColorSpace::Srgb &&
		           resizedDraft->GetPixels() == Vector<UInt8>{1, 2, 3, 4},
		       "Texture replacement did not update the draft members");
		done(draft->SetPixel(1, 0, {201, 202, 203, 204}));
		const auto path = root / "Assets" / "checker.ursatex";
		const auto id = ResourceId::Generate();
		VirtualFileSystem filesystem;
		done(filesystem.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite));
		done(draft->Save(filesystem, Take(VirtualPath::Parse("prj:checker.ursatex")), id));
		auto loaded = Take(LoadTextureResource(path));
		Expect(loaded.id == id && loaded.texture->GetWidth() == 2 && loaded.texture->GetHeight() == 2 &&
		           loaded.texture->GetColorSpace() == TextureColorSpace::Srgb &&
		           loaded.texture->GetPixels() == draft->GetPixels(),
		       "Non-default texture did not round-trip");
		auto workspace = Take(Editor::ProjectWorkspace::Open(root));
		auto reopened =
		    Take(Editor::OpenTextureDocument(Take(VirtualPath::Parse("prj:checker.ursatex")), workspace->Index()));
		Expect(reopened->Id() == id && reopened->Payload<Editor::TextureDraft>()->GetPixels() == draft->GetPixels(),
		       "Editor texture document did not reopen with stable ID and pixels");
		auto documents = Take(Editor::EditorDocuments::Create(*workspace));
		auto created = Take(Editor::NewTextureDocument(Take(VirtualPath::Parse("prj:created.ursatex"))));
		const auto createdId = created->Id();
		done(documents->RequestOpen(std::move(created)));
		done(documents->SaveCurrent());
		Expect(Take(LoadTextureResource(root / "Assets" / "created.ursatex")).id == createdId,
		       "New procedural texture did not retain its generated ID");
		std::filesystem::remove_all(root);
		std::cout << "Editor texture authoring test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
