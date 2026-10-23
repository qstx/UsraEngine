#include <Assets/AssetImportRules.hpp>
#include <Assets/ModelImporter.hpp>

#include <Core/StringInterop.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>

namespace Ursa::Editor
{
namespace
{
Result<String> NormalizeExtension(String extension)
{
	if (extension.empty())
	{
		return Result<String>::Failure("Importer extension must not be empty");
	}
	if (extension.front() != '.')
	{
		extension.insert(extension.begin(), '.');
	}
	if (extension.size() == 1)
	{
		return Result<String>::Failure("Importer extension needs a file suffix");
	}
	for (Size index = 1; index < extension.size(); ++index)
	{
		auto& character = extension[index];
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character - 'A' + 'a');
		}
		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
		      character == '-'))
		{
			return Result<String>::Failure("Invalid importer extension: " + extension);
		}
	}
	return Result<String>::Success(std::move(extension));
}

Vector<AssetImporterDescriptor> NativeImporters()
{
	return {{"ursa.Default",
	         1,
	         {".ursamat", ".ursaprefab", ".ursascene", ".ursamesh", ".ursatex", ".ursashaderasset", ".ursamodel", ".cs",
	          ".dll", ".meta"},
	         {}},
	        {"ursa.Model", ModelImporter::Version, ModelImporter::SupportedExtensions(), {}},
	        {"ursa.Shader", 3, {".ursashader", ".compute"}, {}},
	        {"ursa.ScriptableAsset", 1, {".ursaasset"}, {}},
	        {"ursa.Texture", 1, {".png", ".jpg", ".jpeg"}, {}}};
}

UInt32 ProcessorMask(const String& importer)
{
	return importer == "ursa.Texture" ? 1u : importer == "ursa.Model" ? 2u : 0u;
}
} // namespace

Result<String> ImportScriptType::StableIdentity() const
{
	if (!sourceGuid || !localFileId)
	{
		return Result<String>::Failure("Import script type needs source GUID and local file ID");
	}
	return Result<String>::Success("script:" + sourceGuid.ToString() + ":" + ToUrsaString(std::to_string(localFileId)));
}

Result<ImportScriptType> ImportScriptType::FromIdentity(const String& identity)
{
	const auto separator = identity.rfind(':');
	if (identity.compare(0, 7, "script:") != 0 || separator == String::npos || separator <= 7)
	{
		return Result<ImportScriptType>::Failure("Invalid script importer identity: " + identity);
	}
	auto guid = ResourceId::Parse(identity.substr(7, separator - 7));
	UInt64 localId = 0;
	const auto parsed = std::from_chars(identity.data() + separator + 1, identity.data() + identity.size(), localId);
	if (!guid || !guid.value || !localId || parsed.ec != std::errc{} || parsed.ptr != identity.data() + identity.size())
	{
		return Result<ImportScriptType>::Failure("Invalid script importer identity: " + identity);
	}
	ImportScriptType result{guid.value, localId, {}};
	if (result.StableIdentity().value != identity)
	{
		return Result<ImportScriptType>::Failure("Script importer identity is not canonical: " + identity);
	}
	return Result<ImportScriptType>::Success(std::move(result));
}

Result<AssetImportRules> AssetImportRules::Create(Vector<ScriptedImporterDescriptor> scriptImporters,
                                                  Vector<AssetPostprocessorDescriptor> scriptPostprocessors,
                                                  String compiledFingerprint, UInt64 selectedGeneration)
{
	if (!selectedGeneration ||
	    ((!scriptImporters.empty() || !scriptPostprocessors.empty()) && compiledFingerprint.empty()))
	{
		return Result<AssetImportRules>::Failure("Import rules require a generation and compiled script fingerprint");
	}
	AssetImportRules result;
	result.generation = selectedGeneration;
	result.codeFingerprint = std::move(compiledFingerprint);
	result.importers = NativeImporters();
	Set<String> types;
	for (auto& script : scriptImporters)
	{
		auto identity = script.type.StableIdentity();
		if (!identity || script.type.clrTypeName.empty() || !script.version || script.extensions.empty())
		{
			return Result<AssetImportRules>::Failure(
			    identity ? "Scripted importer needs version and extensions: " + identity.value : identity.error);
		}
		if (!types.insert(identity.value).second)
		{
			return Result<AssetImportRules>::Failure("Duplicate importer type: " + identity.value);
		}
		result.importers.push_back({std::move(identity.value), script.version, std::move(script.extensions),
		                            std::move(script.type), std::move(script.defaultSettingsJson)});
	}
	std::sort(result.importers.begin(), result.importers.end(),
	          [](const auto& left, const auto& right) { return left.identity < right.identity; });
	Map<String, Vector<String>> owners;
	for (Size index = 0; index < result.importers.size(); ++index)
	{
		auto& importer = result.importers[index];
		Set<String> uniqueExtensions;
		for (auto& extension : importer.extensions)
		{
			auto normalized = NormalizeExtension(extension);
			if (!normalized || !uniqueExtensions.insert(normalized.value).second)
			{
				return Result<AssetImportRules>::Failure(
				    normalized ? "Repeated extension in importer " + importer.identity : normalized.error);
			}
			extension = std::move(normalized.value);
			owners[extension].push_back(importer.identity +
			                            (importer.scriptType ? " (" + importer.scriptType->clrTypeName + ")" : ""));
			result.importersByExtension[extension] = index;
		}
		std::sort(importer.extensions.begin(), importer.extensions.end());
	}
	String conflicts;
	for (const auto& entry : owners)
	{
		if (entry.second.size() > 1)
		{
			conflicts += "Conflicting importers for " + entry.first + ":";
			for (const auto& owner : entry.second)
			{
				conflicts += " " + owner;
			}
			conflicts += "\n";
		}
	}
	if (!conflicts.empty())
	{
		return Result<AssetImportRules>::Failure(std::move(conflicts));
	}
	Set<String> processorTypes;
	for (const auto& processor : scriptPostprocessors)
	{
		auto identity = processor.type.StableIdentity();
		const auto mask = static_cast<UInt32>(processor.targets);
		if (!identity || processor.type.clrTypeName.empty() || mask == 0 || mask > 3)
		{
			return Result<AssetImportRules>::Failure(identity ? "Invalid postprocessor target mask: " + identity.value
			                                                  : identity.error);
		}
		if (!processorTypes.insert(identity.value).second || types.find(identity.value) != types.end())
		{
			return Result<AssetImportRules>::Failure("Duplicate import rule type: " + identity.value);
		}
	}
	std::sort(scriptPostprocessors.begin(), scriptPostprocessors.end(),
	          [](const auto& left, const auto& right)
	          {
		          return left.order != right.order
		                     ? left.order < right.order
		                     : left.type.StableIdentity().value < right.type.StableIdentity().value;
	          });
	result.postprocessors = std::move(scriptPostprocessors);
	return Result<AssetImportRules>::Success(std::move(result));
}

Result<const AssetImporterDescriptor*> AssetImportRules::FindImporter(const String& extension,
                                                                      const String& requiredIdentity) const
{
	if (!generation)
	{
		return Result<const AssetImporterDescriptor*>::Failure("Import rules snapshot is not initialized");
	}
	const AssetImporterDescriptor* selected = nullptr;
	if (!extension.empty())
	{
		auto normalized = NormalizeExtension(extension);
		if (!normalized)
		{
			return Result<const AssetImporterDescriptor*>::Failure(normalized.error);
		}
		const auto found = importersByExtension.find(normalized.value);
		if (found != importersByExtension.end())
		{
			selected = &importers[found->second];
		}
	}
	if (!selected)
	{
		selected = &*std::find_if(importers.begin(), importers.end(),
		                          [](const auto& item) { return item.identity == "ursa.Default"; });
	}
	if (!requiredIdentity.empty() && selected->identity != requiredIdentity)
	{
		return Result<const AssetImporterDescriptor*>::Failure("Required importer " + requiredIdentity +
		                                                       " is unavailable for extension " + extension);
	}
	return Result<const AssetImporterDescriptor*>::Success(selected);
}

Vector<AssetPostprocessorDescriptor> AssetImportRules::PostprocessorsFor(const String& importer,
                                                                         const String& sourceScheme) const
{
	Vector<AssetPostprocessorDescriptor> result;
	if (sourceScheme != projectScheme)
	{
		return result;
	}
	const auto mask = ProcessorMask(importer);
	for (const auto& processor : postprocessors)
	{
		if ((static_cast<UInt32>(processor.targets) & mask) != 0)
		{
			result.push_back(processor);
		}
	}
	return result;
}

Result<String> AssetImportRules::FingerprintFor(const String& extension, const String& sourceScheme,
                                                const String& requiredIdentity) const
{
	auto selected = FindImporter(extension, requiredIdentity);
	if (!selected)
	{
		return Result<String>::Failure(selected.error);
	}
	const auto& importer = *selected.value;
	const auto chain = PostprocessorsFor(importer.identity, sourceScheme);
	nlohmann::json value{{"format", "Ursa.ImportRules.1"},
	                     {"importer", ToStdString(importer.identity)},
	                     {"version", importer.version},
	                     {"processors", nlohmann::json::array()}};
	if (importer.scriptType || !chain.empty())
	{
		value["code"] = ToStdString(codeFingerprint);
	}
	for (const auto& processor : chain)
	{
		value["processors"].push_back({{"type", ToStdString(processor.type.StableIdentity().value)},
		                               {"version", processor.version},
		                               {"order", processor.order}});
	}
	return Result<String>::Success(ToUrsaString(value.dump()));
}

Result<UInt64> StableImportObjectId(const String& importerIdentity, const String& outputKey)
{
	if (importerIdentity.empty() || outputKey.empty())
	{
		return Result<UInt64>::Failure("Imported objects require importer identity and nonempty output key");
	}
	UInt64 hash = 14695981039346656037ull;
	const auto add = [&hash](const String& text)
	{
		UInt64 size = text.size();
		for (UInt32 index = 0; index < 8; ++index)
		{
			hash = (hash ^ static_cast<UInt8>(size >> (index * 8))) * 1099511628211ull;
		}
		for (const unsigned char character : text)
		{
			hash = (hash ^ character) * 1099511628211ull;
		}
	};
	add("Ursa.ImportObject.1");
	add(importerIdentity);
	add(outputKey);
	return Result<UInt64>::Success(hash ? hash : 1);
}
} // namespace Ursa::Editor
