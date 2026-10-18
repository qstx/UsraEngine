#include <exception>
#include <iostream>

int TestContainerHeader();
int TestLogHeader();
int TestRuntimeHeader();
int TestMaterialHeader();
int TestLightingHeader();
int TestSerializableHeader();
#if defined(URSA_HEADER_SMOKE_EDITOR)
int TestMaterialInspectorHeader();
#endif

int main()
{
	const struct
	{
		const char* name;
		int (*run)();
	} examples[] = {
	    {"Container", TestContainerHeader},
	    {"Log", TestLogHeader},
	    {"Runtime", TestRuntimeHeader},
	    {"Material", TestMaterialHeader},
	    {"Lighting", TestLightingHeader},
	    {"Serializable", TestSerializableHeader},
#if defined(URSA_HEADER_SMOKE_EDITOR)
	    {"MaterialInspector", TestMaterialInspectorHeader},
#endif
	};
	int failures = 0;
	for (const auto& example : examples)
	{
		try
		{
			if (example.run() == 0)
			{
				continue;
			}
			std::cerr << example.name << " header example failed\n";
		}
		catch (const std::exception& error)
		{
			std::cerr << example.name << " header example: " << error.what() << '\n';
		}
		++failures;
	}
	return failures == 0 ? 0 : 1;
}
