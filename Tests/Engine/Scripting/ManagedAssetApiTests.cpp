#include <Core/StringInterop.hpp>
#include <Material/Material.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Runtime.generated.hpp>
#include <BindingCalls.hpp>
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

bool SameHandle(const Bindings::Wire::Value_Ursa_NativeHandle& left,
                const Bindings::Wire::Value_Ursa_NativeHandle& right)
{
	return left.F_sessionId == right.F_sessionId && left.F_index == right.F_index &&
	       left.F_generation == right.F_generation && left.F_kind == right.F_kind;
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
		using namespace Ursa::Test::BindingCalls;
		namespace R = Bindings::Runtime;
		using Handle = Bindings::Wire::Value_Ursa_NativeHandle;
		using Asset = Bindings::Wire::Value_Ursa_NativeAssetReference;
		void* context = sceneApi.Token();
		const String textureGuid = textureId.ToString();
		const String materialGuid = materialId.ToString();
		const String missingGuid = ResourceId::Generate().ToString();
		Asset textureAsset{StringView(textureGuid), 0};
		Asset materialAsset{StringView(materialGuid), 0};
		Asset missingAsset{StringView(missingGuid), 0};
		auto textureHandle = Value(Invoke<Handle>(R::NativeResourceBindings_LoadTexture, context, &textureAsset));
		Expect(textureHandle.F_kind == UInt32(ManagedHandleKind::Asset),
		       "Generated binding failed to load Texture by stable identity");
		auto cached = Value(Invoke<Handle>(R::NativeResourceBindings_LoadTexture, context, &textureAsset));
		Expect(SameHandle(textureHandle, cached), "Generated Texture binding did not reuse the session handle");
		Expect(!Invoke<Handle>(R::NativeResourceBindings_LoadTexture, context, &materialAsset),
		       "Texture loader accepted a Material");
		Expect(!Invoke<Handle>(R::NativeMaterialBindings_Load, context, &textureAsset),
		       "Material loader accepted a Texture");
		Expect(!Invoke<Handle>(R::NativeResourceBindings_LoadTexture, context, &missingAsset),
		       "Texture loader accepted a missing GUID");
		textureAsset.F_LocalFileId = 99;
		Expect(!Invoke<Handle>(R::NativeResourceBindings_LoadTexture, context, &textureAsset),
		       "Texture loader accepted a missing local file ID");

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
