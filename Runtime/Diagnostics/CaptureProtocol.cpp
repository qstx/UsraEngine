#include "CaptureStorage.hpp"
#include <atomic>
#include <iomanip>
#include <random>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace Ursa::Diagnostics
{
UInt32 CaptureProcessId()
{
#ifdef _WIN32
	return GetCurrentProcessId();
#else
	return static_cast<UInt32>(getpid());
#endif
}

Int64 CaptureUtcMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

String NewCaptureId()
{
	static std::atomic<UInt64> sequence{0};
	std::random_device random;
	std::ostringstream text;
	text << std::hex << std::setfill('0') << std::setw(16) << ((UInt64(random()) << 32) ^ random() ^ ++sequence)
	     << std::setw(16) << ((UInt64(random()) << 32) ^ random() ^ UInt64(CaptureUtcMilliseconds()));
	return ToUrsaString(text.str());
}

String SerializeCaptureRequest(const CaptureRequest& request)
{
	return Detail::Text({{"Version", 1},
	                     {"Pid", request.pid},
	                     {"SessionId", ToStdString(request.sessionId)},
	                     {"RequestId", ToStdString(request.requestId)},
	                     {"Output", request.output.u8string()},
	                     {"DeadlineUtc", request.deadlineUtc}});
}

Result<CaptureRequest> ParseCaptureRequest(const String& text)
{
	try
	{
		const auto json = Detail::Json(text);
		Detail::CheckVersion(json);
		CaptureRequest request;
		request.pid = Detail::ReadPid(json);
		request.sessionId = ToUrsaString(json.at("SessionId").get<std::string>());
		request.requestId = ToUrsaString(json.at("RequestId").get<std::string>());
		const auto output = json.at("Output").get<std::string>();
		if (output.find('\0') != std::string::npos)
		{
			throw std::runtime_error("Invalid output path");
		}
		request.output = std::filesystem::u8path(output);
		if (!json.at("DeadlineUtc").is_number_integer())
		{
			throw std::runtime_error("Invalid capture deadline");
		}
		request.deadlineUtc = json.at("DeadlineUtc").get<Int64>();
		if (!Detail::ValidId(request.sessionId) || !Detail::ValidId(request.requestId) ||
		    !request.output.is_absolute() || request.output.filename().empty() || request.deadlineUtc <= 0)
		{
			throw std::runtime_error("Invalid capture request fields");
		}
		return Result<CaptureRequest>::Success(std::move(request));
	}
	catch (const std::exception& error)
	{
		return Result<CaptureRequest>::Failure(ToUrsaString(error.what()));
	}
}

String SerializeCaptureResult(const CaptureResult& result)
{
	auto json = Detail::Json(SerializeCaptureRequest(result.request));
	if (!result.request.pid)
	{
		json["Pid"] = nullptr;
	}
	if (result.request.sessionId.empty())
	{
		json["SessionId"] = nullptr;
	}
	json["Status"] = ToStdString(result.status);
	json["Message"] = ToStdString(result.message);
	if (result.Succeeded())
	{
		json["Width"] = result.width;
		json["Height"] = result.height;
		json["FrameSerial"] = result.frameSerial;
		json["CapturedAtUtc"] = result.capturedAtUtc;
	}
	// Native exception messages can contain bytes in the host ANSI code page.
	return ToUrsaString(json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

Result<CaptureResult> ParseCaptureResult(const String& text)
{
	try
	{
		CaptureResult result;
		result.request = Detail::Take(ParseCaptureRequest(text));
		const auto json = Detail::Json(text);
		result.status = ToUrsaString(json.at("Status").get<std::string>());
		result.message = ToUrsaString(json.at("Message").get<std::string>());
		const String allowed[] = {"Success", "InvalidRequest", "TargetUnavailable",   "NotReady",      "Busy",
		                          "Timeout", "TargetClosed",   "UnsupportedProtocol", "CaptureFailed", "WriteFailed"};
		if (std::find(std::begin(allowed), std::end(allowed), result.status) == std::end(allowed))
		{
			throw std::runtime_error("Unknown capture result status");
		}
		if (result.Succeeded())
		{
			const auto width = json.at("Width").get<Int64>();
			const auto height = json.at("Height").get<Int64>();
			if (width < 1 || height < 1 || width > std::numeric_limits<UInt32>::max() ||
			    height > std::numeric_limits<UInt32>::max())
			{
				throw std::runtime_error("Invalid capture dimensions");
			}
			result.width = static_cast<UInt32>(width);
			result.height = static_cast<UInt32>(height);
			result.frameSerial = json.at("FrameSerial").get<UInt64>();
			result.capturedAtUtc = json.at("CapturedAtUtc").get<Int64>();
			if (!result.frameSerial || result.capturedAtUtc <= 0)
			{
				throw std::runtime_error("Invalid capture completion metadata");
			}
		}
		return Result<CaptureResult>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<CaptureResult>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Diagnostics
