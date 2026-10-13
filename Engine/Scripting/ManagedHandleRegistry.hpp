#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <atomic>
#include <memory>
#include <thread>

namespace Ursa::Scripting
{
enum class ManagedHandleKind : UInt32
{
	GameObject = 1,
	Component = 2,
	Asset = 3,
	Mesh = 4,
	Material = 5,
	RenderTexture = 6,
	GraphicsBuffer = 7
};

struct ManagedHandle
{
	UInt64 sessionId = 0;
	UInt32 index = 0;
	UInt32 generation = 0;
	ManagedHandleKind kind = ManagedHandleKind::GameObject;
	UInt32 reserved = 0;
};

static_assert(sizeof(ManagedHandle) == 24, "ManagedHandle layout changed");

class ManagedHandleRegistry final
{
public:
	static Result<std::unique_ptr<ManagedHandleRegistry>> Create();

	Result<ManagedHandle> Register(void* object, ManagedHandleKind kind);

	Result<void*> Resolve(const ManagedHandle& handle, ManagedHandleKind expectedKind) const;

	template <class T> Result<T*> ResolveAs(const ManagedHandle& handle, ManagedHandleKind expectedKind) const
	{
		auto object = Resolve(handle, expectedKind);
		return object ? Result<T*>::Success(static_cast<T*>(object.value)) : Result<T*>::Failure(object.error);
	}

	Result<void> Invalidate(const ManagedHandle& handle);

	Result<void> InvalidateAll();

	UInt64 SessionId() const
	{
		return sessionId;
	}

private:
	struct Slot
	{
		void* object = nullptr;
		ManagedHandleKind kind = ManagedHandleKind::GameObject;
		UInt32 generation = 1;
	};

	explicit ManagedHandleRegistry(UInt64 session);

	Result<void> RequireMainThread() const;

	void AdvanceGeneration(Slot& slot);

	UInt64 sessionId;
	std::thread::id mainThread;
	Vector<Slot> slots;
	Vector<UInt32> freeIndices;
	UnorderedMap<const void*, UInt32> objectIndices;
};
} // namespace Ursa::Scripting
