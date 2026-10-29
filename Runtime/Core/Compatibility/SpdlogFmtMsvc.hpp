#pragma once

#include <iterator>

// The pinned fmt selects a removed stdext iterator when _SECURE_SCL is set.
// Select its ordinary pointer adapter only while parsing fmt. STL debug levels
// and the caller's compatibility macro remain unchanged.
#pragma push_macro("_SECURE_SCL")
#undef _SECURE_SCL
#define _SECURE_SCL 0
#include <spdlog/fmt/bundled/format.h>
#pragma pop_macro("_SECURE_SCL")
