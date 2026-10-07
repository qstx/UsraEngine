#include <Serialization/TextureResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/PakArchive.hpp>
#include <Vfs/UserDataStore.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

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

VirtualPath Path(const char* name)
{
	return Take(VirtualPath::Parse(name));
}
} // namespace

int main()
{
	using namespace Ursa;
	const auto root =
	    std::filesystem::temp_directory_path() / ("UrsaResourceVfs-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::create_directories(root / "Assets");
		std::filesystem::create_directories(root / "Output");
		VirtualFileSystem source;
		Done(source.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite));
		const auto textureId = ResourceId::Generate();
		const auto materialId = ResourceId::Generate();
		const auto sceneId = ResourceId::Generate();
		const auto prefabId = ResourceId::Generate();
		auto texture =
		    Take(MaterialTexture::Create(256, 256, TextureColorSpace::Srgb, Vector<UInt8>(256 * 256 * 4, 200)));
		Done(SaveTextureResource(source, Path("prj:Texture.ursatex"), textureId, *texture));
		auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		Done(material->SetTexture(MaterialTextureSlot::BaseColor, texture));
		Done(SaveMaterialResource(source, Path("prj:Material.ursamat"), materialId, *material,
		                          [&](MaterialTexturePtr) { return Result<ResourceId>::Success(textureId); }));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		Scene authored;
		auto camera = Take(authored.CreateGameObject("Camera"));
		auto cameraComponent = Take(authored.AddComponent<CameraComponent>(camera));
		Done(authored.SetActiveCamera(cameraComponent));
		auto box = Take(authored.CreateGameObject("Box"));
		Take(authored.AddComponent<BoxRenderComponent>(box));
		authored.FindComponent<BoxRenderComponent>(box)->material = material;
		const MaterialIdResolver identify = [&](const std::shared_ptr<MaterialInstance>&)
		{ return Result<ResourceId>::Success(materialId); };
		Done(SaveSceneResource(source, Path("prj:Scene.ursascene"), sceneId, authored, registry, identify));
		Done(SavePrefabResource(source, Path("prj:Box.ursaprefab"), prefabId, authored, box, registry, identify));
		VirtualFileSystem output;
		Done(output.Mount("output", root / "Output", MountAccess::ReadWrite));
		Done(PakBuilder::Build(source, {projectScheme}, output, Path("output:Content.pak")));
		auto archive = Take(PakArchive::Open(root / "Output" / "Content.pak"));
		Expect(archive->VerifiedBlockCount() == 0, "Mount scanned Pak payloads");
		VirtualFileSystem packed;
		Done(archive->Mount(packed));
		for (const VirtualFileSystem* filesystem : {&source, &packed})
		{
			auto session = Take(ResourceSession::Open(*filesystem));
			Expect(session->Index().Entries().size() == 4, "Resource index differs between directory and Pak");
			auto restoredMaterial = Take(session->LoadMaterial(materialId));
			Expect(restoredMaterial->Texture(MaterialTextureSlot::BaseColor)->GetPixels() == texture->GetPixels(),
			       "Texture dependency did not resolve");
			ComponentLoadContext context(*session);
			context.Provide(registry);
			auto restored = Take(LoadSceneResource(*filesystem, Path("prj:Scene.ursascene"), registry, context));
			Expect(Take(restored.scene->CollectRenderData(1)).boxes.size() == 1,
			       "Scene graph did not load through VFS");
			auto prefab = Take(LoadPrefabAssetResource(*filesystem, Path("prj:Box.ursaprefab"), registry, context));
			Expect(prefab.Id() == prefabId && prefab.Root()->FindComponent<BoxRenderComponent>(),
			       "PrefabAsset did not load through VFS");
			Scene instances;
			Take(prefab.Instantiate(instances, registry, context, identify));
			Take(prefab.Instantiate(instances, registry, context, identify));
			Expect(instances.RootGameObjects().size() == 2, "Prefab could not be reused");
			ArchiveWriter writer;
			SerializationContext saveContext;
			saveContext.Provide(registry);
			saveContext.SetResourceIdResolver([&](const Serializable&)
			                                  { return Result<ResourceId>::Success(materialId); });
			writer.SetContext(&saveContext);
			Done(restored.scene->Serialize(writer));
			Scene binaryScene;
			Done(binaryScene.Deserialize(Take(BinaryArchive::Decode(Take(BinaryArchive::Encode(writer)))), context));
			Expect(Take(binaryScene.CollectRenderData(1)).boxes.size() == 1,
			       "Serializable Scene binary round trip failed");
			auto active = std::move(restored.scene);
			auto previous = active.get();
			Expect(!ReloadSceneResource(*filesystem, Path("prj:Missing.ursascene"), registry, context, active) &&
			           active.get() == previous,
			       "Failed reload replaced active Scene");
		}
		Expect(!SaveMaterialResource(packed, Path("prj:Material.ursamat"), materialId, *material, {}),
		       "Pak accepted a save");
		const auto old = Take(source.Read(Path("prj:Material.ursamat")));
		Expect(!SaveMaterialResource(source, Path("prj:Material.ursamat"), materialId, *material, {}) &&
		           Take(source.Read(Path("prj:Material.ursamat"))) == old,
		       "Failed serialization damaged an asset");
		VirtualFileSystem user;
		Done(user.MountProvider(userDataScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		UserDataStore store(user);
		Done(store.SaveSettings("Options.json", {1, 2}));
		Done(store.SaveGame("Slot.bin", {3, 4}));
		Expect(Take(store.LoadSettings("Options.json")) == Vector<UInt8>{1, 2} &&
		           Take(store.LoadGame("Slot.bin")) == Vector<UInt8>{3, 4},
		       "User data did not round trip");
		Expect(!store.SaveGame("../Escape.bin", {}) && !UserDataStore(packed).SaveGame("Slot.bin", {}),
		       "User data escaped its mount");
		std::filesystem::remove_all(root);
		std::cout << "Directory/Pak resource persistence and archive integration passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
