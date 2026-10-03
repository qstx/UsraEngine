#include <EASTL/allocator.h>
#include <EASTL/vector.h>

#include <cstdint>

int main()
{
	eastl::allocator allocator;
	void* ordinary = allocator.allocate(37);
	if (!ordinary)
	{
		return 1;
	}
	allocator.deallocate(ordinary, 37);

	void* aligned = allocator.allocate(128, 64, 8);
	if (!aligned)
	{
		return 2;
	}
	if ((reinterpret_cast<uintptr_t>(aligned) + 8) % 64 != 0)
	{
		return 3;
	}
	allocator.deallocate(aligned, 128);

	eastl::vector<int> values;
	for (int index = 0; index < 128; ++index)
	{
		values.push_back(index);
	}
	if (values.size() != 128 || values.back() != 127)
	{
		return 4;
	}
}
