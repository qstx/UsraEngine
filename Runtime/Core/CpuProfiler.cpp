#include <Core/CpuProfiler.hpp>
#include <Core/Profiler.hpp>

#if defined(URSA_ENABLE_TRACY)
#include <tracy/TracyC.h>
#include <atomic>
#include <map>
#include <memory>
#include <tuple>
#include <vector>
#endif

namespace Ursa::Profiling
{
#if defined(URSA_ENABLE_TRACY)
namespace
{
struct Marker
{
	String name;
	String file;
	String function;
	___tracy_source_location_data location{};
};

struct Registry
{
	std::mutex gate;
	std::map<std::tuple<String, String, String, Int32>, UInt64> identities;
	std::vector<std::unique_ptr<Marker>> markers;
	std::atomic<UInt64> nextToken{1};
};

Registry& Markers()
{
	// Tracy can request metadata after a script session or the host has closed.
	static auto* registry = new Registry;
	return *registry;
}

struct ActiveZone
{
	UInt64 token;
	TracyCZoneCtx context{};
};

thread_local std::vector<ActiveZone> activeZones;
} // namespace
#endif

bool CpuMarkersEnabled() noexcept
{
#if defined(URSA_ENABLE_TRACY)
	return true;
#else
	return false;
#endif
}

Result<UInt64> RegisterCpuMarker(const String& name, const String& file, const String& function, Int32 line)
{
	if (name.empty() || name.size() > 1024 || file.size() > 4096 || function.size() > 1024 || line < 0 ||
	    name.find('\0') != String::npos || file.find('\0') != String::npos || function.find('\0') != String::npos)
	{
		return Result<UInt64>::Failure("CPU marker requires a name and valid bounded source metadata");
	}
#if defined(URSA_ENABLE_TRACY)
	auto& registry = Markers();
	std::lock_guard lock(registry.gate);
	auto key = std::make_tuple(name, file, function, line);
	const auto found = registry.identities.find(key);
	if (found != registry.identities.end())
	{
		return Result<UInt64>::Success(found->second);
	}
	auto marker = std::make_unique<Marker>();
	marker->name = name;
	marker->file = file;
	marker->function = function;
	marker->location = {marker->name.c_str(), marker->function.c_str(), marker->file.c_str(), static_cast<UInt32>(line),
	                    0};
	const UInt64 identity = registry.markers.size() + 1;
	registry.markers.push_back(std::move(marker));
	registry.identities.emplace(std::move(key), identity);
	return Result<UInt64>::Success(identity);
#else
	return Result<UInt64>::Success(0);
#endif
}

Result<UInt64> BeginCpuMarker(UInt64 marker)
{
#if defined(URSA_ENABLE_TRACY)
	auto& registry = Markers();
	const ___tracy_source_location_data* location;
	{
		std::lock_guard lock(registry.gate);
		if (marker == 0 || marker > registry.markers.size())
		{
			return Result<UInt64>::Failure("Unknown CPU marker identity");
		}
		location = &registry.markers[static_cast<Size>(marker - 1)]->location;
	}
	const UInt64 token = registry.nextToken.fetch_add(1, std::memory_order_relaxed);
	// Allocate stack capacity before sending begin: allocation failure must not leave an open zone.
	activeZones.push_back({token, {}});
	activeZones.back().context = ___tracy_emit_zone_begin(location, IsConnected() ? 1 : 0);
	return Result<UInt64>::Success(token);
#else
	(void)marker;
	return Result<UInt64>::Success(0);
#endif
}

Result<void> EndCpuMarker(UInt64 token)
{
#if defined(URSA_ENABLE_TRACY)
	if (token == 0)
	{
		return {};
	}
	if (activeZones.empty() || activeZones.back().token != token)
	{
		return Result<void>::Failure("CPU scopes must end once, in stack order, on their starting thread");
	}
	___tracy_emit_zone_end(activeZones.back().context);
	activeZones.pop_back();
#else
	(void)token;
#endif
	return {};
}
} // namespace Ursa::Profiling
