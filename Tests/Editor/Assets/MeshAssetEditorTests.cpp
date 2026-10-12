#include <Assets/MeshAssetEditor.hpp>
#include <Mesh/Primitive.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Scene/ComponentEditors.hpp>
#include <Serialization/MeshResource.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

class FailingMetadata final : public VirtualFileProvider
{
public:
	String Identity() const override
	{
		return "MeshMetadataFailure";
	}

	String SourceLabel() const override
	{
		return Identity();
	}

	Result<bool> Exists(const String& path) const override
	{
		return memory.Exists(path);
	}

	Result<UInt64> Size(const String& path) const override
	{
		return memory.Size(path);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return memory.ReadRange(path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		return memory.List();
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool create) override
	{
		return path.size() >= 5 && path.substr(path.size() - 5) == ".meta"
		           ? Result<void>::Failure("Synthetic metadata write failure")
		           : memory.WriteAtomic(path, bytes, create);
	}

	Result<void> Append(const String& path, const Vector<UInt8>& bytes, bool create) override
	{
		return memory.Append(path, bytes, create);
	}

	Result<void> Remove(const String& path) override
	{
		return memory.Remove(path);
	}

private:
	MemoryFileProvider memory;
};

void Run()
{
	VirtualFileSystem files;
	Done(files.Mount("ursa", std::filesystem::u8path(URSA_TEST_BUILTIN_SOURCE), MountAccess::ReadOnly));
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	auto resources = Take(ResourceSession::Open(files));
	Scene scene;
	auto a = Take(CreatePrimitive(scene, *resources, PrimitiveType::Cube));
	auto b = Take(CreatePrimitive(scene, *resources, PrimitiveType::Cube));
	auto* filterA = a->FindComponent<MeshFilterComponent>();
	auto* filterB = b->FindComponent<MeshFilterComponent>();
	auto* rendererA = a->FindComponent<MeshRendererComponent>();
	auto mesh = filterA->SharedMesh();
	Expect(mesh == filterB->SharedMesh() && rendererA != b->FindComponent<MeshRendererComponent>(),
	       "Primitives did not share geometry with independent Renderer components");
	const auto snapshot = mesh->Snapshot();
	Expect(snapshot->data.positions.size() == 24 && snapshot->data.indices.size() == 36 &&
	           snapshot->data.normals.size() == 24 && snapshot->data.uv0.size() == 24 &&
	           snapshot->data.bounds.minimum.x == -0.5f && snapshot->data.bounds.maximum.x == 0.5f,
	       "Builtin Cube geometry differs from the original unit cube");
	const auto count = scene.RootGameObjects().size();
	Expect(!CreatePrimitive(scene, *resources, PrimitiveType(100)) && scene.RootGameObjects().size() == count,
	       "Failed primitive left a partial object");
	ComponentEditors editors;
	Done(RegisterBuiltinComponentEditors(editors));
	ComponentLoadContext context(*resources);
	ComponentInspectorContext inspector{a, scene.RootGameObjects(), &scene, nullptr, &context};
	Expect(!editors.CanCreate("ursa.MeshFilter", inspector) && !editors.CanCreate("ursa.MeshRenderer", inspector) &&
	           !editors.CanCreate("ursa.SkinnedMeshRenderer", inspector),
	       "Add Component admitted a duplicate Filter/Renderer");
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	const auto destination = Take(VirtualPath::Parse("prj:Copy.ursamesh"));
	auto saved = Take(SaveMeshAs(*database, *resources, destination, mesh));
	Expect(saved != mesh && saved->AssetIdentity().guid != mesh->AssetIdentity().guid &&
	           saved->AssetIdentity().localFileId == 0,
	       "Save Mesh As retained the source asset identity");
	Expect(Take(database->Metadata(destination)).guid == saved->AssetIdentity().guid &&
	           Take(resources->LoadMesh(saved->AssetIdentity().guid, 0)) == saved,
	       "Saved Mesh was not indexed/cached");
	SerializationContext restore(resources.get());
	auto reopened = Take(LoadMeshResource(files, destination, restore));
	Expect(reopened.mesh->Snapshot()->data.indices == mesh->Snapshot()->data.indices, "Saved Mesh failed to reopen");
	Expect(!SaveMeshAs(*database, *resources, destination, mesh), "Save Mesh As overwrote an existing asset");
	Expect(!SaveMeshAs(*database, *resources, Take(VirtualPath::Parse("ursa:Meshes/Copy.ursamesh")), mesh),
	       "Save Mesh As wrote to the read-only engine mount");
	const auto moved = Take(VirtualPath::Parse("prj:Moved.ursamesh"));
	Done(database->Move(destination, moved));
	auto movedSession = Take(ResourceSession::Open(files));
	auto relocated = Take(movedSession->LoadMesh(saved->AssetIdentity().guid, 0));
	Expect(relocated->Snapshot()->data.indices == saved->Snapshot()->data.indices &&
	           relocated->AssetIdentity().guid == saved->AssetIdentity().guid,
	       "Directory move invalidated Mesh GUID references");
	VirtualFileSystem failing;
	Done(failing.MountProvider("prj", std::make_shared<FailingMetadata>(), MountAccess::ReadWrite));
	Done(failing.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	auto failingDatabase = Take(AssetDatabase::Open(failing));
	auto failed = SaveMeshAs(*failingDatabase, *resources, destination, mesh);
	Expect(!failed && !Take(failing.Exists(destination)) && filterA->SharedMesh() == mesh,
	       "Failed asset metadata write left a source file or changed the component binding");
}
} // namespace

int main()
{
	try
	{
		Run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
