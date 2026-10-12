#include <Shader/ShaderSourceLibrary.hpp>

#include <algorithm>
#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>

namespace Ursa::Shader
{
namespace
{
Result<ResourceId> ReadShaderIdentity(const VirtualFileSystem& filesystem, const VirtualPath& source)
{
	auto path = VirtualPath::Parse(source.ToString() + ".meta");
	if (!path)
	{
		return Result<ResourceId>::Failure(path.error);
	}
	auto text = filesystem.ReadText(path.value);
	if (!text)
	{
		return Result<ResourceId>::Failure("Shader asset metadata is required at " + path.value.ToString() + ": " +
		                                   text.error);
	}
	auto metadata = nlohmann::json::parse(text.value.begin(), text.value.end(), nullptr, false);
	if (!metadata.is_object() || !metadata.contains("version") || !metadata["version"].is_number_integer() ||
	    metadata["version"] != 1 || !metadata.contains("folder") || !metadata["folder"].is_boolean() ||
	    metadata["folder"] != false || !metadata.contains("guid") || !metadata["guid"].is_string())
	{
		return Result<ResourceId>::Failure("Invalid Shader asset metadata at " + path.value.ToString());
	}
	auto id = ResourceId::Parse(ToUrsaString(metadata["guid"].get<std::string>()));
	return id && id.value ? id : Result<ResourceId>::Failure("Invalid Shader GUID at " + path.value.ToString());
}

bool HasSuffix(const String& value, const char* suffix)
{
	Ursa::Size length = 0;
	while (suffix[length] != '\0')
	{
		++length;
	}
	return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
}

String Trim(const String& value)
{
	Ursa::Size begin = 0;
	while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
	{
		++begin;
	}
	Ursa::Size end = value.size();
	while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
	{
		--end;
	}
	return value.substr(begin, end - begin);
}

Result<VirtualPath> ResolveIncludePath(const VirtualPath& owner, String includePath)
{
	if (includePath.find(':') != String::npos)
	{
		auto absolute = VirtualPath::Parse(includePath);
		if (!absolute)
		{
			return Result<VirtualPath>::Failure(absolute.error);
		}
		if (absolute.value.scheme != owner.scheme)
		{
			return Result<VirtualPath>::Failure("HLSL includes cannot cross VFS schemes: " + includePath);
		}
		return absolute;
	}
	for (char& character : includePath)
	{
		if (character == '\\')
		{
			character = '/';
		}
	}
	if (includePath.empty() || includePath.front() == '/')
	{
		return Result<VirtualPath>::Failure("HLSL include path must be relative or a VFS virtual path");
	}
	Vector<String> segments;
	const auto ownerSlash = owner.relative.rfind('/');
	if (ownerSlash != String::npos)
	{
		String directory = owner.relative.substr(0, ownerSlash);
		Ursa::Size cursor = 0;
		while (cursor < directory.size())
		{
			const auto slash = directory.find('/', cursor);
			const Ursa::Size end = slash == String::npos ? directory.size() : slash;
			segments.push_back(directory.substr(cursor, end - cursor));
			cursor = slash == String::npos ? directory.size() : slash + 1;
		}
	}
	Ursa::Size cursor = 0;
	while (cursor <= includePath.size())
	{
		const auto slash = includePath.find('/', cursor);
		const Ursa::Size end = slash == String::npos ? includePath.size() : slash;
		const String segment = includePath.substr(cursor, end - cursor);
		if (!segment.empty() && segment != ".")
		{
			if (segment == "..")
			{
				if (segments.empty())
				{
					return Result<VirtualPath>::Failure("HLSL include escapes its VFS scheme root: " + includePath);
				}
				segments.pop_back();
			}
			else
			{
				segments.push_back(segment);
			}
		}
		if (slash == String::npos)
		{
			break;
		}
		cursor = slash + 1;
	}
	String relative;
	for (const String& segment : segments)
	{
		if (!relative.empty())
		{
			relative.push_back('/');
		}
		relative += segment;
	}
	return VirtualPath::Parse(owner.scheme + ":" + relative);
}

Result<String> QuotedInclude(const String& line)
{
	const String trimmed = Trim(line);
	if (trimmed.size() < 8 || trimmed.substr(0, 8) != "#include")
	{
		return Result<String>::Success({});
	}
	Ursa::Size offset = 8;
	while (offset < trimmed.size() && (trimmed[offset] == ' ' || trimmed[offset] == '\t'))
	{
		++offset;
	}
	if (offset >= trimmed.size() || trimmed[offset] != '"')
	{
		return Result<String>::Failure("HLSL includes must use a quoted VFS path");
	}
	const Ursa::Size begin = ++offset;
	while (offset < trimmed.size() && trimmed[offset] != '"')
	{
		++offset;
	}
	if (offset == trimmed.size() || offset == begin)
	{
		return Result<String>::Failure("HLSL include path is empty or unterminated");
	}
	return Result<String>::Success(trimmed.substr(begin, offset - begin));
}

Result<void> VisitInclude(const VirtualFileSystem& filesystem, const VirtualPath& owner, const String& source,
                          Set<String>& active, Set<String>& visited, ShaderDependencyReport& report)
{
	Ursa::Size lineStart = 0;
	while (lineStart <= source.size())
	{
		const auto newline = source.find('\n', lineStart);
		const Ursa::Size lineEnd = newline == String::npos ? source.size() : newline;
		auto parsed = QuotedInclude(source.substr(lineStart, lineEnd - lineStart));
		if (!parsed)
		{
			return Result<void>::Failure(owner.ToString() + ": " + parsed.error);
		}
		if (!parsed.value.empty())
		{
			auto includePath = ResolveIncludePath(owner, parsed.value);
			if (!includePath)
			{
				return Result<void>::Failure(owner.ToString() + ": " + includePath.error);
			}
			const String key = includePath.value.ToString();
			if (active.find(key) != active.end())
			{
				return Result<void>::Failure("HLSL include cycle detected at " + key);
			}
			if (visited.find(key) == visited.end())
			{
				auto includedText = filesystem.ReadText(includePath.value);
				if (!includedText)
				{
					return Result<void>::Failure("Cannot read HLSL include " + key + ": " + includedText.error);
				}
				active.insert(key);
				auto nested = VisitInclude(filesystem, includePath.value, includedText.value, active, visited, report);
				active.erase(key);
				if (!nested)
				{
					return nested;
				}
				visited.insert(key);
				report.includes.push_back(std::move(includePath.value));
			}
		}
		if (newline == String::npos)
		{
			break;
		}
		lineStart = newline + 1;
	}
	return {};
}

void CollectPrograms(const ShaderSourceDocument& shader, Vector<const ProgramBlock*>& programs)
{
	for (const ProgramBlock& include : shader.includes)
	{
		programs.push_back(&include);
	}
	for (const SubShaderDeclaration& subShader : shader.subShaders)
	{
		for (const ProgramBlock& include : subShader.includes)
		{
			programs.push_back(&include);
		}
		for (const PassDeclaration& pass : subShader.passes)
		{
			for (const ProgramBlock& program : pass.programs)
			{
				programs.push_back(&program);
			}
		}
	}
}

bool HasPass(const ShaderSourceDocument& shader, const String& passName)
{
	for (const SubShaderDeclaration& subShader : shader.subShaders)
	{
		for (const PassDeclaration& pass : subShader.passes)
		{
			if (pass.name == passName)
			{
				return true;
			}
		}
	}
	return false;
}

Result<String> ShaderNameFromUsePass(const String& reference)
{
	const auto slash = reference.rfind('/');
	if (slash == String::npos || slash == 0 || slash + 1 >= reference.size())
	{
		return Result<String>::Failure("UsePass must use \"ShaderName/PassName\": " + reference);
	}
	return Result<String>::Success(reference.substr(0, slash));
}
} // namespace

Result<ShaderSourceLibrary> ShaderSourceLibrary::Load(const VirtualFileSystem& filesystem, const String& scheme,
                                                      const VirtualPath& requestedSource)
{
	auto entries = filesystem.List(scheme);
	if (!entries)
	{
		return Result<ShaderSourceLibrary>::Failure(entries.error);
	}
	ShaderSourceLibrary library;
	Map<String, String> ids;
	for (const VirtualFileEntry& entry : entries.value)
	{
		if (!HasSuffix(entry.path.FileName(), ".ursashader"))
		{
			continue;
		}
		auto text = filesystem.ReadText(entry.path);
		if (!text)
		{
			return Result<ShaderSourceLibrary>::Failure(text.error);
		}
		auto parsed = ParseShaderSource(entry.path.ToString(), text.value);
		if (!parsed)
		{
			if (!requestedSource.relative.empty() && entry.path != requestedSource)
			{
				continue;
			}
			return Result<ShaderSourceLibrary>::Failure(parsed.error);
		}
		auto identity = ReadShaderIdentity(filesystem, entry.path);
		if (!identity)
		{
			return Result<ShaderSourceLibrary>::Failure(identity.error);
		}
		parsed.value.id = identity.value;
		const String id = identity.value.ToString();
		const auto existingId = ids.find(id);
		if (existingId != ids.end())
		{
			return Result<ShaderSourceLibrary>::Failure("Duplicate Shader resource ID " + id + " in " +
			                                            existingId->second + " and " + entry.path.ToString());
		}
		ids.emplace(id, entry.path.ToString());
		if (library.shaders.find(parsed.value.name) != library.shaders.end())
		{
			return Result<ShaderSourceLibrary>::Failure("Duplicate Shader name " + parsed.value.name + " at " +
			                                            entry.path.ToString());
		}
		String shaderName = parsed.value.name;
		library.shaders.emplace(std::move(shaderName), ShaderSourceAsset{entry.path, std::move(parsed.value)});
	}
	return Result<ShaderSourceLibrary>::Success(std::move(library));
}

const ShaderSourceAsset* ShaderSourceLibrary::Find(const String& shaderName) const
{
	const auto found = shaders.find(shaderName);
	return found == shaders.end() ? nullptr : &found->second;
}

Vector<const ShaderSourceAsset*> ShaderSourceLibrary::Sources() const
{
	Vector<const ShaderSourceAsset*> result;
	result.reserve(shaders.size());
	for (const auto& shader : shaders)
	{
		result.push_back(&shader.second);
	}
	std::sort(result.begin(), result.end(),
	          [](const auto* left, const auto* right) { return left->path.ToString() < right->path.ToString(); });
	return result;
}

Result<ShaderDependencyReport> ShaderSourceLibrary::ResolveDependencies(const VirtualFileSystem& filesystem,
                                                                        const String& shaderName) const
{
	const auto root = shaders.find(shaderName);
	if (root == shaders.end())
	{
		return Result<ShaderDependencyReport>::Failure("Shader source was not found: " + shaderName);
	}
	Map<String, Vector<String>> edges;
	ShaderDependencyReport report;
	auto resolveEdges = [&](const String& name) -> Result<void>
	{
		if (edges.find(name) != edges.end())
		{
			return {};
		}
		const auto entry = shaders.find(name);
		if (entry == shaders.end())
		{
			return Result<void>::Failure("Shader reference was not found: " + name);
		}
		const ShaderSourceDocument& shader = entry->second.source;
		Vector<String> dependencies;
		if (!shader.fallback.empty())
		{
			if (shaders.find(shader.fallback) == shaders.end())
			{
				return Result<void>::Failure(shader.sourcePath + ": missing fallback Shader " + shader.fallback);
			}
			dependencies.push_back(shader.fallback);
		}
		for (const SubShaderDeclaration& subShader : shader.subShaders)
		{
			for (const String& reference : subShader.reusedPasses)
			{
				auto targetName = ShaderNameFromUsePass(reference);
				if (!targetName)
				{
					return Result<void>::Failure(shader.sourcePath + ": " + targetName.error);
				}
				const auto target = shaders.find(targetName.value);
				if (target == shaders.end())
				{
					return Result<void>::Failure(shader.sourcePath + ": missing UsePass Shader " + targetName.value);
				}
				const auto slash = reference.rfind('/');
				const String passName = reference.substr(slash + 1);
				if (!HasPass(target->second.source, passName))
				{
					return Result<void>::Failure(shader.sourcePath + ": missing Pass " + passName + " in Shader " +
					                             targetName.value);
				}
				if (std::find(dependencies.begin(), dependencies.end(), targetName.value) == dependencies.end())
				{
					dependencies.push_back(std::move(targetName.value));
				}
			}
		}
		edges.emplace(name, std::move(dependencies));
		return {};
	};
	Set<String> visiting;
	Set<String> visitedShaders;
	Set<String> reportedReferences;
	Vector<String> orderedShaders;
	auto visitShader = [&](auto&& self, const String& name) -> Result<void>
	{
		if (visiting.find(name) != visiting.end())
		{
			return Result<void>::Failure("Shader reference cycle detected at " + name);
		}
		if (visitedShaders.find(name) != visitedShaders.end())
		{
			return {};
		}
		visiting.insert(name);
		auto resolved = resolveEdges(name);
		if (!resolved)
		{
			return resolved;
		}
		for (const String& dependency : edges[name])
		{
			if (reportedReferences.insert(dependency).second)
			{
				report.shaderReferences.push_back(dependency);
			}
			auto nested = self(self, dependency);
			if (!nested)
			{
				return nested;
			}
		}
		visiting.erase(name);
		visitedShaders.insert(name);
		orderedShaders.push_back(name);
		return {};
	};
	auto referencesValid = visitShader(visitShader, shaderName);
	if (!referencesValid)
	{
		return Result<ShaderDependencyReport>::Failure(referencesValid.error);
	}
	Set<String> activeIncludes;
	Set<String> visitedIncludes;
	for (const String& orderedName : orderedShaders)
	{
		const ShaderSourceAsset& asset = shaders.find(orderedName)->second;
		activeIncludes.insert(asset.path.ToString());
		Vector<const ProgramBlock*> programs;
		CollectPrograms(asset.source, programs);
		for (const ProgramBlock* program : programs)
		{
			auto includes =
			    VisitInclude(filesystem, asset.path, program->source, activeIncludes, visitedIncludes, report);
			if (!includes)
			{
				return Result<ShaderDependencyReport>::Failure(includes.error);
			}
		}
		activeIncludes.erase(asset.path.ToString());
	}
	return Result<ShaderDependencyReport>::Success(std::move(report));
}
} // namespace Ursa::Shader
