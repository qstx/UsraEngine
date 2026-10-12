#include <Assets/MeshAssetEditor.hpp>
#include <Serialization/MeshResource.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>

namespace Ursa::Editor
{
Result<MeshPtr> SaveMeshAs(AssetDatabase& database, ResourceSession& session, const VirtualPath& destination,
                           const MeshPtr& mesh)
{
	auto& files = database.Filesystem();
	auto kind = ResourceKindForPath(destination);
	auto writable = files.CheckWritable(destination);
	if (!mesh || !mesh->IsValid() || destination.scheme != database.SourceScheme() || !writable || !kind ||
	    kind.value != ResourceKind::Mesh)
	{
		return Result<MeshPtr>::Failure(
		    writable ? "Save Mesh As requires a valid Mesh and writable project .ursamesh path" : writable.error);
	}
	auto sidecar = VirtualPath::Parse(destination.ToString() + ".meta");
	if (!sidecar)
	{
		return Result<MeshPtr>::Failure(sidecar.error);
	}
	auto exists = files.Exists(destination);
	auto metadataExists = files.Exists(sidecar.value);
	if (!exists || !metadataExists || exists.value || metadataExists.value)
	{
		return Result<MeshPtr>::Failure("Save Mesh As requires a new asset path");
	}
	auto copy = mesh->Clone();
	if (!copy)
	{
		return copy;
	}
	const ResourceId id = ResourceId::Generate();
	auto bytes = EncodeMeshResource(id, *copy.value);
	if (!bytes)
	{
		return Result<MeshPtr>::Failure(bytes.error);
	}
	const nlohmann::json metadata{
	    {"version", assetMetadataVersion}, {"guid", ToStdString(id.ToString())},  {"folder", false}, {"importer", ""},
	    {"importerVersion", UInt32{0}},    {"settings", nlohmann::json::object()}};
	const auto text = metadata.dump(2);
	Vector<UInt8> metaBytes(text.data(), text.data() + text.size());
	VirtualFileSystem staging;
	auto mounted =
	    staging.MountProvider(destination.scheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
	auto staged = mounted ? staging.WriteAtomic(destination, bytes.value) : mounted;
	if (!staged)
	{
		return Result<MeshPtr>::Failure(staged.error);
	}
	SerializationContext context(&session);
	auto validated = LoadMeshResource(staging, destination, context);
	if (!validated)
	{
		return Result<MeshPtr>::Failure(validated.error);
	}
	auto written = files.WriteAtomic(destination, bytes.value);
	if (!written)
	{
		return Result<MeshPtr>::Failure(written.error);
	}
	written = files.WriteAtomic(sidecar.value, metaBytes);
	if (written)
	{
		written = database.Refresh();
	}
	if (!written)
	{
		auto sourceRollback = files.Remove(destination);
		auto sidecarRollback = files.Remove(sidecar.value);
		database.Refresh();
		return Result<MeshPtr>::Failure(written.error +
		                                ((!sourceRollback || !sidecarRollback) ? "; asset rollback failed" : ""));
	}
	copy.value = std::move(validated.value.mesh);
	Result<void> remembered;
	try
	{
		remembered = session.RememberMeshAsset(copy.value);
	}
	catch (const std::exception& error)
	{
		remembered = Result<void>::Failure(error.what());
	}
	if (!remembered)
	{
		auto sourceRollback = files.Remove(destination);
		auto sidecarRollback = files.Remove(sidecar.value);
		database.Refresh();
		return Result<MeshPtr>::Failure(remembered.error +
		                                ((!sourceRollback || !sidecarRollback) ? "; asset rollback failed" : ""));
	}
	return copy;
}
} // namespace Ursa::Editor
