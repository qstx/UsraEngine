#include <Serialization/PrefabLocalResources.hpp>
#include <Core/StringInterop.hpp>
#include <functional>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

String Scoped(ResourceId instance, ResourceId local)
{
	for (Size i = 0; i < local.bytes.size(); ++i)
	{
		local.bytes[i] ^= instance.bytes[i];
	}
	if (!local)
	{
		local.bytes[0] = 1;
	}
	return local.ToString();
}

Result<Map<String, Json>> Table(const Json& rows)
{
	if (!rows.is_array() || rows.size() > 100000)
	{
		return Result<Map<String, Json>>::Failure("Invalid local resource table");
	}
	Map<String, Json> result;
	for (const auto& row : rows)
	{
		if (!row.is_object() || !row.contains("id") || !row["id"].is_string())
		{
			return Result<Map<String, Json>>::Failure("Malformed local resource record");
		}
		const auto id = ToUrsaString(row["id"].get<std::string>());
		if (!ResourceId::Parse(id) || !result.emplace(id, row).second)
		{
			return Result<Map<String, Json>>::Failure("Duplicate or invalid local resource ID " + id);
		}
	}
	return Result<Map<String, Json>>::Success(std::move(result));
}
} // namespace

void RemapLocalResourceBindings(Json& value, const Map<String, String>& mapping)
{
	if (value.is_object() && value.value("kind", std::string{}) == "local" && value.contains("id") &&
	    value["id"].is_string())
	{
		auto found = mapping.find(ToUrsaString(value["id"].get<std::string>()));
		if (found != mapping.end())
		{
			value["id"] = ToStdString(found->second);
		}
		return;
	}
	if (value.is_array() || value.is_object())
	{
		for (auto& item : value)
		{
			RemapLocalResourceBindings(item, mapping);
		}
	}
}

Set<String> ReferencedLocalResources(const Json& value)
{
	Set<String> result;
	std::function<void(const Json&)> visit = [&](const Json& item)
	{
		if (item.is_object() && item.value("kind", std::string{}) == "local" && item.contains("id") &&
		    item["id"].is_string())
		{
			result.insert(ToUrsaString(item["id"].get<std::string>()));
			return;
		}
		if (item.is_object() || item.is_array())
		{
			for (const auto& child : item)
			{
				visit(child);
			}
		}
	};
	visit(value);
	return result;
}

Result<Json> MergePrefabLocalResources(const Json& prefab, const Json& overrides)
{
	auto table = Table(prefab.value("localResources", Json::array()));
	if (!table)
	{
		return Result<Json>::Failure(table.error);
	}
	if (overrides.contains("localResources"))
	{
		auto additions = Table(overrides["localResources"]);
		if (!additions)
		{
			return Result<Json>::Failure(additions.error);
		}
		for (const auto& row : additions.value)
		{
			table.value[row.first] = row.second;
		}
	}
	Json rows = Json::array();
	for (auto& row : table.value)
	{
		rows.push_back(std::move(row.second));
	}
	return Result<Json>::Success(std::move(rows));
}

Result<void> ExpandPrefabLocalResources(const Json& prefab, const Json& overrides, ResourceId instance,
                                        Map<String, String>& scoped, Json& expanded)
{
	if (prefab.contains("localResources") && prefab.value("localResourceVersion", UInt32{0}) != 1)
	{
		return Result<void>::Failure("Unsupported Prefab local resource version");
	}
	auto rows = MergePrefabLocalResources(prefab, overrides);
	if (!rows)
	{
		return Result<void>::Failure(rows.error);
	}
	for (const auto& row : rows.value)
	{
		const auto id = ToUrsaString(row["id"].get<std::string>());
		auto parsed = ResourceId::Parse(id);
		if (!parsed || !scoped.emplace(id, Scoped(instance, parsed.value)).second)
		{
			return Result<void>::Failure("Prefab resource ID collides with an object or component ID");
		}
	}
	auto current = Table(expanded.value("localResources", Json::array()));
	if (!current)
	{
		return Result<void>::Failure(current.error);
	}
	for (auto row : rows.value)
	{
		const auto id = scoped.at(ToUrsaString(row["id"].get<std::string>()));
		row["id"] = ToStdString(id);
		RemapLocalResourceBindings(row["data"], scoped);
		auto existing = current.value.find(id);
		if (existing != current.value.end() && existing->second != row)
		{
			return Result<void>::Failure("Conflicting scoped local resource records " + id);
		}
		current.value[id] = std::move(row);
	}
	expanded["localResourceVersion"] = UInt32{1};
	expanded["localResources"] = Json::array();
	for (auto& row : current.value)
	{
		expanded["localResources"].push_back(std::move(row.second));
	}
	return {};
}

Result<void> CapturePrefabLocalResources(const Json& current, const Json& baseline, const String& instance,
                                         Map<String, String>& scoped, Json& overrides)
{
	auto parsedInstance = ResourceId::Parse(instance);
	if (!parsedInstance)
	{
		return Result<void>::Failure(parsedInstance.error);
	}
	Map<String, const Json*> currentNodes;
	for (const auto& node : current["gameObjects"])
	{
		currentNodes[ToUrsaString(node["id"].get<std::string>())] = &node;
	}
	String root;
	Set<String> previousNodes;
	for (const auto& node : baseline["gameObjects"])
	{
		const auto id = ToUrsaString(node["id"].get<std::string>());
		for (const auto& entry : scoped)
		{
			if (entry.second == id)
			{
				previousNodes.insert(id);
				break;
			}
		}
	}
	for (const auto& node : baseline["gameObjects"])
	{
		const auto id = ToUrsaString(node["id"].get<std::string>());
		if (previousNodes.count(id) &&
		    (node["parentId"].is_null() || !previousNodes.count(ToUrsaString(node["parentId"].get<std::string>()))))
		{
			root = id;
			break;
		}
	}
	if (root.empty() || !currentNodes.count(root))
	{
		return Result<void>::Failure("Prefab instance root is missing");
	}
	Set<String> reachableNodes{root};
	bool changed = true;
	while (changed)
	{
		changed = false;
		for (const auto& entry : currentNodes)
		{
			const auto& parent = (*entry.second)["parentId"];
			if (!parent.is_null() && reachableNodes.count(ToUrsaString(parent.get<std::string>())))
			{
				changed |= reachableNodes.insert(entry.first).second;
			}
		}
	}
	Set<String> referenced;
	for (const auto& id : reachableNodes)
	{
		const auto ids = ReferencedLocalResources((*currentNodes.at(id))["components"]);
		referenced.insert(ids.begin(), ids.end());
	}
	auto currentRows = Table(current.value("localResources", Json::array()));
	auto baselineRows = Table(baseline.value("localResources", Json::array()));
	auto oldOverrides = Table(overrides.value("localResources", Json::array()));
	if (!currentRows || !baselineRows || !oldOverrides)
	{
		return Result<void>::Failure("Invalid captured local resource table");
	}
	Map<String, String> reverse;
	for (const auto& entry : scoped)
	{
		reverse[entry.second] = entry.first;
	}
	for (const auto& id : referenced)
	{
		if (!currentRows.value.count(id))
		{
			return Result<void>::Failure("Missing reachable local resource " + id);
		}
		if (!reverse.count(id))
		{
			auto parsed = ResourceId::Parse(id);
			if (!parsed)
			{
				return Result<void>::Failure(parsed.error);
			}
			const auto local = Scoped(parsedInstance.value, parsed.value);
			if (!scoped.emplace(local, id).second)
			{
				return Result<void>::Failure("Prefab local identity collision");
			}
			reverse[id] = local;
		}
	}
	Json retained = Json::array();
	for (const auto& id : referenced)
	{
		const auto local = reverse.at(id);
		const auto& row = currentRows.value.at(id);
		auto before = baselineRows.value.find(id);
		if (before == baselineRows.value.end() || before->second != row || oldOverrides.value.count(local))
		{
			Json record = row;
			record["id"] = ToStdString(local);
			RemapLocalResourceBindings(record["data"], reverse);
			retained.push_back(std::move(record));
		}
	}
	if (retained.empty())
	{
		overrides.erase("localResources");
	}
	else
	{
		overrides["localResources"] = std::move(retained);
	}
	return {};
}

void CaptureRegularLocalResources(Json& document, const Json& encoded)
{
	const auto referenced = ReferencedLocalResources(document["gameObjects"]);
	const auto rows = encoded.value("localResources", Json::array());
	document["localResourceVersion"] = UInt32{1};
	document["localResources"] = Json::array();
	for (const auto& row : rows)
	{
		if (referenced.count(ToUrsaString(row["id"].get<std::string>())))
		{
			document["localResources"].push_back(row);
		}
	}
}
} // namespace Ursa
