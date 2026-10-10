#pragma once

#include <Core/Memory/MemoryContainers.hpp>
#include <algorithm>
#include <iterator>
#include <utility>

namespace Ursa
{
namespace Memory::Detail
{
template <class Iterator, class Value, class Compare>
void SortBuffered(Iterator first, Iterator last, Value* buffer, Compare& compare)
{
	const auto count = last - first;
	if (count < 2)
	{
		return;
	}
	if (count <= 16)
	{
		for (auto current = first + 1; current != last; ++current)
		{
			Value value = std::move(*current);
			auto destination = current;
			while (destination != first && compare(value, *(destination - 1)))
			{
				*destination = std::move(*(destination - 1));
				--destination;
			}
			*destination = std::move(value);
		}
		return;
	}
	const auto half = count / 2;
	const auto middle = first + half;
	SortBuffered(first, middle, buffer, compare);
	SortBuffered(middle, last, buffer + half, compare);
	if (!compare(*middle, *(middle - 1)))
	{
		return;
	}
	auto left = first;
	auto right = middle;
	auto output = buffer;
	while (left != middle && right != last)
	{
		if (compare(*right, *left))
		{
			*output++ = std::move(*right++);
		}
		else
		{
			*output++ = std::move(*left++);
		}
	}
	output = std::move(left, middle, output);
	std::move(right, last, output);
	std::move(buffer, buffer + count, first);
}
} // namespace Memory::Detail

template <class Iterator, class Compare>
void StableSort(Iterator first, Iterator last, Compare compare, Memory::MemoryTag tag = Memory::MemoryTag::Temporary)
{
	const auto count = last - first;
	if (count < 2)
	{
		return;
	}
	using Value = typename std::iterator_traits<Iterator>::value_type;
	Memory::TemporaryScope scope;
	TemporaryVector<Value> scratch{Memory::TemporaryContainerAllocator(tag)};
	scratch.resize(static_cast<Size>(count));
	Memory::Detail::SortBuffered(first, last, scratch.data(), compare);
}
} // namespace Ursa
