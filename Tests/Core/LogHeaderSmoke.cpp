#include <Core/Log.hpp>

int main()
{
	Ursa::LogSession log;
	if (!log.GetStatus())
	{
		return 1;
	}
	URSA_LOG_INFO("Example", "Logging ready");
	return 0;
}
