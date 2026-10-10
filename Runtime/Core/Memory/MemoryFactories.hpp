#pragma once

#include <Core/Memory/ContainerAllocator.hpp>
#include <memory>
#include <type_traits>
#include <utility>

namespace Ursa
{
template <class T, class... Args> T* New(Memory::Allocator& allocator, Memory::MemoryTag tag, Args&&... args)
{
	void* storage = allocator.Allocate(sizeof(T), alignof(T), tag);
	try
	{
		return ::new (storage) T(std::forward<Args>(args)...);
	}
	catch (...)
	{
		Memory::Free(storage);
		throw;
	}
}

template <class T, class... Args> T* New(Memory::MemoryTag tag, Args&&... args)
{
	return New<T>(Memory::DefaultAllocator(), tag, std::forward<Args>(args)...);
}

template <class T> void Delete(T* pointer) noexcept
{
	if (pointer)
	{
		pointer->~T();
		Memory::Free(pointer);
	}
}

template <class T> struct MemoryDeleter
{
	void operator()(T* pointer) const noexcept
	{
		Delete(pointer);
	}
};

template <class T> using UniquePtr = std::unique_ptr<T, MemoryDeleter<T>>;

template <class T, class... Args>
UniquePtr<T> MakeUnique(Memory::Allocator& allocator, Memory::MemoryTag tag, Args&&... args)
{
	T* object = New<T>(allocator, tag, std::forward<Args>(args)...);
	return UniquePtr<T>(object);
}

template <class T, class... Args> UniquePtr<T> MakeUnique(Memory::MemoryTag tag, Args&&... args)
{
	return MakeUnique<T>(Memory::DefaultAllocator(), tag, std::forward<Args>(args)...);
}

template <class T, class... Args>
std::shared_ptr<T> MakeShared(Memory::Allocator& allocator, Memory::MemoryTag tag, Args&&... args)
{
	return std::allocate_shared<T>(Memory::StlAllocator<T>(allocator, tag), std::forward<Args>(args)...);
}

template <class T, class... Args> std::shared_ptr<T> MakeShared(Memory::MemoryTag tag, Args&&... args)
{
	return MakeShared<T>(Memory::DefaultAllocator(), tag, std::forward<Args>(args)...);
}
} // namespace Ursa
