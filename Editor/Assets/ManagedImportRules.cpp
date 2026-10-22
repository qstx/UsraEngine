#include <Assets/ManagedImportRules.hpp>

#if defined(URSA_ENABLE_DOTNET)
namespace Ursa::Editor
{
Result<AssetImportRules> DiscoverManagedImportRules(Scripting::DotNetHost& host, UInt64 session,
                                                    String compiledFingerprint, UInt64 generation)
{
	auto native = AssetImportRules::Create({}, {}, {}, generation);
	if (!native)
	{
		return native;
	}
	Vector<String> reserved;
	for (const auto& importer : native.value.Importers())
	{
		reserved.insert(reserved.end(), importer.extensions.begin(), importer.extensions.end());
	}
	auto described = host.PrepareScriptedImporters(session, reserved);
	if (!described)
	{
		return Result<AssetImportRules>::Failure(described.error);
	}
	Vector<ScriptedImporterDescriptor> importers;
	for (const auto& importer : described.value)
	{
		auto guid = ResourceId::Parse(importer.sourceGuid);
		if (!guid)
		{
			return Result<AssetImportRules>::Failure(guid.error);
		}
		importers.push_back({{guid.value, importer.localFileId, importer.clrTypeName},
		                     importer.version,
		                     importer.extensions,
		                     importer.defaultSettingsJson});
	}
	auto describedProcessors = host.DescribeAssetPostprocessors(session);
	if (!describedProcessors)
	{
		return Result<AssetImportRules>::Failure(describedProcessors.error);
	}
	Vector<AssetPostprocessorDescriptor> processors;
	for (const auto& processor : describedProcessors.value)
	{
		auto guid = ResourceId::Parse(processor.sourceGuid);
		if (!guid)
		{
			return Result<AssetImportRules>::Failure(guid.error);
		}
		processors.push_back({{guid.value, processor.localFileId, processor.clrTypeName},
		                      processor.version,
		                      processor.order,
		                      static_cast<AssetPostprocessorTarget>(processor.targets)});
	}
	return AssetImportRules::Create(std::move(importers), std::move(processors), std::move(compiledFingerprint),
	                                generation);
}
} // namespace Ursa::Editor
#endif
