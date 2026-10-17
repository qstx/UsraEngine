#include <Scripting/Bindings/SerializationServices.hpp>
#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Serialization/RenderResourceReferences.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>

namespace Ursa::Scripting::Bindings
{
namespace
{
SerializationServices::Archive WriteReference(BindingCall& call, const ArchiveWriter& writer)
{
	const auto json = TakeBindingResult(ArchiveToJson(writer)).at("value");
	return {1, call.arena.StringValue(ToUrsaString(json.dump()))};
}

ArchiveReader ReadReference(const SerializationServices::Archive& archive)
{
	if (archive.F_Version != 1)
	{
		throw std::runtime_error("Unsupported resource reference archive version");
	}
	const auto json = nlohmann::json::parse(ToStdString(ReadString(archive.F_Json)));
	return TakeBindingResult(ArchiveFromJson(nlohmann::json{{"value", json}}));
}

SerializationContext& RestoreContext(SerializationContext& fallback)
{
	auto* active = SerializationContext::Active();
	if (active && !active->ResourceLoader())
	{
		auto references = active->Service<RenderResourceReferences>();
		if (references)
		{
			fallback.Provide<RenderResourceReferences>(*references.value);
		}
	}
	return active && active->ResourceLoader() ? *active : fallback;
}
} // namespace

Result<SerializationServices::Archive> SerializationServices::SnapshotMesh(BindingCall& call, const Handle& mesh)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto value = TakeBindingResult(api.ResolveMesh(ReadManagedHandle(mesh)));
	ArchiveWriter writer;
	writer.SetContext(SerializationContext::Active());
	if (!value->AssetIdentity().guid && !writer.Context())
	{
		const auto json = nlohmann::json{{"kind", "local"}, {"id", ToStdString(value->LocalIdentity().ToString())}};
		return Result<Archive>::Success({1, call.arena.StringValue(ToUrsaString(json.dump()))});
	}
	CompleteBindingResult(WriteMeshBinding(writer, "value", value));
	return Result<Archive>::Success(WriteReference(call, writer));
}

Result<SerializationServices::Archive> SerializationServices::SnapshotMaterial(BindingCall& call,
                                                                               const Handle& material)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto value = TakeBindingResult(api.ResolveMaterial(ReadManagedHandle(material)));
	ArchiveWriter writer;
	writer.SetContext(SerializationContext::Active());
	if (!value->AssetIdentity().guid && !writer.Context())
	{
		const auto json = nlohmann::json{{"kind", "local"}, {"id", ToStdString(value->LocalIdentity().ToString())}};
		return Result<Archive>::Success({1, call.arena.StringValue(ToUrsaString(json.dump()))});
	}
	CompleteBindingResult(WriteMaterialBinding(writer, "value", value));
	return Result<Archive>::Success(WriteReference(call, writer));
}

Result<SerializationServices::Handle> SerializationServices::RestoreMesh(BindingCall& call, const Archive& archive)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto reader = ReadReference(archive);
	SerializationContext fallback(api.resources);
	const auto value = TakeBindingResult(ReadMeshBinding(reader, "value", RestoreContext(fallback)));
	return Result<Handle>::Success(WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterMesh(value))));
}

Result<SerializationServices::Handle> SerializationServices::RestoreMaterial(BindingCall& call, const Archive& archive)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto reader = ReadReference(archive);
	SerializationContext fallback(api.resources);
	const auto value = TakeBindingResult(ReadMaterialBinding(reader, "value", RestoreContext(fallback)));
	return Result<Handle>::Success(WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterMaterial(value))));
}

Result<Wire::Value_Ursa_ScriptableAssetGraphArchive>
SerializationServices::LoadScriptableAssetGraph(BindingCall& call, const Wire::Value_Ursa_NativeAssetReference& root)
{
	auto& api = RuntimeServices::SceneApi(call);
	if (!api.resources)
	{
		return Result<Wire::Value_Ursa_ScriptableAssetGraphArchive>::Failure(
		    "ScriptableAsset loading requires a ResourceSession");
	}
	const auto graph = TakeBindingResult(Ursa::LoadScriptableAssetGraph(*api.resources, ReadAssetValue(root)));
	auto json = nlohmann::json::array();
	for (const auto& asset : graph.assets)
	{
		json.push_back({{"guid", ToStdString(asset.SourceGuid().ToString())},
		                {"localFileId", asset.LocalFileId()},
		                {"typeId", ToStdString(asset.TypeId())},
		                {"typeVersion", asset.TypeVersion()},
		                {"payload", asset.Payload()}});
	}
	return Result<Wire::Value_Ursa_ScriptableAssetGraphArchive>::Success(
	    {1, call.arena.StringValue(ToUrsaString(json.dump()))});
}
} // namespace Ursa::Scripting::Bindings
