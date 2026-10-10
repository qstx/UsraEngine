#include <Core/Types.hpp>
#include <Core/Memory/Memory.hpp>
#include <EASTL/allocator.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <malloc.h>
#include <new>

namespace eastl
{
allocator::allocator(const char* name)
{
#if EASTL_NAME_ENABLED
	mpName = name ? name : "Ursa";
#else
	(void)name;
#endif
}

allocator::allocator(const allocator& other)
{
#if EASTL_NAME_ENABLED
	mpName = other.mpName;
#else
	(void)other;
#endif
}

allocator::allocator(const allocator&, const char* name) : allocator(name)
{
}

allocator& allocator::operator=(const allocator& other)
{
#if EASTL_NAME_ENABLED
	mpName = other.mpName;
#else
	(void)other;
#endif
	return *this;
}

void* allocator::allocate(Ursa::Size size, int flags)
{
	(void)flags;
	return Ursa::Memory::Allocate(size, alignof(std::max_align_t), Ursa::Memory::MemoryTag::Containers);
}

void* allocator::allocate(Ursa::Size size, Ursa::Size alignment, Ursa::Size offset, int flags)
{
	(void)flags;
	if (alignment == 0 || (alignment & (alignment - 1)) != 0 || offset == SIZE_MAX)
	{
		throw std::bad_alloc();
	}
	return Ursa::Memory::Allocate(size, alignment, Ursa::Memory::MemoryTag::Containers, offset);
}

void allocator::deallocate(void* memory, Ursa::Size)
{
	Ursa::Memory::Free(memory);
}

const char* allocator::get_name() const
{
#if EASTL_NAME_ENABLED
	return mpName;
#else
	return "Ursa";
#endif
}

void allocator::set_name(const char* name)
{
#if EASTL_NAME_ENABLED
	mpName = name ? name : "Ursa";
#else
	(void)name;
#endif
}

bool operator==(const allocator&, const allocator&)
{
	return true;
}

#if !defined(EA_COMPILER_HAS_THREE_WAY_COMPARISON)
bool operator!=(const allocator&, const allocator&)
{
	return false;
}
#endif

namespace
{
allocator defaultAllocator;
std::atomic<allocator*> currentAllocator{&defaultAllocator};
} // namespace

allocator* GetDefaultAllocator()
{
	return currentAllocator.load();
}

allocator* SetDefaultAllocator(allocator* value)
{
	return currentAllocator.exchange(value ? value : &defaultAllocator);
}
} // namespace eastl
