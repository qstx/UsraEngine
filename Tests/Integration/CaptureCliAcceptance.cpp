#include "CaptureCliAcceptance.hpp"
#include <Core/StringInterop.hpp>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace Ursa::Test
{
Result<Diagnostics::CaptureResult> RunCaptureCli(const std::filesystem::path& output)
{
	try
	{
#ifdef _WIN32
		struct Handle
		{
			HANDLE value = nullptr;

			~Handle()
			{
				if (value && value != INVALID_HANDLE_VALUE)
				{
					CloseHandle(value);
				}
			}
		};

		Handle read;
		Handle write;
		SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
		if (!CreatePipe(&read.value, &write.value, &attributes, 0) ||
		    !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
		{
			throw std::runtime_error("Cannot create capture CLI output pipe");
		}
		const auto executable = std::filesystem::u8path(URSA_CAPTURE_CLI);
		auto command = L"\"" + executable.wstring() + L"\" --pid " + std::to_wstring(Diagnostics::CaptureProcessId()) +
		               L" --output \"" + output.wstring() + L"\" --timeout-ms 10000";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = write.value;
		startup.hStdError = write.value;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
		                    nullptr, &startup, &process))
		{
			throw std::runtime_error("Cannot start external capture CLI");
		}
		Handle processHandle{process.hProcess};
		Handle threadHandle{process.hThread};
		CloseHandle(write.value);
		write.value = nullptr;
		String text;
		char bytes[2048];
		DWORD received = 0;
		while (ReadFile(read.value, bytes, sizeof(bytes), &received, nullptr) && received)
		{
			if (text.size() + received > 64 * 1024)
			{
				throw std::runtime_error("Capture CLI output exceeded its limit");
			}
			text.append(bytes, received);
		}
		WaitForSingleObject(processHandle.value, 15000);
		DWORD exitCode = 0;
		GetExitCodeProcess(processHandle.value, &exitCode);
		if (exitCode != 0)
		{
			throw std::runtime_error("Capture CLI failed: " + ToStdString(text));
		}
		return Diagnostics::ParseCaptureResult(text);
#else
		Diagnostics::CaptureClient client(Diagnostics::CaptureProcessId(), output, 10000);
		return Result<Diagnostics::CaptureResult>::Success(client.Wait());
#endif
	}
	catch (const std::exception& error)
	{
		return Result<Diagnostics::CaptureResult>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Test
