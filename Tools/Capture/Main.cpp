#include <Diagnostics/CaptureChannel.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
Ursa::UInt32 Positive(const std::string& text)
{
	if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
	{
		throw std::runtime_error("Expected a positive integer");
	}
	const auto value = std::stoull(text);
	if (!value || value > std::numeric_limits<Ursa::UInt32>::max())
	{
		throw std::runtime_error("Integer out of range");
	}
	return static_cast<Ursa::UInt32>(value);
}
} // namespace

int RunCapture(int argc, const char* const* argv)
{
	using namespace Ursa;
	using namespace Ursa::Diagnostics;
	CaptureResult result;
	try
	{
		UInt32 pid = 0;
		UInt32 timeout = 5000;
		std::filesystem::path output;
		bool selectedTimeout = false;
		for (int index = 1; index < argc; ++index)
		{
			const std::string argument = argv[index];
			if (argument == "--help")
			{
				std::cout << "UrsaCapture --pid <positive-pid> --output <path.png> [--timeout-ms <positive-ms>]\n"
				             "Capture the next complete client-area image of a running Editor or Player.\n"
				             "Default overall timeout: 5000 ms. stdout: one UTF-8 JSON result; diagnostics: stderr.\n";
				return 0;
			}
			if (index + 1 >= argc)
			{
				throw std::runtime_error("Missing option value");
			}
			if (argument == "--pid" && !pid)
			{
				pid = Positive(argv[++index]);
			}
			else if (argument == "--output" && output.empty())
			{
				output = std::filesystem::u8path(argv[++index]);
			}
			else if (argument == "--timeout-ms" && !selectedTimeout)
			{
				timeout = Positive(argv[++index]);
				selectedTimeout = true;
			}
			else
			{
				throw std::runtime_error("Unknown or duplicate capture option");
			}
		}
		CaptureClient client(pid, output, timeout);
		result = client.Wait();
	}
	catch (const std::exception& error)
	{
		result.message = ToUrsaString(error.what());
	}
	if (!result.Succeeded())
	{
		std::cerr << result.status.c_str() << ": " << result.message.c_str() << '\n';
	}
	std::cout << SerializeCaptureResult(result).c_str() << '\n';
	return result.Succeeded() ? 0 : 1;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
	Ursa::Vector<std::string> utf8;
	Ursa::Vector<const char*> arguments;
	for (int index = 0; index < argc; ++index)
	{
		utf8.push_back(std::filesystem::path(argv[index]).u8string());
	}
	for (const auto& argument : utf8)
	{
		arguments.push_back(argument.c_str());
	}
	return RunCapture(argc, arguments.data());
}
#else
int main(int argc, char** argv)
{
	return RunCapture(argc, argv);
}
#endif
