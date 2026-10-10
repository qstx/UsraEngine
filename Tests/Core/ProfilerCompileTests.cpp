#include <Core/Profiler.hpp>
#include <Core/Containers.hpp>

int main()
{
	Ursa::String message = "Profiler facade compile test";
	URSA_PROFILE_FRAME("Ursa Profiler Compile Test");
	URSA_PROFILE_SCOPE("Profiler Facade Compile Test");
	URSA_PROFILE_SCOPE_DYNAMIC(message.data(), message.size());
	URSA_PROFILE_MESSAGE(message);
	return 0;
}
