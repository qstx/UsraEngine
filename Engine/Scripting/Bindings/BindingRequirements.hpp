#pragma once

#include <BindingManifest.generated.hpp>
#include <Core/Result.hpp>
#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <algorithm>

namespace Ursa::Scripting::Bindings
{
inline nlohmann::json ScriptBindingRequirements(bool editor)
{
	auto result = nlohmann::json::array();
	for (const auto& module : Manifest::modules)
	{
		if (module.required || (editor && module.callScoped))
		{
			result.push_back({{"Id", module.id},
			                  {"Name", module.name},
			                  {"Hash", module.hash},
			                  {"FunctionCount", module.functionCount},
			                  {"CallScoped", module.callScoped}});
		}
	}
	return result;
}

inline Result<void> ValidateScriptBindingRequirements(const nlohmann::json& requirements, bool editor)
{
	try
	{
		const auto expected = ScriptBindingRequirements(editor);
		if (!requirements.is_array() || requirements.size() != expected.size())
		{
			return Result<void>::Failure(
			    "Script content binding requirements are incomplete; rebuild the project content");
		}
		std::set<UInt64> seen;
		for (const auto& required : requirements)
		{
			const UInt64 id = required.at("Id").get<UInt64>();
			if (!seen.insert(id).second)
			{
				return Result<void>::Failure("Script content repeats a binding module");
			}
			const auto module =
			    std::find_if(expected.begin(), expected.end(), [&](const auto& value) { return value.at("Id") == id; });
			if (module == expected.end() || *module != required)
			{
				return Result<void>::Failure(
				    "Script content binding identity differs from this Engine; rebuild the project content");
			}
		}
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Malformed script binding requirements: " + ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Scripting::Bindings
