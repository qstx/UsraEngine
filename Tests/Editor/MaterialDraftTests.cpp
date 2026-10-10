#include <ResourceFixtures.hpp>
#include <Core/Types.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/MaterialPreview.hpp>
#include <Serialization/TextureResource.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Rhi;

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

std::string bytes(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
} // namespace

int main()
{
	const auto root =
	    std::filesystem::current_path() / ("material_draft_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::create_directories(root / "Assets");
		const auto textureId = ResourceId::Generate();
		auto texture = Take(MaterialTexture::Create(
		    2, 2, TextureColorSpace::Srgb, {255, 80, 20, 255, 255, 80, 20, 255, 255, 80, 20, 255, 255, 80, 20, 255}));
		done(SaveTextureResource(root / "Assets" / "shared.ursatex", textureId, *texture));
		Vector<ResourceMount> engineMounts;
		for (const auto& provider : Take(OpenEngineFileSystem()).Providers())
		{
			engineMounts.push_back({provider.scheme, {}, MountAccess::ReadOnly, provider.provider});
		}
		auto workspace = Take(Editor::ProjectWorkspace::Open(root, engineMounts));
		auto documents = Take(Editor::EditorDocuments::Create(*workspace));
		const auto firstPath = root / "Assets" / "first.ursamat";
		auto first = Take(Editor::NewMaterialDocument(Take(VirtualPath::Parse("prj:first.ursamat")),
		                                              MaterialType::Unlit, *workspace));
		const auto firstId = first->Id();
		auto firstDraft = first->Payload<Editor::MaterialDraft>();
		done(firstDraft->SetTextureId(MaterialTextureSlot::BaseColor, textureId, documents->Session()));
		Expect(!firstDraft->SetTextureId(MaterialTextureSlot::Normal, textureId, documents->Session()),
		       "Unlit material accepted an unsupported texture slot");
		done(firstDraft->Material().SetBaseColor({0.8f, 0.6f, 0.4f, 1.f}));
		done(documents->RequestOpen(std::move(first)));
		done(documents->SaveCurrent());
		const auto firstBytes = bytes(firstPath);
		Expect(!firstDraft->Material().SetMetallic(-1.f), "Invalid Unlit metallic value was accepted");
		Expect(bytes(firstPath) == firstBytes, "Invalid material edit changed a saved resource");

		const auto secondPath = root / "Assets" / "second.ursamat";
		auto second = Take(Editor::NewMaterialDocument(Take(VirtualPath::Parse("prj:second.ursamat")),
		                                               MaterialType::PbrMetallicRoughness, *workspace));
		const auto secondId = second->Id();
		auto secondDraft = second->Payload<Editor::MaterialDraft>();
		done(secondDraft->SetTextureId(MaterialTextureSlot::BaseColor, textureId, documents->Session()));
		done(secondDraft->Material().SetMetallic(0.7f));
		auto pbrShader = secondDraft->Material().Shader();
		done(secondDraft->SetPropertyValue("_Roughness", MaterialPropertyValue::Range(0.3f)));
		done(secondDraft->SetTextureProperty("_EmissiveMap", textureId, *pbrShader, documents->Session()));
		done(secondDraft->Material().SetAlphaMode(AlphaMode::Mask, 0.4f));
		done(secondDraft->Material().SetSampler({false, true}));
		done(documents->RequestOpen(std::move(second)));
		done(documents->SaveCurrent());
		done(Editor::SetShaderMaterialProperty(*documents->Current(), "_Roughness",
		                                       MaterialPropertyValue::Range(0.45f)));
		Expect(documents->Current()->Dirty(), "Shader-driven material Inspector edits did not dirty the document");
		done(documents->SaveCurrent());
		auto reopenedFirst = Take(Editor::OpenMaterialDocument(Take(VirtualPath::Parse("prj:first.ursamat")),
		                                                       documents->Session(), *workspace));
		auto reopenedSecond = Take(Editor::OpenMaterialDocument(Take(VirtualPath::Parse("prj:second.ursamat")),
		                                                        documents->Session(), *workspace));
		auto firstLoaded = reopenedFirst->Payload<Editor::MaterialDraft>();
		auto secondLoaded = reopenedSecond->Payload<Editor::MaterialDraft>();
		Expect(reopenedFirst->Id() == firstId && reopenedSecond->Id() == secondId,
		       "Material resource IDs changed after reopen");
		Expect(firstLoaded->TextureId(MaterialTextureSlot::BaseColor) == textureId &&
		           secondLoaded->TextureId(MaterialTextureSlot::BaseColor) == textureId,
		       "Shared material texture reference ID changed");
		Expect(firstLoaded->Material().Texture(MaterialTextureSlot::BaseColor) ==
		           secondLoaded->Material().Texture(MaterialTextureSlot::BaseColor),
		       "Shared texture did not resolve to the same resource-session object");
		auto roughness = secondLoaded->Material().PropertyValue("_Roughness", *secondLoaded->Material().Shader());
		auto emissiveMap = secondLoaded->Material().PropertyValue("_EmissiveMap", *secondLoaded->Material().Shader());
		Expect(roughness && roughness.value.floatValue == 0.45f && emissiveMap &&
		           emissiveMap.value.textureId == textureId &&
		           secondLoaded->Material().Parameters().sampler.linear == false &&
		           secondLoaded->Material().Parameters().alphaMode == AlphaMode::Mask,
		       "PBR parameters did not reopen");

		DeviceDesc desc;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(CreateDevice(desc));
		auto gui = Take(Editor::ImGuiBridge::Create(device));
		auto preview = Take(Editor::MaterialPreview::Create(device, *gui, 64));
		done(preview->Render(secondLoaded->MaterialPtr()));
		auto readback =
		    Take(device->CreateBuffer({64 * 64 * 4, BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(command->Barrier(preview->ColorTexture(), ResourceState::ShaderRead, ResourceState::TransferSource));
		done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
		done(command->CopyTextureToBuffer(preview->ColorTexture(), readback));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
		Array<Ursa::UInt8, 64 * 64 * 4> pixels{};
		done(readback->Read(0, pixels.data(), pixels.size()));
		const Ursa::Size center = (32 * 64 + 32) * 4;
		Expect(pixels[center] > 30 || pixels[center + 1] > 30, "Material 3D preview did not render the cube");
		done(device->WaitIdle());
		preview.reset();
		gui.reset();
		device.reset();
		std::filesystem::remove_all(root);
		std::cout << "Editor material authoring test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
