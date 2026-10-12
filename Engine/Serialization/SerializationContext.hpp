#pragma once

#include <Serialization/Serializable.hpp>
#include <Serialization/ResourceId.hpp>
#include <functional>
#include <typeindex>

namespace Ursa
{
class ResourceSession;

class SerializationContext
{
public:
	explicit SerializationContext(ResourceSession* resources = nullptr) : resources(resources)
	{
	}

	virtual ~SerializationContext() = default;

	template <class T> void Provide(T& service)
	{
		services[std::type_index(typeid(T))] = &service;
	}

	template <class T> Result<T*> Service() const
	{
		auto found = services.find(std::type_index(typeid(T)));
		return found == services.end() ? Result<T*>::Failure("Missing runtime service " + String(typeid(T).name()))
		                               : Result<T*>::Success(static_cast<T*>(found->second));
	}

	template <class T> void RemoveService()
	{
		services.erase(std::type_index(typeid(T)));
	}

	ResourceSession* ResourceLoader() const
	{
		return resources;
	}

	void SetResourceIdResolver(std::function<Result<ResourceId>(const Serializable&)> resolver)
	{
		resourceIdResolver = std::move(resolver);
	}

	Result<ResourceId> Identify(const Serializable& object) const
	{
		return resourceIdResolver ? resourceIdResolver(object) : Result<ResourceId>::Failure("No resource ID resolver");
	}

	Result<void> RegisterObject(ResourceId id, Serializable& object)
	{
		if (!id || !objects.emplace(id.ToString(), &object).second)
		{
			return Result<void>::Failure("Duplicate persistent serialization ID " + id.ToString());
		}
		return {};
	}

	template <class T> Result<T*> Object(ResourceId id) const
	{
		auto found = objects.find(id.ToString());
		auto* object = found == objects.end() ? nullptr : dynamic_cast<T*>(found->second);
		return object ? Result<T*>::Success(object)
		              : Result<T*>::Failure("Unresolved object reference " + id.ToString());
	}

	void Defer(std::function<Result<void>()> binding)
	{
		bindings.push_back(std::move(binding));
	}

	Result<void> ResolveReferences()
	{
		auto pending = std::move(bindings);
		bindings.clear();
		for (const auto& binding : pending)
		{
			auto result = binding();
			if (!result)
			{
				return result;
			}
		}
		return {};
	}

	void ClearReferences()
	{
		objects.clear();
		bindings.clear();
	}

	static SerializationContext* Active()
	{
		return active;
	}

	static SerializationContext* SetActive(SerializationContext* value)
	{
		auto* previous = active;
		active = value;
		return previous;
	}

private:
	inline static thread_local SerializationContext* active = nullptr;
	ResourceSession* resources;
	Map<std::type_index, void*> services;
	Map<String, Serializable*> objects;
	Vector<std::function<Result<void>()>> bindings;
	std::function<Result<ResourceId>(const Serializable&)> resourceIdResolver;
};

class SerializationScope final
{
public:
	explicit SerializationScope(SerializationContext* context) : previous(SerializationContext::SetActive(context))
	{
	}

	~SerializationScope()
	{
		SerializationContext::SetActive(previous);
	}

private:
	SerializationContext* previous;
};
} // namespace Ursa
