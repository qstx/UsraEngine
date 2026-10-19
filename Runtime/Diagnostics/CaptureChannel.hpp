#pragma once

#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

namespace Ursa::Diagnostics
{
UInt32 CaptureProcessId();
Int64 CaptureUtcMilliseconds();
String NewCaptureId();

struct CaptureRequest
{
	UInt32 pid = 0;
	String sessionId;
	String requestId;
	std::filesystem::path output;
	Int64 deadlineUtc = 0;
};

struct CaptureResult
{
	CaptureRequest request;
	String status = "InvalidRequest";
	String message;
	UInt32 width = 0;
	UInt32 height = 0;
	UInt64 frameSerial = 0;
	Int64 capturedAtUtc = 0;

	bool Succeeded() const
	{
		return status == "Success";
	}
};

String SerializeCaptureRequest(const CaptureRequest& request);
Result<CaptureRequest> ParseCaptureRequest(const String& text);
String SerializeCaptureResult(const CaptureResult& result);
Result<CaptureResult> ParseCaptureResult(const String& text);

// Owns one process's diagnostic endpoint. All methods run on its window thread.
class CaptureServer final
{
public:
	CaptureServer();
	~CaptureServer();
	CaptureServer(const CaptureServer&) = delete;
	CaptureServer& operator=(const CaptureServer&) = delete;

	Result<void> Start(const String& application, const std::filesystem::path& root = {},
	                   UInt32 pid = CaptureProcessId());
	Result<void> SetReady();
	Result<void> Poll();
	const CaptureRequest* Pending();
	Result<void> Complete(const String& status, const String& message = {}, UInt32 width = 0, UInt32 height = 0,
	                      UInt64 frameSerial = 0, Int64 capturedAtUtc = 0);
	void Close();
	String SessionId() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

// Submission and waiting need no graphics device and may run in an external process.
class CaptureClient final
{
public:
	CaptureClient(UInt32 pid, const std::filesystem::path& output, UInt32 timeoutMs = 5000,
	              const std::filesystem::path& root = {});
	~CaptureClient();
	CaptureClient(const CaptureClient&) = delete;
	CaptureClient& operator=(const CaptureClient&) = delete;

	std::optional<CaptureResult> Poll();
	CaptureResult Wait();
	const CaptureRequest& Request() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace Ursa::Diagnostics
