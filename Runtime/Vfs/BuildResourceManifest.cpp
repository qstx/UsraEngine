#include <Vfs/BuildResourceManifest.hpp>
#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

Json EncodeFiles(const Vector<BuildResourceFile>& records)
{
	Json result = Json::array();
	for (const auto& record : records)
	{
		result.push_back({{"path", ToStdString(record.path)},
		                  {"fingerprint", ToStdString(record.fingerprint)},
		                  {"size", record.size}});
	}
	return result;
}

Result<Vector<BuildResourceFile>> DecodeFiles(const Json& records)
{
	if (!records.is_array())
	{
		return Result<Vector<BuildResourceFile>>::Failure("Build resource file records are not an array");
	}
	Vector<BuildResourceFile> result;
	Set<String> paths;
	for (const auto& record : records)
	{
		if (!record.is_object() || !record.contains("path") || !record["path"].is_string() ||
		    !record.contains("fingerprint") || !record["fingerprint"].is_string() || !record.contains("size") ||
		    !record["size"].is_number_unsigned())
		{
			return Result<Vector<BuildResourceFile>>::Failure("Invalid build resource file record");
		}
		const String relative = ToUrsaString(record["path"].get<std::string>());
		auto path = VirtualPath::Parse("built:" + relative);
		if (!path || path.value.relative != relative || relative == "BuildManifest.json" ||
		    !paths.insert(relative).second)
		{
			return Result<Vector<BuildResourceFile>>::Failure("Invalid or duplicate build resource path: " + relative);
		}
		const String fingerprint = ToUrsaString(record["fingerprint"].get<std::string>());
		if (fingerprint.size() != 16 || fingerprint.find_first_not_of("0123456789abcdef") != String::npos)
		{
			return Result<Vector<BuildResourceFile>>::Failure("Invalid build resource fingerprint: " + relative);
		}
		result.push_back({relative, fingerprint, record["size"].get<UInt64>()});
	}
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.path < right.path; });
	return Result<Vector<BuildResourceFile>>::Success(std::move(result));
}
} // namespace

String BuildResourceFingerprint(const Vector<UInt8>& bytes)
{
	UInt64 hash = 14695981039346656037ull;
	for (UInt8 value : bytes)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	std::ostringstream output;
	output << std::hex << std::setfill('0') << std::setw(16) << hash;
	return ToUrsaString(output.str());
}

Result<Vector<BuildResourceFile>> SnapshotBuildResourceFiles(const VirtualFileSystem& files, const String& scheme)
{
	auto listed = files.List(scheme);
	if (!listed)
	{
		return Result<Vector<BuildResourceFile>>::Failure(listed.error);
	}
	Vector<BuildResourceFile> result;
	for (const auto& entry : listed.value)
	{
		if (entry.path.relative == "BuildManifest.json")
		{
			continue;
		}
		auto bytes = files.Read(entry.path);
		if (!bytes)
		{
			return Result<Vector<BuildResourceFile>>::Failure(bytes.error);
		}
		result.push_back(
		    {entry.path.relative, BuildResourceFingerprint(bytes.value), static_cast<UInt64>(bytes.value.size())});
	}
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.path < right.path; });
	return Result<Vector<BuildResourceFile>>::Success(std::move(result));
}

bool EqualBuildResourceFiles(const Vector<BuildResourceFile>& left, const Vector<BuildResourceFile>& right)
{
	if (left.size() != right.size())
	{
		return false;
	}
	for (Size position = 0; position < left.size(); ++position)
	{
		if (left[position].path != right[position].path || left[position].size != right[position].size ||
		    left[position].fingerprint != right[position].fingerprint)
		{
			return false;
		}
	}
	return true;
}

Result<BuildResourceManifest> ReadBuildResourceManifest(const VirtualFileSystem& files, const String& scheme)
{
	const auto path = VirtualPath::Parse(scheme + ":BuildManifest.json");
	if (!path)
	{
		return Result<BuildResourceManifest>::Failure(path.error);
	}
	auto text = files.ReadText(path.value);
	if (!text)
	{
		return Result<BuildResourceManifest>::Failure(
		    "Missing engine build manifest: " + path.value.ToString() +
		    "; run UrsaAssetImport --project <project> and use its prepared content view. " + text.error);
	}
	try
	{
		const Json document = Json::parse(ToStdString(text.value));
		if (document.value("format", std::string{}) != "UrsaBuildResources" || document.value("version", 0u) != 1 ||
		    !document.at("managed").is_boolean())
		{
			return Result<BuildResourceManifest>::Failure("Invalid engine build manifest at " + path.value.ToString());
		}
		BuildResourceManifest manifest;
		manifest.configuration = ToUrsaString(document.at("configuration").get<std::string>());
		manifest.target = ToUrsaString(document.at("target").get<std::string>());
		manifest.compilerVersion = ToUrsaString(document.at("compilerVersion").get<std::string>());
		manifest.managed = document.at("managed").get<bool>();
		auto inputs = DecodeFiles(document.at("inputs"));
		auto outputs = DecodeFiles(document.at("outputs"));
		if (!inputs || !outputs || inputs.value.empty() || outputs.value.empty() || manifest.configuration.empty() ||
		    manifest.configuration.find_first_of("/\\:") != String::npos || manifest.target != "VulkanSpirv" ||
		    manifest.compilerVersion.empty())
		{
			return Result<BuildResourceManifest>::Failure(!inputs    ? inputs.error
			                                              : !outputs ? outputs.error
			                                                         : "Incomplete engine build manifest at " +
			                                                               path.value.ToString());
		}
		manifest.inputs = std::move(inputs.value);
		manifest.outputs = std::move(outputs.value);
		return Result<BuildResourceManifest>::Success(std::move(manifest));
	}
	catch (const Json::exception& error)
	{
		return Result<BuildResourceManifest>::Failure("Invalid engine build manifest: " + String(error.what()));
	}
}

Result<void> WriteBuildResourceManifest(VirtualFileSystem& files, const String& scheme,
                                        const BuildResourceManifest& manifest)
{
	const Json document{{"format", "UrsaBuildResources"},
	                    {"version", 1},
	                    {"configuration", ToStdString(manifest.configuration)},
	                    {"target", ToStdString(manifest.target)},
	                    {"compilerVersion", ToStdString(manifest.compilerVersion)},
	                    {"managed", manifest.managed},
	                    {"inputs", EncodeFiles(manifest.inputs)},
	                    {"outputs", EncodeFiles(manifest.outputs)}};
	const std::string text = document.dump(2);
	const auto* bytes = reinterpret_cast<const UInt8*>(text.data());
	return files.WriteAtomic(VirtualPath::Parse(scheme + ":BuildManifest.json").value,
	                         Vector<UInt8>(bytes, bytes + text.size()));
}

Result<BuildResourceManifest> ValidateBuildResources(const VirtualFileSystem& files, const String& scheme,
                                                     const String& configuration, bool requireManaged,
                                                     const VirtualFileSystem* sources, const String& sourceScheme)
{
	auto manifest = ReadBuildResourceManifest(files, scheme);
	if (!manifest)
	{
		return manifest;
	}
	if (manifest.value.configuration != configuration || (requireManaged && !manifest.value.managed))
	{
		return Result<BuildResourceManifest>::Failure("Engine build resources do not match configuration " +
		                                              configuration);
	}
	for (const auto& input : manifest.value.inputs)
	{
		if (input.path.size() >= 11 && input.path.substr(input.path.size() - 11) == ".ursashader")
		{
			const String artifact = input.path.substr(0, input.path.size() - 11) + ".ursashaderasset";
			for (const String required : {artifact, artifact + ".meta"})
			{
				if (std::none_of(manifest.value.outputs.begin(), manifest.value.outputs.end(),
				                 [&](const auto& record) { return record.path == required; }))
				{
					return Result<BuildResourceManifest>::Failure("Build manifest lacks Shader output: " + required);
				}
			}
		}
	}
	for (const auto& record : manifest.value.outputs)
	{
		const auto path = VirtualPath::Parse(scheme + ":" + record.path).value;
		auto bytes = files.Read(path);
		if (!bytes || bytes.value.size() != record.size || BuildResourceFingerprint(bytes.value) != record.fingerprint)
		{
			return Result<BuildResourceManifest>::Failure(
			    "Missing or invalid engine resource " + path.ToString() + " for " + configuration +
			    "; run UrsaAssetImport --project <project> and use its prepared content view");
		}
	}
	if (manifest.value.managed)
	{
		for (const String name : {"Ursa.ManagedHost.dll", "Ursa.ManagedHost.deps.json",
		                          "Ursa.ManagedHost.runtimeconfig.json", "Ursa.Runtime.dll"})
		{
			if (std::none_of(manifest.value.outputs.begin(), manifest.value.outputs.end(),
			                 [&](const auto& record) { return record.path == "Managed/" + name; }))
			{
				return Result<BuildResourceManifest>::Failure("Build manifest lacks managed bootstrap file: " + name);
			}
		}
	}
	if (sources)
	{
		auto inputs = SnapshotBuildResourceFiles(*sources, sourceScheme);
		if (!inputs || !EqualBuildResourceFiles(inputs.value, manifest.value.inputs))
		{
			return Result<BuildResourceManifest>::Failure(
			    "Engine resources are stale for " + configuration +
			    "; run UrsaAssetImport --project <project> and use its prepared content view");
		}
	}
	return manifest;
}
} // namespace Ursa
