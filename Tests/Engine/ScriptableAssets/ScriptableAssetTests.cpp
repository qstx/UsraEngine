#include <ScriptableAssets/ScriptableAsset.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Serialization/Archive.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/SerializableRegistry.hpp>
#include <iostream>
#include <stdexcept>

using namespace Ursa;

namespace
{
void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <typename T> T Take(Result<T> result)
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
} // namespace

int main()
{
	try
	{
		const auto guid = ResourceId::Generate();
		const nlohmann::json payload = {{"ScriptType", "test.Asset"},
		                                {"Version", 2},
		                                {"Fields", {{"exposure", 1.5}, {"futureField", {{"nested", {1, 2, 3}}}}}}};
		ScriptableAsset asset;
		Done(asset.SetDescription(guid, 1, "test.Asset", 2, payload));
		ArchiveWriter writer;
		Done(asset.Serialize(writer));
		SerializationContext context;
		for (bool binary : {false, true})
		{
			auto bytes = binary ? Take(BinaryArchive::Encode(writer)) : Take(JsonArchive::Encode(writer));
			auto reader = binary ? Take(BinaryArchive::Decode(bytes)) : Take(JsonArchive::Decode(bytes));
			ScriptableAsset copy;
			Done(copy.Deserialize(reader, context));
			Expect(copy.SourceGuid() == guid && copy.LocalFileId() == 1 && copy.TypeId() == "test.Asset" &&
			           copy.TypeVersion() == 2 && copy.Payload() == payload,
			       "Scriptable asset archive changed identity or unknown fields");
		}
		auto invalid = payload;
		invalid["Fields"]["sceneObject"] = {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", "scene-id"}}}};
		Expect(!asset.SetDescription(guid, 1, "test.Asset", 2, invalid), "Scene reference persisted");
		Expect(asset.Payload() == payload, "Failed edit changed the asset");
		invalid = payload;
		invalid["Fields"]["mesh"] = {
		    {"$ursaReference", {{"Kind", "Mesh"}, {"Id", ""}, {"Resource", {{"kind", "local"}, {"id", "temporary"}}}}}};
		Expect(!asset.SetDescription(guid, 1, "test.Asset", 2, invalid), "Local resource persisted");
		invalid = payload;
		invalid["ScriptType"] = "another.Type";
		Expect(!asset.SetDescription(guid, 1, "test.Asset", 2, invalid), "Payload identity mismatch accepted");
		Expect(!asset.SetDescription({}, 1, "test.Asset", 2, payload), "Empty source GUID accepted");
		Expect(!asset.SetDescription(guid, 0, "test.Asset", 2, payload), "Empty local file identity accepted");
		invalid = payload;
		invalid["FormatVersion"] = 99;
		Expect(!asset.SetDescription(guid, 1, "test.Asset", 2, invalid), "Unknown field archive format accepted");
		SerializableRegistry registry;
		Done(RegisterBuiltinSerializableTypes(registry));
		Expect(bool(registry.Create("ursa.ScriptableAsset", 1, context)) &&
		           !registry.Create("ursa.ScriptableAsset", 2, context),
		       "ScriptableAsset serialization version was not enforced");
		VirtualFileSystem files;
		Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		auto sourcePath = Take(VirtualPath::Parse("prj:Settings.ursaasset"));
		Done(SaveScriptableAssetSource(files, sourcePath, asset));
		const auto source = Take(files.ReadText(sourcePath));
		Expect(source.find("sourceGuid") == String::npos && source.find(guid.ToString()) == String::npos,
		       "Source file duplicated its metadata GUID");
		auto sourceCopy = Take(LoadScriptableAssetSource(files, sourcePath, guid));
		Expect(sourceCopy.Payload() == payload && sourceCopy.SourceGuid() == guid,
		       "Source did not use metadata identity");
		Expect(!LoadScriptableAssetSource(files, sourcePath, {}), "Source accepted an empty metadata GUID");
		const auto sourceBeforeInvalidSave = Take(files.Read(sourcePath));
		ScriptableAsset incomplete;
		Expect(!SaveScriptableAssetSource(files, sourcePath, incomplete) &&
		           Take(files.Read(sourcePath)) == sourceBeforeInvalidSave,
		       "Failed source save replaced the original bytes");
		const auto otherGuid = ResourceId::Generate();
		auto reference = [&](ResourceId target)
		{
			return nlohmann::json{{"$ursaReference",
			                       {{"Kind", "ScriptableObject"},
			                        {"Id", ""},
			                        {"Resource", {{"guid", target.ToString().c_str()}, {"localFileId", 1}}}}}};
		};
		auto firstPayload = payload;
		firstPayload["Fields"]["other"] = reference(otherGuid);
		firstPayload["Fields"]["shared"] = {reference(otherGuid), reference(otherGuid)};
		firstPayload["Fields"]["shader"] = {
		    {"$ursaReference",
		     {{"Kind", "Shader"}, {"Resource", {{"guid", otherGuid.ToString().c_str()}, {"localFileId", 0}}}}}};
		firstPayload["Fields"]["compute"] = {
		    {"$ursaReference",
		     {{"Kind", "ComputeShader"}, {"Resource", {{"guid", otherGuid.ToString().c_str()}, {"localFileId", 2}}}}}};
		Done(asset.SetDescription(guid, 1, "test.Asset", 2, firstPayload));
		ScriptableAsset other;
		auto otherPayload = payload;
		otherPayload["Fields"]["back"] = reference(guid);
		Done(other.SetDescription(otherGuid, 1, "test.Asset", 2, otherPayload));
		auto firstPath = Take(VirtualPath::Parse("prj:First.ursascriptableasset"));
		auto secondPath = Take(VirtualPath::Parse("prj:Second.ursascriptableasset"));
		Done(files.WriteAtomic(firstPath, Take(EncodeScriptableAssetArtifact(asset))));
		Done(files.WriteAtomic(secondPath, Take(EncodeScriptableAssetArtifact(other))));
		auto resources = Take(ResourceSession::Open(files,
		                                            [&](ResourceId id, UInt64 fileId)
		                                            {
			                                            if (fileId == 1 && id == guid)
			                                            {
				                                            return Result<VirtualPath>::Success(firstPath);
			                                            }
			                                            if (fileId == 1 && id == otherGuid)
			                                            {
				                                            return Result<VirtualPath>::Success(secondPath);
			                                            }
			                                            return Result<VirtualPath>::Failure("Missing fixture asset");
		                                            }));
		auto graph = Take(LoadScriptableAssetGraph(*resources, {guid, 1}));
		Expect(graph.assets.size() == 2 && graph.assets[0].Payload() == firstPayload &&
		           graph.assets[1].Payload() == otherPayload,
		       "Shared/cyclic graph was duplicated or truncated");
		Done(files.WriteAtomic(secondPath, Take(EncodeScriptableAssetArtifact(asset))));
		Expect(!LoadScriptableAssetGraph(*resources, {guid, 1}), "Incorrect dependency identity accepted");
		std::cout << "Scriptable asset archive tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
