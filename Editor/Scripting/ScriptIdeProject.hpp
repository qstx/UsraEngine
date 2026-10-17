#pragma once

#include <Scripting/ScriptProjectModel.hpp>
#include <functional>

namespace Ursa::Editor
{
struct ScriptIdeWorkspace
{
	std::filesystem::path solution;
	std::filesystem::path codeWorkspace;
	std::filesystem::path projectRoot;
	String fingerprint;
	UInt32 filesChanged = 0;
};

class ScriptIdeProject final
{
public:
	// The caller serializes access to its asset database. A freshness check prevents stale publication.
	static Result<ScriptIdeWorkspace> Generate(VirtualFileSystem& filesystem, const ScriptProjectInput& input,
	                                           const std::function<Result<String>()>& currentFingerprint = {});
};
} // namespace Ursa::Editor
