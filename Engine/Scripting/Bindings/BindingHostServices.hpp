#pragma once

#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Core/Containers.hpp>

namespace Ursa::Scripting::Bindings
{
class BindingHostServices
{
public:
	virtual ~BindingHostServices() = default;
	virtual Result<void> WriteScriptLog(UInt32 level, const String& message) = 0;
	virtual Result<void> ReportHostMessage(UInt32 level, const String& message) = 0;
	virtual Result<Vector<UInt8>> ReadScriptFile(const String& path) = 0;
	virtual Result<void> WriteScriptFile(const String& path, const Vector<UInt8>& bytes) = 0;
};

class BindingHostAccess final
{
public:
	explicit BindingHostAccess(void* token) : lease(BindingContext::Acquire(token)), lock(lease->gate)
	{
		if (!lease->source)
		{
			throw std::runtime_error("Runtime host services have closed");
		}
		lease->source->Require(0);
		services = dynamic_cast<BindingHostServices*>(lease->source);
		if (!services)
		{
			throw std::runtime_error("Binding context does not provide Runtime host services");
		}
	}

	BindingHostServices& Services() const
	{
		return *services;
	}

private:
	std::shared_ptr<Detail::ContextLease> lease;
	std::unique_lock<std::recursive_mutex> lock;
	BindingHostServices* services = nullptr;
};
} // namespace Ursa::Scripting::Bindings
