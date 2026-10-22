#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Core/StringInterop.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <algorithm>

namespace Ursa::Rendering
{
namespace
{
Result<void> ValidateIdentity(const GraphicsSettings& settings)
{
	const auto& reference = settings.renderPipeline;
	if (bool(reference.guid) != (reference.localFileId != 0))
	{
		return Result<void>::Failure("Graphics pipeline requires both an asset GUID and a nonzero local file ID");
	}
	return {};
}
} // namespace

Result<Vector<UInt8>> EncodeGraphicsSettings(const GraphicsSettings& settings)
{
	auto valid = ValidateIdentity(settings);
	if (!valid)
	{
		return Result<Vector<UInt8>>::Failure(valid.error);
	}
	nlohmann::json document{{"format", "UrsaGraphicsSettings"},
	                        {"version", 1u},
	                        {"renderPipeline", settings.renderPipeline.guid
	                                               ? AssetReferenceToJson(settings.renderPipeline)
	                                               : nlohmann::json(nullptr)}};
	const auto text = document.dump(2);
	return Result<Vector<UInt8>>::Success(Vector<UInt8>(text.data(), text.data() + text.size()));
}

Result<GraphicsSettings> ReadGraphicsSettings(const VirtualFileSystem& files, bool runtime)
{
	const auto path = VirtualPath::Parse(runtime ? graphicsSettingsRuntime : graphicsSettingsSource).value;
	if (!files.IsMounted(path.scheme))
	{
		return Result<GraphicsSettings>::Success({});
	}
	auto exists = files.Exists(path);
	if (!exists)
	{
		return Result<GraphicsSettings>::Failure(exists.error);
	}
	if (!exists.value)
	{
		return Result<GraphicsSettings>::Success({});
	}
	auto text = files.ReadText(path);
	if (!text)
	{
		return Result<GraphicsSettings>::Failure(text.error);
	}
	try
	{
		const auto document = nlohmann::json::parse(ToStdString(text.value));
		if (!document.is_object() || document.value("format", std::string{}) != "UrsaGraphicsSettings" ||
		    !document.contains("version") || document.at("version") != 1 || !document.contains("renderPipeline"))
		{
			return Result<GraphicsSettings>::Failure("Invalid Graphics settings schema at " + path.ToString());
		}
		GraphicsSettings settings;
		if (!document.at("renderPipeline").is_null())
		{
			auto reference = AssetReferenceFromJson(document.at("renderPipeline"), path.ToString() + ".renderPipeline");
			if (!reference)
			{
				return Result<GraphicsSettings>::Failure(reference.error);
			}
			settings.renderPipeline = reference.value;
		}
		auto valid = ValidateIdentity(settings);
		return valid ? Result<GraphicsSettings>::Success(settings) : Result<GraphicsSettings>::Failure(valid.error);
	}
	catch (const std::exception& error)
	{
		return Result<GraphicsSettings>::Failure("Invalid Graphics settings at " + path.ToString() + ": " +
		                                         ToUrsaString(error.what()));
	}
}

Result<void> SaveGraphicsSettings(VirtualFileSystem& files, const GraphicsSettings& settings,
                                  const PipelineAssetValidator& validator)
{
	auto bytes = EncodeGraphicsSettings(settings);
	if (!bytes)
	{
		return Result<void>::Failure(bytes.error);
	}
	if (settings.renderPipeline.guid)
	{
		if (!validator)
		{
			return Result<void>::Failure("A pipeline asset validator is required for a nonempty Graphics selection");
		}
		auto valid = validator(settings.renderPipeline);
		if (!valid)
		{
			return valid;
		}
	}
	return files.WriteAtomic(VirtualPath::Parse(graphicsSettingsSource).value, bytes.value);
}

Result<void> ValidatePipelineAsset(const VirtualFileSystem& files, const ResourceSession::AssetPathResolver& resolver,
                                   const Vector<ScriptTypeDescriptor>& types, AssetReference reference)
{
	auto valid = ValidateIdentity({reference});
	if (!valid || !reference.guid)
	{
		return valid;
	}
	if (!resolver)
	{
		return Result<void>::Failure("Graphics selection requires an asset resolver");
	}
	auto path = resolver(reference.guid, 0);
	if (!path)
	{
		return Result<void>::Failure("Missing Graphics pipeline " + reference.guid.ToString() + ": " + path.error);
	}
	const auto load = [&]() -> Result<ScriptableAsset>
	{
		auto size = files.Size(path.value);
		if (!size || size.value > 16ull * 1024 * 1024)
		{
			return Result<ScriptableAsset>::Failure("Graphics pipeline asset is missing or exceeds its size budget");
		}
		auto bytes = files.Read(path.value);
		if (!bytes)
		{
			return Result<ScriptableAsset>::Failure(bytes.error);
		}
		auto imported = DecodeScriptableAssetArtifact(bytes.value);
		if (imported)
		{
			return imported;
		}
		if (PathFromUrsaString(path.value.relative).extension() == ".ursaasset")
		{
			return LoadScriptableAssetSource(files, path.value, reference.guid);
		}
		return Result<ScriptableAsset>::Failure("Graphics selection is not a RenderPipelineAsset: " +
		                                        path.value.ToString());
	};
	auto asset = load();
	if (!asset)
	{
		return Result<void>::Failure(asset.error);
	}
	const auto type = std::find_if(types.begin(), types.end(),
	                               [&](const auto& record) { return record.scriptType == asset.value.TypeId(); });
	const auto& payload = asset.value.Payload();
	if (asset.value.SourceGuid() != reference.guid || asset.value.LocalFileId() != reference.localFileId ||
	    type == types.end() || !type->isRenderPipeline || type->kind != ScriptTypeKind::ScriptableObject ||
	    type->group != ScriptAssemblyGroup::Game || type->version != asset.value.TypeVersion() ||
	    !payload.contains("SourceGuid") || !payload["SourceGuid"].is_string() || !payload.contains("LocalFileId") ||
	    !payload["LocalFileId"].is_number_unsigned() ||
	    payload["SourceGuid"].get<std::string>() != ToStdString(type->sourceGuid.ToString()) ||
	    payload["LocalFileId"].get<UInt64>() != type->localFileId)
	{
		return Result<void>::Failure("Graphics selection requires a current Game RenderPipelineAsset: " +
		                             path.value.ToString());
	}
	return {};
}
} // namespace Ursa::Rendering
