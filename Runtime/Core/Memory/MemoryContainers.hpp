#pragma once

#include <Core/Memory/ContainerAllocator.hpp>
#include <EASTL/vector.h>
#include <EASTL/map.h>

namespace Ursa
{
template <class T> using MemoryVector = eastl::vector<T, Memory::ContainerAllocator>;

template <class T> using TemporaryVector = eastl::vector<T, Memory::TemporaryContainerAllocator>;

template <class Key, class Value>
using MemoryMap = eastl::map<Key, Value, eastl::less<Key>, Memory::ContainerAllocator>;

template <class Key, class Value>
using TemporaryMap = eastl::map<Key, Value, eastl::less<Key>, Memory::TemporaryContainerAllocator>;
} // namespace Ursa
