#pragma once

#include <Core/Memory/Memory.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <limits>
#include <new>
#include <type_traits>

namespace Ursa::Memory
{
class ContainerAllocator
{
public:
	explicit ContainerAllocator(Allocator& allocator = DefaultAllocator(), MemoryTag tag = MemoryTag::Containers)
	    : allocator(&allocator), tag(tag)
	{
	}

	// EASTL allocator protocol keeps its vendor-defined method names.
	void* allocate(Size size, int = 0)
	{
		return allocator->Allocate(size, alignof(std::max_align_t), tag);
	}

	void* allocate(Size size, Size alignment, Size offset, int = 0)
	{
		return allocator->Allocate(size, alignment, tag, offset);
	}

	void deallocate(void* pointer, Size) noexcept
	{
		Free(pointer);
	}

	const char* get_name() const noexcept
	{
		return name;
	}

	void set_name(const char* value) noexcept
	{
		name = value ? value : "Ursa.Memory";
	}

	bool operator==(const ContainerAllocator& other) const noexcept
	{
		return allocator == other.allocator && tag == other.tag;
	}

	bool operator!=(const ContainerAllocator& other) const noexcept
	{
		return !(*this == other);
	}

private:
	Allocator* allocator;
	MemoryTag tag;
	const char* name = "Ursa.Memory";
};

class TemporaryContainerAllocator final : public ContainerAllocator
{
public:
	explicit TemporaryContainerAllocator(MemoryTag tag = MemoryTag::Temporary)
	    : ContainerAllocator(ThreadTemporaryAllocator(), tag)
	{
	}

	explicit TemporaryContainerAllocator(LinearAllocator& allocator, MemoryTag tag = MemoryTag::Temporary)
	    : ContainerAllocator(allocator, tag)
	{
	}
};

template <class T> class StlAllocator
{
public:
	using value_type = T;
	using is_always_equal = std::false_type;

	explicit StlAllocator(Allocator& allocator = DefaultAllocator(), MemoryTag tag = MemoryTag::Core) noexcept
	    : allocator(&allocator), tag(tag)
	{
	}

	template <class U> StlAllocator(const StlAllocator<U>& other) noexcept : allocator(other.allocator), tag(other.tag)
	{
	}

	// Standard-library Allocator protocol keeps its standard-defined method names.
	T* allocate(Size count)
	{
		if (count > std::numeric_limits<Size>::max() / sizeof(T))
		{
			throw std::bad_array_new_length();
		}
		return static_cast<T*>(allocator->Allocate(count * sizeof(T), alignof(T), tag));
	}

	void deallocate(T* pointer, Size) noexcept
	{
		Free(pointer);
	}

	template <class U> bool operator==(const StlAllocator<U>& other) const noexcept
	{
		return allocator == other.allocator && tag == other.tag;
	}

	template <class U> bool operator!=(const StlAllocator<U>& other) const noexcept
	{
		return !(*this == other);
	}

private:
	template <class U> friend class StlAllocator;
	Allocator* allocator;
	MemoryTag tag;
};
} // namespace Ursa::Memory
