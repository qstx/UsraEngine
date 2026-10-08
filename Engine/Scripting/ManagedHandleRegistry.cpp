#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/ManagedHandleRegistry.hpp>
#include <limits>
#include <sstream>

namespace Ursa::Scripting
{
namespace
{
std::atomic<UInt64> nextSessionId{1};
} // namespace

ManagedHandleRegistry::ManagedHandleRegistry(UInt64 session)
    : sessionId(session), mainThread(std::this_thread::get_id())
{
}

Result<std::unique_ptr<ManagedHandleRegistry>> ManagedHandleRegistry::Create()
{
	const UInt64 session = nextSessionId.fetch_add(1, std::memory_order_relaxed);
	if (session == 0 || session == std::numeric_limits<UInt64>::max())
	{
		return Result<std::unique_ptr<ManagedHandleRegistry>>::Failure("Managed handle session ID space is exhausted");
	}
	return Result<std::unique_ptr<ManagedHandleRegistry>>::Success(
	    std::unique_ptr<ManagedHandleRegistry>(new ManagedHandleRegistry(session)));
}

Result<ManagedHandle> ManagedHandleRegistry::Register(void* object, ManagedHandleKind kind)
{
	auto mainThreadResult = RequireMainThread();
	if (!mainThreadResult)
	{
		return Result<ManagedHandle>::Failure(mainThreadResult.error);
	}
	if (!object)
	{
		return Result<ManagedHandle>::Failure("Cannot register a null native object");
	}
	const auto existing = objectIndices.find(object);
	if (existing != objectIndices.end())
	{
		const Slot& slot = slots[existing->second];
		if (slot.kind != kind)
		{
			return Result<ManagedHandle>::Failure("Native object is already registered with another handle kind");
		}
		return Result<ManagedHandle>::Success({sessionId, existing->second, slot.generation, slot.kind, 0});
	}

	UInt32 index = 0;
	if (freeIndices.empty())
	{
		if (slots.size() >= std::numeric_limits<UInt32>::max())
		{
			return Result<ManagedHandle>::Failure("Managed handle table is full");
		}
		index = static_cast<UInt32>(slots.size());
		slots.emplace_back();
	}
	else
	{
		index = freeIndices.back();
		freeIndices.pop_back();
	}
	Slot& slot = slots[index];
	slot.object = object;
	slot.kind = kind;
	objectIndices.emplace(object, index);
	return Result<ManagedHandle>::Success({sessionId, index, slot.generation, kind, 0});
}

Result<void*> ManagedHandleRegistry::Resolve(const ManagedHandle& handle, ManagedHandleKind expectedKind) const
{
	auto mainThreadResult = RequireMainThread();
	if (!mainThreadResult)
	{
		return Result<void*>::Failure(mainThreadResult.error);
	}
	if (handle.reserved != 0 || handle.sessionId != sessionId || handle.index >= slots.size())
	{
		return Result<void*>::Failure("Managed object handle belongs to another session or is malformed");
	}
	const Slot& slot = slots[handle.index];
	if (!slot.object || slot.generation != handle.generation || slot.kind != handle.kind || handle.kind != expectedKind)
	{
		return Result<void*>::Failure("Managed object handle is stale or has the wrong kind");
	}
	return Result<void*>::Success(slot.object);
}

Result<void> ManagedHandleRegistry::Invalidate(const ManagedHandle& handle)
{
	auto mainThreadResult = RequireMainThread();
	if (!mainThreadResult)
	{
		return mainThreadResult;
	}
	if (handle.sessionId != sessionId || handle.reserved != 0 || handle.index >= slots.size())
	{
		return Result<void>::Failure("Cannot invalidate a handle from another session");
	}
	Slot& slot = slots[handle.index];
	if (!slot.object || slot.generation != handle.generation || slot.kind != handle.kind)
	{
		return Result<void>::Failure("Cannot invalidate a stale managed object handle");
	}
	objectIndices.erase(slot.object);
	slot.object = nullptr;
	AdvanceGeneration(slot);
	freeIndices.push_back(handle.index);
	return {};
}

Result<void> ManagedHandleRegistry::InvalidateAll()
{
	auto mainThreadResult = RequireMainThread();
	if (!mainThreadResult)
	{
		return mainThreadResult;
	}
	objectIndices.clear();
	freeIndices.clear();
	for (UInt32 index = 0; index < slots.size(); ++index)
	{
		Slot& slot = slots[index];
		if (!slot.object)
		{
			continue;
		}
		slot.object = nullptr;
		AdvanceGeneration(slot);
		freeIndices.push_back(index);
	}
	return {};
}

Result<void> ManagedHandleRegistry::RequireMainThread() const
{
	if (std::this_thread::get_id() == mainThread)
	{
		return {};
	}
	std::ostringstream message;
	message << "Managed object handles can only be accessed on the owning main thread (current "
	        << std::this_thread::get_id() << ", owner " << mainThread << ")";
	return Result<void>::Failure(ToUrsaString(message.str()));
}

void ManagedHandleRegistry::AdvanceGeneration(Slot& slot)
{
	++slot.generation;
	if (slot.generation == 0)
	{
		slot.generation = 1;
	}
}
} // namespace Ursa::Scripting
