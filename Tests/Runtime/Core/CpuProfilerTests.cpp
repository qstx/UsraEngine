#include <Core/CpuProfiler.hpp>
#include <Core/Profiler.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main(int argc, char** argv)
{
	try
	{
		using namespace Ursa::Profiling;
		Expect(!RegisterCpuMarker("", "", "", 0), "Empty name was accepted");
		auto outer = RegisterCpuMarker("Managed.Test.Outer", __FILE__, "Outer", __LINE__);
		auto inner = RegisterCpuMarker("Managed.Test.Inner", __FILE__, "Inner", 1);
		auto same = RegisterCpuMarker("Managed.Test.Inner", __FILE__, "Inner", 1);
		Expect(outer && inner && same, "Marker registration failed");
		Expect(inner.value == same.value, "Metadata was not deduplicated");
		if (CpuMarkersEnabled())
		{
			Expect(outer.value != inner.value && !BeginCpuMarker(0), "Marker identities were not validated");
			auto parent = BeginCpuMarker(outer.value);
			auto child = BeginCpuMarker(inner.value);
			Expect(parent && child && !EndCpuMarker(parent.value), "Out of order end was accepted");
			bool wrongThreadRejected = false;
			std::thread worker([&] { wrongThreadRejected = !EndCpuMarker(child.value); });
			worker.join();
			Expect(wrongThreadRejected, "Cross thread end was accepted");
			Expect(bool(EndCpuMarker(child.value)) && !EndCpuMarker(child.value), "Duplicate end was accepted");
			Expect(bool(EndCpuMarker(parent.value)), "Rejected end damaged the stack");
		}
		else
		{
			Expect(outer.value == 0 && BeginCpuMarker(0).value == 0 && EndCpuMarker(0),
			       "Disabled markers were not inert");
		}
		const int seconds = argc > 1 ? std::stoi(argv[1]) : 0;
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
		do
		{
			auto parent = BeginCpuMarker(outer.value);
			auto child = BeginCpuMarker(inner.value);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			Expect(bool(EndCpuMarker(child.value)) && bool(EndCpuMarker(parent.value)), "Capture scopes failed");
			URSA_PROFILE_FRAME("Managed marker test");
		} while (std::chrono::steady_clock::now() < until);
		std::cout << "CPU marker identities, nesting, thread validation and optional capture passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
