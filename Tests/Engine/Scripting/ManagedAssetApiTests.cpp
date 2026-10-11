#include <Core/StringInterop.hpp>
#include <Material/Material.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/TextureResource.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const String& value)
{
	auto path = VirtualPath::Parse(value);
	if (!path)
	{
		throw std::runtime_error(ToStdString(path.error));
	}
	return path.value;
}

void WriteMeta(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId guid)
{
	const String metaPath = path.ToString() + ".meta";
	const nlohmann::json document = {{"version", 1},         {"guid", ToStdString(guid.ToString())},
	                                 {"folder", false},      {"importer", ""},
	                                 {"importerVersion", 0}, {"settings", nlohmann::json::object()}};
	const std::string text = document.dump(2);
	Vector<UInt8> bytes;
	bytes.reserve(text.size());
	for (unsigned char value : text)
	{
		bytes.push_back(value);
	}
	auto saved = filesystem.WriteAtomic(Path(metaPath), bytes);
	if (!saved)
	{
		throw std::runtime_error(ToStdString(saved.error));
	}
}

bool SameHandle(const ManagedHandle& left, const ManagedHandle& right)
{
	return left.sessionId == right.sessionId && left.index == right.index && left.generation == right.generation &&
	       left.kind == right.kind;
}
} // namespace

int main()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	const auto root = std::filesystem::current_path() / "temp" /
	                  ("UrsaManagedAssets-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::create_directories(root / "Assets");
		VirtualFileSystem filesystem;
		auto mounted = filesystem.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite);
		Expect(static_cast<bool>(mounted), "Could not mount managed asset test content");

		const ResourceId textureId = ResourceId::Generate();
		const ResourceId materialId = ResourceId::Generate();
		auto texture = MaterialTexture::Create(2, 2, TextureColorSpace::Srgb, Vector<UInt8>(16, 255));
		Expect(static_cast<bool>(texture), "Could not create texture asset fixture");
		auto savedTexture = SaveTextureResource(filesystem, Path("prj:Texture.ursatex"), textureId, *texture.value);
		Expect(static_cast<bool>(savedTexture), "Could not save texture asset fixture");
		WriteMeta(filesystem, Path("prj:Texture.ursatex"), textureId);

		auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		auto setTexture = material->SetTexture(MaterialTextureSlot::BaseColor, texture.value);
		Expect(static_cast<bool>(setTexture), "Could not set the texture on the material fixture");
		auto savedMaterial =
		    SaveMaterialResource(filesystem, Path("prj:Material.ursamat"), materialId, *material,
		                         [textureId](MaterialTexturePtr) { return Result<ResourceId>::Success(textureId); });
		Expect(static_cast<bool>(savedMaterial), "Could not save material asset fixture");
		WriteMeta(filesystem, Path("prj:Material.ursamat"), materialId);

		auto resources = ResourceSession::Open(filesystem);
		Expect(static_cast<bool>(resources), "Could not open managed asset ResourceSession");
		Scene scene;
		ManagedSceneApi sceneApi(scene, resources.value.get());
		NativeApiV4 api = sceneApi.Bind({});
		ManagedHandle textureHandle{};
		const String textureGuid = textureId.ToString();
		Expect(api.loadTexture(api.sceneContext, reinterpret_cast<const UInt8*>(textureGuid.data()), textureGuid.size(),
		                       0, &textureHandle) == 0 &&
		           textureHandle.kind == ManagedHandleKind::Asset,
		       "Managed API failed to load a Texture asset by stable identity");
		ManagedHandle cachedTextureHandle{};
		Expect(api.loadTexture(api.sceneContext, reinterpret_cast<const UInt8*>(textureGuid.data()), textureGuid.size(),
		                       0, &cachedTextureHandle) == 0 &&
		           SameHandle(textureHandle, cachedTextureHandle),
		       "Managed API did not reuse the Texture handle within its ResourceSession");

		ManagedHandle wrongTexture{};
		const String materialGuid = materialId.ToString();
		Expect(api.loadTexture(api.sceneContext, reinterpret_cast<const UInt8*>(materialGuid.data()),
		                       materialGuid.size(), 0, &wrongTexture) != 0,
		       "Managed API accepted a Material as a Texture");
		ManagedHandle wrongMaterial{};
		Expect(api.loadMaterial(api.sceneContext, reinterpret_cast<const UInt8*>(textureGuid.data()),
		                        textureGuid.size(), 0, &wrongMaterial) != 0,
		       "Managed API accepted a Texture as a Material");
		const ResourceId missingId = ResourceId::Generate();
		const String missingGuid = missingId.ToString();
		ManagedHandle missing{};
		Expect(api.loadTexture(api.sceneContext, reinterpret_cast<const UInt8*>(missingGuid.data()), missingGuid.size(),
		                       0, &missing) != 0,
		       "Managed API accepted a missing asset GUID");
		Expect(api.loadTexture(api.sceneContext, reinterpret_cast<const UInt8*>(textureGuid.data()), textureGuid.size(),
		                       99, &missing) != 0,
		       "Managed API accepted an invalid texture local file ID");

		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
