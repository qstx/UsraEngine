#include "CaptureStorage.hpp"
#include <thread>

namespace Ursa::Diagnostics
{
using namespace Detail;

struct CaptureClient::Impl
{
	CaptureStorage storage;
	CaptureRequest request;
	std::optional<CaptureResult> terminal;
	std::chrono::steady_clock::time_point expires;
	bool submitted = false;

	void Finish(const String& status, const String& message)
	{
		CaptureResult result;
		result.request = request;
		result.status = status;
		result.message = message;
		terminal = result;
	}

	void Cleanup()
	{
		if (!submitted)
		{
			return;
		}
		try
		{
			if (!terminal || terminal->status == "Timeout")
			{
				storage.Write(MessagePath(request, "cancel"), "{}");
			}
			else
			{
				storage.Remove(MessagePath(request, "cancel"));
			}
			storage.Remove(MessagePath(request, "inbox"));
			storage.Remove(MessagePath(request, "replies"));
		}
		catch (...)
		{
		}
		submitted = false;
	}
};

CaptureClient::CaptureClient(UInt32 pid, const std::filesystem::path& output, UInt32 timeoutMs,
                             const std::filesystem::path& root)
    : impl(std::make_unique<Impl>())
{
	impl->expires = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	impl->request.pid = pid;
	try
	{
		impl->request.requestId = NewCaptureId();
		if (!pid || !timeoutMs || output.empty() || output.filename().empty() ||
		    output.u8string().find('\0') != std::string::npos)
		{
			throw std::runtime_error("Capture needs a positive PID, timeout and output filename");
		}
		impl->request.output = std::filesystem::absolute(output).lexically_normal();
		impl->request.deadlineUtc = CaptureUtcMilliseconds() + timeoutMs;
		try
		{
			impl->storage.Open(root, false);
		}
		catch (const std::exception& error)
		{
			impl->Finish("TargetUnavailable", ToUrsaString(error.what()));
			return;
		}
		if (!impl->storage.Exists(Registry(pid)))
		{
			impl->Finish("TargetUnavailable", "Capture instance is unavailable");
			return;
		}
		const auto registration = Json(impl->storage.Read(Registry(pid)));
		impl->request.sessionId = RegistrationSession(registration, pid);
		if (!registration.at("Ready").get<bool>())
		{
			impl->Finish("NotReady", "Window is not ready for capture");
			return;
		}
		impl->storage.Write(MessagePath(impl->request, "inbox"), SerializeCaptureRequest(impl->request));
		impl->submitted = true;
	}
	catch (const std::exception& error)
	{
		const auto message = ToUrsaString(error.what());
		impl->Finish(message.find("UnsupportedProtocol") != String::npos ? "UnsupportedProtocol" : "InvalidRequest",
		             message);
	}
}

CaptureClient::~CaptureClient()
{
	impl->Cleanup();
}

std::optional<CaptureResult> CaptureClient::Poll()
{
	if (impl->terminal)
	{
		return impl->terminal;
	}
	try
	{
		if (std::chrono::steady_clock::now() >= impl->expires)
		{
			impl->Finish("Timeout", "Capture deadline exceeded");
		}
		else if (impl->storage.Exists(MessagePath(impl->request, "replies")))
		{
			const auto parsed = ParseCaptureResult(impl->storage.Read(MessagePath(impl->request, "replies")));
			if (!parsed)
			{
				impl->Finish("CaptureFailed", parsed.error);
			}
			else if (parsed.value.request.pid == impl->request.pid &&
			         parsed.value.request.sessionId == impl->request.sessionId &&
			         parsed.value.request.requestId == impl->request.requestId)
			{
				if (parsed.value.Succeeded() && (parsed.value.request.output != impl->request.output ||
				                                 !std::filesystem::is_regular_file(impl->request.output)))
				{
					impl->Finish("CaptureFailed", "Success result does not match a saved output");
				}
				else
				{
					impl->terminal = parsed.value;
				}
			}
		}
		else if (!impl->storage.Exists(Registry(impl->request.pid)))
		{
			impl->Finish("TargetClosed", "Target capture endpoint closed");
		}
		else if (RegistrationSession(Json(impl->storage.Read(Registry(impl->request.pid))), impl->request.pid) !=
		         impl->request.sessionId)
		{
			impl->Finish("TargetUnavailable", "Target capture instance changed");
		}
	}
	catch (const std::exception& error)
	{
		impl->Finish("CaptureFailed", ToUrsaString(error.what()));
	}
	if (impl->terminal)
	{
		impl->Cleanup();
	}
	return impl->terminal;
}

CaptureResult CaptureClient::Wait()
{
	for (;;)
	{
		if (const auto result = Poll())
		{
			return *result;
		}
		const auto remaining = impl->expires - std::chrono::steady_clock::now();
		if (remaining > std::chrono::steady_clock::duration::zero())
		{
			std::this_thread::sleep_for(std::min(
			    remaining,
			    std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(10))));
		}
	}
}

const CaptureRequest& CaptureClient::Request() const
{
	return impl->request;
}
} // namespace Ursa::Diagnostics
