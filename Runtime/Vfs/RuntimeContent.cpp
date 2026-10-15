#include <Vfs/RuntimeContent.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

class ContentProvider final : public VirtualFileProvider
{
public:
	ContentProvider(std::shared_ptr<const VirtualFileSystem> storage, const RuntimeContentSnapshot& snapshot,
	                const String& scheme)
	    : storage(std::move(storage)), label("content:" + scheme)
	{
		for (const auto& entry : snapshot.entries)
		{
			if (entry.path.scheme == scheme)
			{
				entries.emplace(entry.path.relative, entry);
			}
		}
	}

	String Identity() const override
	{
		return label + ":" + ToUrsaString(std::to_string(reinterpret_cast<UIntPtr>(this)));
	}

	String SourceLabel() const override
	{
		return label;
	}

	Result<bool> Exists(const String& path) const override
	{
		return Result<bool>::Success(entries.find(path) != entries.end());
	}

	Result<UInt64> Size(const String& path) const override
	{
		const auto entry = entries.find(path);
		return entry == entries.end() ? Result<UInt64>::Failure("Missing runtime content: " + path)
		                              : Result<UInt64>::Success(entry->second.size);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		const auto entry = entries.find(path);
		return entry == entries.end() ? Result<Vector<UInt8>>::Failure("Missing runtime content: " + path)
		                              : storage->ReadRange(entry->second.storage, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		Vector<String> result;
		for (const auto& entry : entries)
		{
			result.push_back(entry.first);
		}
		return Result<Vector<String>>::Success(std::move(result));
	}

	Result<void> WriteAtomic(const String&, const Vector<UInt8>&, bool) override
	{
		return Result<void>::Failure("Imported runtime content is read-only");
	}

	Result<void> Append(const String&, const Vector<UInt8>&, bool) override
	{
		return Result<void>::Failure("Imported runtime content is read-only");
	}

private:
	std::shared_ptr<const VirtualFileSystem> storage;
	String label;
	Map<String, RuntimeContentEntry> entries;
};

Result<void> ValidateInputs(const RuntimeContentSnapshot& snapshot, const VirtualFileSystem& sources)
{
	for (const auto& scheme : snapshot.completeSourceSchemes)
	{
		Set<String> expected;
		for (const auto& input : snapshot.inputs)
		{
			if (input.path.scheme == scheme)
			{
				expected.insert(input.path.ToString());
			}
		}
		auto listing = sources.List(scheme);
		if (!listing || listing.value.size() != expected.size())
		{
			return Result<void>::Failure("Runtime source inventory changed; run UrsaAssetImport --project <project>");
		}
		for (const auto& entry : listing.value)
		{
			if (!expected.erase(entry.path.ToString()))
			{
				return Result<void>::Failure("Runtime source inventory changed at " + entry.path.ToString() +
				                             "; run UrsaAssetImport --project <project>");
			}
		}
	}
	for (const auto& input : snapshot.inputs)
	{
		auto bytes = sources.Read(input.path);
		if (!bytes || bytes.value.size() != input.size || BuildResourceFingerprint(bytes.value) != input.fingerprint)
		{
			return Result<void>::Failure("Runtime content is stale at " + input.path.ToString() +
			                             "; run UrsaAssetImport --project <project> before starting Player");
		}
	}
	return {};
}

Result<void> ValidateEntries(const RuntimeContentSnapshot& snapshot, const VirtualFileSystem& files)
{
	Set<String> paths;
	for (const auto& entry : snapshot.entries)
	{
		auto logical = VirtualPath::Parse(entry.path.ToString());
		auto storage = VirtualPath::Parse(entry.storage.ToString());
		if (!logical || !storage || (entry.path.scheme != engineScheme && entry.path.scheme != projectScheme) ||
		    entry.storage.scheme != editorLibraryScheme || !paths.insert(entry.path.ToString()).second)
		{
			return Result<void>::Failure("Invalid or duplicate runtime content entry: " + entry.path.ToString());
		}
		auto bytes = files.Read(entry.storage);
		if (!bytes || entry.fingerprint.empty() || bytes.value.size() != entry.size ||
		    BuildResourceFingerprint(bytes.value) != entry.fingerprint)
		{
			return Result<void>::Failure("Runtime content is missing or damaged at " + entry.path.ToString() +
			                             "; run UrsaAssetImport --project <project>");
		}
	}
	return snapshot.entries.empty() ? Result<void>::Failure("Runtime content has no entries") : Result<void>{};
}
} // namespace

Result<VirtualPath> RuntimeContentManifestPath(const String& configuration, const String& profile, const String& target)
{
	for (const auto& part : {configuration, profile, target})
	{
		if (part.empty() ||
		    part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != String::npos)
		{
			return Result<VirtualPath>::Failure("Invalid runtime content target/profile/configuration");
		}
	}
	return VirtualPath::Parse(String(editorLibraryScheme) + ":RuntimeContent/" + target + "/" + configuration + "/" +
	                          profile + "/Ready.json");
}

Result<RuntimeContentInput> CaptureRuntimeContentInput(const VirtualFileSystem& files, const VirtualPath& path)
{
	auto bytes = files.Read(path);
	return bytes ? Result<RuntimeContentInput>::Success(
	                   {path, BuildResourceFingerprint(bytes.value), static_cast<UInt64>(bytes.value.size())})
	             : Result<RuntimeContentInput>::Failure(bytes.error);
}

Result<void> PublishRuntimeContent(VirtualFileSystem& files, RuntimeContentSnapshot snapshot)
{
	auto path = RuntimeContentManifestPath(snapshot.configuration, snapshot.profile, snapshot.target);
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	auto inputs = ValidateInputs(snapshot, files);
	if (!inputs)
	{
		return inputs;
	}
	auto entries = ValidateEntries(snapshot, files);
	if (!entries)
	{
		return entries;
	}
	std::sort(snapshot.entries.begin(), snapshot.entries.end(),
	          [](const auto& left, const auto& right) { return left.path.ToString() < right.path.ToString(); });
	Json document{{"format", "UrsaRuntimeContent"},
	              {"version", 1u},
	              {"target", ToStdString(snapshot.target)},
	              {"configuration", ToStdString(snapshot.configuration)},
	              {"profile", ToStdString(snapshot.profile)}};
	document["entries"] = Json::array();
	document["inputs"] = Json::array();
	document["completeSourceSchemes"] = Json::array();
	for (const auto& scheme : snapshot.completeSourceSchemes)
	{
		document["completeSourceSchemes"].push_back(ToStdString(scheme));
	}
	for (const auto& entry : snapshot.entries)
	{
		document["entries"].push_back({{"path", ToStdString(entry.path.ToString())},
		                               {"storage", ToStdString(entry.storage.ToString())},
		                               {"fingerprint", ToStdString(entry.fingerprint)},
		                               {"size", entry.size}});
	}
	for (const auto& input : snapshot.inputs)
	{
		document["inputs"].push_back({{"path", ToStdString(input.path.ToString())},
		                              {"fingerprint", ToStdString(input.fingerprint)},
		                              {"size", input.size}});
	}
	const auto text = document.dump(2);
	return files.WriteAtomic(path.value, Vector<UInt8>(text.data(), text.data() + text.size()));
}

Result<RuntimeContentSnapshot> ReadRuntimeContent(const VirtualFileSystem& files, const String& configuration,
                                                  const String& profile, const String& target,
                                                  const VirtualFileSystem* currentSources)
{
	auto path = RuntimeContentManifestPath(configuration, profile, target);
	if (!path)
	{
		return Result<RuntimeContentSnapshot>::Failure(path.error);
	}
	auto text = files.ReadText(path.value);
	if (!text)
	{
		return Result<RuntimeContentSnapshot>::Failure("Project content is not ready for " + configuration + "/" +
		                                               profile + "; run UrsaAssetImport --project <project>. " +
		                                               text.error);
	}
	try
	{
		const auto document = Json::parse(ToStdString(text.value));
		if (document.at("format") != "UrsaRuntimeContent" || document.at("version") != 1 ||
		    document.at("target") != ToStdString(target) ||
		    document.at("configuration") != ToStdString(configuration) ||
		    document.at("profile") != ToStdString(profile))
		{
			return Result<RuntimeContentSnapshot>::Failure("Runtime content target or schema mismatch");
		}
		RuntimeContentSnapshot snapshot;
		snapshot.target = target;
		snapshot.configuration = configuration;
		snapshot.profile = profile;
		for (const auto& scheme : document.value("completeSourceSchemes", Json::array()))
		{
			const auto name = ToUrsaString(scheme.get<std::string>());
			if (name != engineScheme && name != projectScheme && name != projectSettingsScheme)
			{
				return Result<RuntimeContentSnapshot>::Failure("Invalid source inventory scheme");
			}
			snapshot.completeSourceSchemes.push_back(name);
		}
		for (const auto& entry : document.at("entries"))
		{
			auto logical = VirtualPath::Parse(ToUrsaString(entry.at("path").get<std::string>()));
			auto storage = VirtualPath::Parse(ToUrsaString(entry.at("storage").get<std::string>()));
			if (!logical || !storage)
			{
				return Result<RuntimeContentSnapshot>::Failure("Invalid runtime content path");
			}
			snapshot.entries.push_back({logical.value, storage.value,
			                            ToUrsaString(entry.at("fingerprint").get<std::string>()),
			                            entry.at("size").get<UInt64>()});
		}
		for (const auto& input : document.at("inputs"))
		{
			auto source = VirtualPath::Parse(ToUrsaString(input.at("path").get<std::string>()));
			if (!source)
			{
				return Result<RuntimeContentSnapshot>::Failure(source.error);
			}
			snapshot.inputs.push_back({source.value, ToUrsaString(input.at("fingerprint").get<std::string>()),
			                           input.at("size").get<UInt64>()});
		}
		auto valid = ValidateEntries(snapshot, files);
		if (!valid)
		{
			return Result<RuntimeContentSnapshot>::Failure(valid.error);
		}
		if (currentSources)
		{
			valid = ValidateInputs(snapshot, *currentSources);
			if (!valid)
			{
				return Result<RuntimeContentSnapshot>::Failure(valid.error);
			}
		}
		return Result<RuntimeContentSnapshot>::Success(std::move(snapshot));
	}
	catch (const std::exception& error)
	{
		return Result<RuntimeContentSnapshot>::Failure("Invalid runtime content manifest: " +
		                                               ToUrsaString(error.what()));
	}
}

Result<VirtualFileSystem> OpenRuntimeContentView(std::shared_ptr<const VirtualFileSystem> storage,
                                                 const RuntimeContentSnapshot& snapshot)
{
	if (!storage)
	{
		return Result<VirtualFileSystem>::Failure("Runtime content requires artifact storage");
	}
	auto valid = ValidateEntries(snapshot, *storage);
	if (!valid)
	{
		return Result<VirtualFileSystem>::Failure(valid.error);
	}
	VirtualFileSystem view;
	for (const String scheme : {String(engineScheme), String(projectScheme)})
	{
		auto mounted = view.MountProvider(scheme, std::make_shared<ContentProvider>(storage, snapshot, scheme),
		                                  MountAccess::ReadOnly);
		if (!mounted)
		{
			return Result<VirtualFileSystem>::Failure(mounted.error);
		}
	}
	return Result<VirtualFileSystem>::Success(std::move(view));
}
} // namespace Ursa
