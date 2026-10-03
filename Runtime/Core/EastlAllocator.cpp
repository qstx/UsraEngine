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

void* allocator::allocate(size_t size, int flags)
{
	(void)flags;
	void* memory = _aligned_malloc(std::max<size_t>(size, 1), alignof(std::max_align_t));
	if (!memory)
	{
		throw std::bad_alloc();
	}
	return memory;
}

void* allocator::allocate(size_t size, size_t alignment, size_t offset, int flags)
{
	(void)flags;
	if (alignment == 0 || (alignment & (alignment - 1)) != 0 || offset == SIZE_MAX)
	{
		throw std::bad_alloc();
	}
	alignment = std::max(alignment, sizeof(void*));
	void* memory = _aligned_offset_malloc(std::max(size, offset + 1), alignment, offset);
	if (!memory)
	{
		throw std::bad_alloc();
	}
	return memory;
}

void allocator::deallocate(void* memory, size_t)
{
	_aligned_free(memory);
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
