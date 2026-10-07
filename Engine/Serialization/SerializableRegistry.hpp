#pragma once

#include <Serialization/SerializationContext.hpp>

namespace Ursa
{
class SerializableRegistry final
{
public:
	using Factory = std::function<Result<std::shared_ptr<Serializable>>(SerializationContext&)>;

	Result<void> Register(String type, UInt32 version, Factory factory)
	{
		if (type.empty() || !version || !factory || !types.emplace(type, Entry{version, std::move(factory)}).second)
		{
			return Result<void>::Failure("Invalid or duplicate Serializable type " + type);
		}
		return {};
	}

	Result<std::shared_ptr<Serializable>> Create(const String& type, UInt32 version,
	                                             SerializationContext& context) const
	{
		const auto found = types.find(type);
		if (found == types.end() || found->second.version != version)
		{
			return Result<std::shared_ptr<Serializable>>::Failure("Unknown Serializable type/version " + type);
		}
		return found->second.factory(context);
	}

private:
	struct Entry
	{
		UInt32 version;
		Factory factory;
	};

	Map<String, Entry> types;
};

Result<void> RegisterBuiltinSerializableTypes(SerializableRegistry& registry);
} // namespace Ursa
