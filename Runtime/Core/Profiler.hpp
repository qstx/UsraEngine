#pragma once

#include <cstddef>

#if defined(URSA_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#define URSA_PROFILE_JOIN_IMPL(left, right) left##right
#define URSA_PROFILE_JOIN(left, right) URSA_PROFILE_JOIN_IMPL(left, right)
#define URSA_PROFILE_DYNAMIC_SCOPE_IMPL(name, length, line)                                                            \
	ZoneNamedN(URSA_PROFILE_JOIN(ursaProfileZone_, line), "Ursa.DynamicScope", true);                                  \
	URSA_PROFILE_JOIN(ursaProfileZone_, line).Name(name, length)
#define URSA_PROFILE_FRAME(name) FrameMarkNamed(name)
#define URSA_PROFILE_SCOPE(name) ZoneScopedN(name)
#define URSA_PROFILE_SCOPE_DYNAMIC(name, length) URSA_PROFILE_DYNAMIC_SCOPE_IMPL(name, length, __LINE__)
#define URSA_PROFILE_MESSAGE(text) TracyMessage(text.data(), text.size())
#else
#define URSA_PROFILE_FRAME(name) ((void)sizeof(name))
#define URSA_PROFILE_SCOPE(name) ((void)sizeof(name))
#define URSA_PROFILE_SCOPE_DYNAMIC(name, length) ((void)sizeof(name), (void)sizeof(length))
#define URSA_PROFILE_MESSAGE(text) ((void)sizeof(text))
#endif
