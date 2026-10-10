#pragma once

#include <atomic>
#include <array>
#include <thread>

namespace Ursa::Memory::Detail
{
class alignas(64) SpinMutex
{
public:
	// Standard Lockable protocol, used by std::lock_guard / std::unique_lock.
	void lock() noexcept
	{
		while (flag.test_and_set(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	}

	void unlock() noexcept
	{
		flag.clear(std::memory_order_release);
	}

private:
	std::atomic_flag flag = ATOMIC_FLAG_INIT;
	std::array<unsigned char, 64 - sizeof(std::atomic_flag)> padding{};
};
} // namespace Ursa::Memory::Detail
