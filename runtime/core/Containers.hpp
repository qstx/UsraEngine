#pragma once

#include <EASTL/array.h>
#include <EASTL/deque.h>
#include <EASTL/map.h>
#include <EASTL/set.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <cstddef>

namespace Ursa
{
using String = eastl::string;

template <class T> using Vector = eastl::vector<T>;

template <class T, size_t size> using Array = eastl::array<T, size>;

template <class Key, class Value> using Map = eastl::map<Key, Value>;

template <class Key, class Value> using UnorderedMap = eastl::unordered_map<Key, Value>;

template <class Key> using Set = eastl::set<Key>;

template <class T> using Deque = eastl::deque<T>;
} // namespace Ursa
