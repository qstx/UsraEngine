#pragma once

#include <Core/Types.hpp>
#include <Core/Result.hpp>
#include <Core/Memory/Memory.hpp>
#include <Core/StringInterop.hpp>
#include <cstring>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#include <utility>

namespace Ursa::Scripting::Bindings
{
inline constexpr UInt64 maximumBytes = 512ull * 1024 * 1024;

struct Utf8View
{
	const UInt8* data = nullptr;
	UInt64 length = 0;
	UInt32 present = 0;
	UInt32 reserved = 0;
};

struct DataView
{
	const void* data = nullptr;
	UInt64 length = 0;
	UInt32 present = 0;
	UInt32 reserved = 0;
};

struct BindingOutcome
{
	Utf8View error;
	void* owner = nullptr;
	void (*release)(void*) = nullptr;
	UInt64 sessionIdentity = 0;
};

struct BindingModuleDescriptor
{
	UInt32 version = 1;
	UInt32 size = sizeof(BindingModuleDescriptor);
	UInt32 pointerSize = sizeof(void*);
	UInt32 functionCount = 0;
	const void* functions = nullptr;
	Int32 (*validateContext)(void*, BindingOutcome*) = nullptr;
	UInt8 hash[32]{};
};

struct BindingModuleReference
{
	UInt64 id = 0;
	const BindingModuleDescriptor* descriptor = nullptr;
	void* context = nullptr;
};

struct BindingBootstrap
{
	UInt32 version = 1;
	UInt32 size = sizeof(BindingBootstrap);
	const BindingModuleReference* modules = nullptr;
	UInt64 moduleCount = 0;
	BindingModuleReference* callbacks = nullptr;
	UInt64 callbackCapacity = 0;
	UInt64 callbackCount = 0;
	BindingOutcome outcome;
};

static_assert(sizeof(Utf8View) == 24 && sizeof(DataView) == 24 && sizeof(BindingOutcome) == 48 &&
                  sizeof(BindingModuleDescriptor) == 64,
              "Binding bootstrap requires the supported x64 ABI");

inline void Validate(const void* data, UInt64 count, UInt64 stride, UInt32 present, UInt32 reserved = 0)
{
	if (present > 1 || reserved != 0 || (!present && (data || count)) || (count && !data) || !stride ||
	    count > maximumBytes / stride || count > UInt64(std::numeric_limits<Int32>::max()))
	{
		throw std::runtime_error("Invalid binding buffer range or presence marker");
	}
}

inline String ReadString(Utf8View value, bool nullable = false)
{
	Validate(value.data, value.length, 1, value.present, value.reserved);
	if (!value.present && !nullable)
	{
		throw std::runtime_error("Required binding string is null");
	}
	return value.length ? String(reinterpret_cast<const char*>(value.data), Size(value.length)) : String{};
}

inline void ValidateUtf8(Utf8View value, bool nullable)
{
	Validate(value.data, value.length, 1, value.present, value.reserved);
	if (!nullable && !value.present)
	{
		throw std::runtime_error("Required binding string is null");
	}
	for (UInt64 index = 0; index < value.length;)
	{
		const UInt8 lead = value.data[index++];
		if (lead <= 0x7f)
		{
			continue;
		}
		UInt32 continuation = 0;
		UInt8 minimum = 0x80, maximum = 0xbf;
		if (lead >= 0xc2 && lead <= 0xdf)
		{
			continuation = 1;
		}
		else if (lead >= 0xe0 && lead <= 0xef)
		{
			continuation = 2;
			minimum = lead == 0xe0 ? 0xa0 : 0x80;
			maximum = lead == 0xed ? 0x9f : 0xbf;
		}
		else if (lead >= 0xf0 && lead <= 0xf4)
		{
			continuation = 3;
			minimum = lead == 0xf0 ? 0x90 : 0x80;
			maximum = lead == 0xf4 ? 0x8f : 0xbf;
		}
		if (!continuation || continuation > value.length - index || value.data[index] < minimum ||
		    value.data[index] > maximum)
		{
			throw std::runtime_error("Invalid binding UTF-8");
		}
		++index;
		for (UInt32 part = 1; part < continuation; ++part, ++index)
		{
			if (value.data[index] < 0x80 || value.data[index] > 0xbf)
			{
				throw std::runtime_error("Invalid binding UTF-8 continuation");
			}
		}
	}
}

inline void CompleteBindingResult(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

template <class T> T TakeBindingResult(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

class BindingContext;

namespace Detail
{
struct ContextLease
{
	std::recursive_mutex gate;
	BindingContext* source = nullptr;
};

struct ContextRegistry
{
	std::mutex gate;
	std::unordered_map<UIntPtr, std::shared_ptr<ContextLease>> entries;
	UIntPtr nextToken = 1;
};

inline ContextRegistry& Contexts()
{
	static ContextRegistry registry;
	return registry;
}
} // namespace Detail

inline constexpr UInt32 contextProtocolVersion = 1;

class BindingContext
{
public:
	BindingContext()
	{
		lease = std::make_shared<Detail::ContextLease>();
		lease->source = this;
		auto& registry = Detail::Contexts();
		std::lock_guard lock(registry.gate);
		if (registry.nextToken == std::numeric_limits<UIntPtr>::max())
		{
			throw std::runtime_error("Binding context identity exhausted");
		}
		contextToken = registry.nextToken++;
		registry.entries.emplace(contextToken, lease);
	}

	BindingContext(const BindingContext&) = delete;
	BindingContext& operator=(const BindingContext&) = delete;

	virtual ~BindingContext()
	{
		Deactivate();
	}

	void* Token() const noexcept
	{
		return reinterpret_cast<void*>(contextToken);
	}

	// Providers with concurrent services deactivate before destroying their own state.
	void Deactivate()
	{
		std::lock_guard active(lease->gate);
		lease->source = nullptr;
		auto& registry = Detail::Contexts();
		std::lock_guard lock(registry.gate);
		registry.entries.erase(contextToken);
	}

	static std::shared_ptr<Detail::ContextLease> Acquire(void* context)
	{
		auto& registry = Detail::Contexts();
		std::lock_guard lock(registry.gate);
		const auto found = registry.entries.find(reinterpret_cast<UIntPtr>(context));
		if (found == registry.entries.end())
		{
			throw std::runtime_error("Binding context is closed or invalid");
		}
		return found->second;
	}

	virtual void Require(UInt32 policy) const
	{
		if (policy > 1 || (!policy && ownerThread != std::this_thread::get_id()))
		{
			throw std::runtime_error("Binding requires its owner thread");
		}
	}

	virtual void CheckHandle(const void*, Size, UInt32) const
	{
		throw std::runtime_error("This binding context does not expose object handles");
	}

	virtual UInt64 TransferBudget() const
	{
		return maximumBytes;
	}

	virtual UInt64 SessionIdentity() const
	{
		return 0;
	}

private:
	std::thread::id ownerThread = std::this_thread::get_id();
	std::shared_ptr<Detail::ContextLease> lease;
	UIntPtr contextToken = 0;
};

class BindingArena final
{
public:
	explicit BindingArena(UInt64 limit = maximumBytes) : budget(limit)
	{
		if (limit > maximumBytes)
		{
			throw std::runtime_error("Binding transfer budget exceeds its hard limit");
		}
	}

	void* Allocate(UInt64 count, UInt64 stride)
	{
		if (!stride || count > (budget - allocated) / stride)
		{
			throw std::runtime_error("Binding transfer capacity exceeded");
		}
		const UInt64 bytes = count * stride;
		if (!bytes)
		{
			return nullptr;
		}
		std::unique_ptr<UInt8, AllocationDeleter> memory(static_cast<UInt8*>(
		    Memory::Allocate(Size(bytes), alignof(std::max_align_t), Memory::MemoryTag::Temporary)));
		std::memset(memory.get(), 0, Size(bytes));
		void* pointer = memory.get();
		ownedRanges.emplace(reinterpret_cast<UIntPtr>(pointer), bytes);
		try
		{
			allocations.push_back(std::move(memory));
		}
		catch (...)
		{
			ownedRanges.erase(reinterpret_cast<UIntPtr>(pointer));
			throw;
		}
		allocated += bytes;
		return pointer;
	}

	void RequireOwned(const void* data, UInt64 count, UInt64 stride) const
	{
		if (!stride || count > maximumBytes / stride)
		{
			throw std::runtime_error("Binding output range exceeds its budget");
		}
		if (!count)
		{
			return;
		}
		const UIntPtr address = reinterpret_cast<UIntPtr>(data);
		auto range = ownedRanges.upper_bound(address);
		if (range != ownedRanges.begin())
		{
			--range;
			const UInt64 offset = address - range->first;
			if (offset <= range->second && count * stride <= range->second - offset)
			{
				return;
			}
		}
		throw std::runtime_error("Binding output is not owned by its result arena");
	}

	Utf8View StringValue(const String& value)
	{
		auto* bytes = static_cast<UInt8*>(Allocate(value.size(), 1));
		if (!value.empty())
		{
			std::memcpy(bytes, value.data(), value.size());
		}
		return {bytes, value.size(), 1, 0};
	}

	template <class T> DataView Array(const T* source, Size count)
	{
		auto* data = static_cast<T*>(Allocate(count, sizeof(T)));
		if (count)
		{
			if (!source)
			{
				throw std::runtime_error("Nonempty binding array has no source");
			}
			std::memcpy(data, source, count * sizeof(T));
		}
		return {data, count, 1, 0};
	}

	void Transfer(BindingOutcome& outcome)
	{
		if (allocations.empty())
		{
			return;
		}
		auto owned = std::make_unique<BindingArena>(budget);
		owned->allocations = std::move(allocations);
		owned->ownedRanges = std::move(ownedRanges);
		owned->allocated = allocated;
		allocated = 0;
		outcome.owner = owned.release();
		outcome.release = [](void* pointer) { delete static_cast<BindingArena*>(pointer); };
	}

private:
	struct AllocationDeleter
	{
		void operator()(UInt8* pointer) const noexcept
		{
			Memory::Free(pointer);
		}
	};

	std::vector<std::unique_ptr<UInt8, AllocationDeleter>> allocations;
	std::map<UIntPtr, UInt64> ownedRanges;
	UInt64 allocated = 0;
	UInt64 budget;
};

struct BindingCall
{
	BindingContext& context;
	BindingArena arena;
};

template <class F> Int32 Invoke(void* context, BindingOutcome* outcome, UInt32 threadPolicy, F&& function) noexcept
{
	if (!outcome)
	{
		return 1;
	}
	*outcome = {};
	UInt64 budget = maximumBytes;
	UInt64 sessionIdentity = 0;
	try
	{
		auto lease = BindingContext::Acquire(context);
		std::lock_guard active(lease->gate);
		if (!lease->source)
		{
			throw std::runtime_error("Binding context is closed");
		}
		auto& source = *lease->source;
		source.Require(threadPolicy);
		sessionIdentity = source.SessionIdentity();
		outcome->sessionIdentity = sessionIdentity;
		budget = source.TransferBudget();
		BindingCall call{source, BindingArena(budget)};
		function(call);
		call.arena.Transfer(*outcome);
		return 0;
	}
	catch (const std::exception& error)
	{
		try
		{
			BindingArena arena(budget);
			outcome->error =
			    arena.StringValue(ToUrsaString("session " + std::to_string(sessionIdentity) + ": " + error.what()));
			arena.Transfer(*outcome);
		}
		catch (...)
		{
			*outcome = {};
		}
		outcome->sessionIdentity = sessionIdentity;
		return 1;
	}
	catch (...)
	{
		outcome->sessionIdentity = sessionIdentity;
		return 2;
	}
}

inline void Release(BindingOutcome& outcome)
{
	BindingOutcome owned = outcome;
	outcome = {};
	if (owned.owner && owned.release)
	{
		owned.release(owned.owner);
	}
}

template <class T> struct CallbackValue
{
	T value{};
};

template <> struct CallbackValue<void>
{
};

template <class T> struct CallbackResult final : CallbackValue<T>
{
	Int32 status = 1;
	BindingOutcome outcome;
	const char* entry = "callback";

	CallbackResult() = default;
	CallbackResult(const CallbackResult&) = delete;
	CallbackResult& operator=(const CallbackResult&) = delete;

	CallbackResult(CallbackResult&& other) noexcept
	    : CallbackValue<T>(other), status(other.status), outcome(std::exchange(other.outcome, {})), entry(other.entry)
	{
	}

	~CallbackResult()
	{
		Release(outcome);
	}

	explicit operator bool() const
	{
		return status == 0;
	}

	String Error() const
	{
		return String(entry) + " (session " + ToUrsaString(std::to_string(outcome.sessionIdentity)) +
		       "): " + (outcome.error.present ? ReadString(outcome.error) : "Callback failed without error text");
	}
};

template <class Table> struct CallbackModule
{
	Table table;
	void* context = nullptr;
};

template <class Table>
Result<CallbackModule<Table>> ConnectCallbackModule(const BindingModuleDescriptor* descriptor,
                                                    const BindingModuleDescriptor& expected, void* context)
{
	if (!descriptor || descriptor->version != expected.version || descriptor->size != sizeof(BindingModuleDescriptor) ||
	    descriptor->pointerSize != sizeof(void*) || descriptor->functionCount != expected.functionCount ||
	    !descriptor->functions || !descriptor->validateContext || std::memcmp(descriptor->hash, expected.hash, 32) != 0)
	{
		return Result<CallbackModule<Table>>::Failure("Callback binding module contract mismatch");
	}
	CallbackResult<void> validation;
	validation.status = descriptor->validateContext(context, &validation.outcome);
	if (!validation)
	{
		return Result<CallbackModule<Table>>::Failure(validation.Error());
	}
	// Copy framework static thunks. Never retain the registration's table allocation.
	CallbackModule<Table> result;
	std::memcpy(&result.table, descriptor->functions, sizeof(Table));
	result.context = context;
	return Result<CallbackModule<Table>>::Success(result);
}

inline Int32 ValidateBindingContext(void* context, BindingOutcome* outcome) noexcept
{
	return Invoke(context, outcome, 0, [](BindingCall&) {});
}
} // namespace Ursa::Scripting::Bindings
