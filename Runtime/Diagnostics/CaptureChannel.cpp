#include "CaptureStorage.hpp"
#include <thread>

namespace Ursa::Diagnostics
{
using namespace Detail;

struct CaptureServer::Impl
{
	CaptureStorage storage;
	CaptureRequest identity;
	String application;
	std::shared_ptr<VirtualFileLease> lease;
	std::optional<CaptureRequest> pending;
	std::thread::id owner;
	std::chrono::steady_clock::time_point nextPoll{};
	std::chrono::steady_clock::time_point nextCleanup{};
	bool ready = false;
	bool registrationPending = false;

	void Register()
	{
		storage.Write(Registry(identity.pid), Text({{"Version", 1},
		                                            {"Pid", identity.pid},
		                                            {"SessionId", ToStdString(identity.sessionId)},
		                                            {"Application", ToStdString(application)},
		                                            {"Ready", ready}}));
	}

	bool Current() const
	{
		return storage.Exists(Registry(identity.pid)) &&
		       RegistrationSession(Json(storage.Read(Registry(identity.pid))), identity.pid) == identity.sessionId;
	}

	bool Expired(const CaptureRequest& request) const
	{
		return CaptureUtcMilliseconds() >= request.deadlineUtc || storage.Exists(MessagePath(request, "cancel"));
	}

	void Reply(const CaptureRequest& request, const String& status, const String& message)
	{
		CaptureResult result;
		result.request = request;
		result.status = status;
		result.message = message;
		storage.Write(MessagePath(request, "replies"), SerializeCaptureResult(result));
	}

	void Cleanup()
	{
		// The PID lease makes previous sessions of this PID inactive. Bound every sweep.
		std::error_code sessionError;
		const auto pidFolder = "sessions/" + ToUrsaString(std::to_string(identity.pid));
		std::filesystem::directory_iterator session(storage.root / PathFromUrsaString(pidFolder), sessionError);
		for (UInt32 visited = 0; !sessionError && session != std::filesystem::directory_iterator{} && visited < 8;
		     ++visited, session.increment(sessionError))
		{
			const auto id = PathToUrsaString(session->path().filename());
			if (!ValidId(id) || id == identity.sessionId)
			{
				continue;
			}
			const auto prefix = pidFolder + "/" + id;
			for (const char* folder : {"inbox", "replies", "cancel"})
			{
				std::error_code error;
				std::filesystem::directory_iterator entry(session->path() / folder, error);
				for (UInt32 count = 0; !error && entry != std::filesystem::directory_iterator{} && count < 4;
				     ++count, entry.increment(error))
				{
					const auto name = PathToUrsaString(entry->path().filename());
					if (entry->path().extension() == ".json" && ValidId(PathToUrsaString(entry->path().stem())))
					{
						storage.Remove(prefix + "/" + folder + "/" + name);
					}
				}
				storage.files.RemoveDirectory(storage.Path(prefix + "/" + folder));
			}
			storage.files.RemoveDirectory(storage.Path(prefix));
		}
		const auto now = std::filesystem::file_time_type::clock::now();
		for (const char* folder : {"replies", "cancel"})
		{
			std::error_code error;
			std::filesystem::directory_iterator entry(storage.root / PathFromUrsaString(Session(identity)) / folder,
			                                          error);
			for (UInt32 count = 0; !error && entry != std::filesystem::directory_iterator{} && count < 16;
			     ++count, entry.increment(error))
			{
				const auto name = PathToUrsaString(entry->path().stem());
				const auto modified = entry->last_write_time(error);
				if (!error && ValidId(name) && now - modified > std::chrono::minutes(2))
				{
					auto old = identity;
					old.requestId = name;
					storage.Remove(MessagePath(old, folder));
				}
			}
		}
	}
};

CaptureServer::CaptureServer() = default;

CaptureServer::~CaptureServer()
{
	Close();
}

Result<void> CaptureServer::Start(const String& application, const std::filesystem::path& root, UInt32 pid)
{
	Close();
	try
	{
		if (!pid || application.empty())
		{
			throw std::runtime_error("Capture endpoint requires PID and application name");
		}
		auto candidate = std::make_unique<Impl>();
		candidate->storage.Open(root, true);
		candidate->identity.pid = pid;
		candidate->identity.sessionId = NewCaptureId();
		candidate->application = application;
		candidate->owner = std::this_thread::get_id();
		Done(candidate->storage.files.CreateDirectory(candidate->storage.Path("instances")));
		candidate->lease = Take(candidate->storage.files.AcquireExclusiveLease(
		    candidate->storage.Path("instances/" + ToUrsaString(std::to_string(pid)) + ".lock")));
		Done(
		    candidate->storage.files.CreateDirectory(candidate->storage.Path(Session(candidate->identity) + "/inbox")));
		candidate->Register();
		impl = std::move(candidate);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}

Result<void> CaptureServer::SetReady()
{
	try
	{
		if (!impl || impl->owner != std::this_thread::get_id())
		{
			throw std::runtime_error("Capture endpoint requires its window thread");
		}
		impl->ready = true;
		impl->registrationPending = true;
		impl->Register();
		impl->registrationPending = false;
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}

Result<void> CaptureServer::Poll()
{
	if (!impl)
	{
		return {};
	}
	try
	{
		if (impl->owner != std::this_thread::get_id())
		{
			throw std::runtime_error("Capture endpoint requires its window thread");
		}
		const auto now = std::chrono::steady_clock::now();
		if (now < impl->nextPoll)
		{
			return {};
		}
		impl->nextPoll = now + std::chrono::milliseconds(50);
		if (impl->registrationPending)
		{
			try
			{
				impl->Register();
				impl->registrationPending = false;
			}
			catch (const std::exception&)
			{
				return {};
			}
		}
		if (!impl->Current())
		{
			throw std::runtime_error("Capture instance registration changed");
		}
		Pending();
		std::error_code error;
		std::filesystem::directory_iterator entry(
		    impl->storage.root / PathFromUrsaString(Session(impl->identity)) / "inbox", error);
		for (UInt32 count = 0; !error && entry != std::filesystem::directory_iterator{} && count < 32;
		     ++count, entry.increment(error))
		{
			const auto filename = entry->path().filename();
			const auto id = PathToUrsaString(filename.stem());
			if (filename.extension() != ".json" || !ValidId(id))
			{
				continue;
			}
			auto request = impl->identity;
			request.requestId = id;
			request.output = impl->storage.root / "invalid.png";
			request.deadlineUtc = CaptureUtcMilliseconds() + 5000;
			const auto source = MessagePath(request, "inbox");
			try
			{
				const auto parsed = ParseCaptureRequest(impl->storage.Read(source));
				if (!parsed)
				{
					impl->Reply(request,
					            parsed.error.find("UnsupportedProtocol") != String::npos ? "UnsupportedProtocol"
					                                                                     : "InvalidRequest",
					            parsed.error);
				}
				else if (parsed.value.pid != request.pid || parsed.value.sessionId != request.sessionId ||
				         parsed.value.requestId != id)
				{
					impl->Reply(request, "InvalidRequest", "Capture request identity mismatch");
				}
				else if (impl->Expired(parsed.value))
				{
					impl->Reply(parsed.value, "Timeout", "Capture request expired");
				}
				else if (!impl->ready)
				{
					impl->Reply(parsed.value, "NotReady", "Window is not ready for capture");
				}
				else if (impl->pending)
				{
					impl->Reply(parsed.value, "Busy", "Another capture is pending");
				}
				else
				{
					impl->pending = parsed.value;
				}
			}
			catch (const std::exception& failure)
			{
				impl->Reply(request, "InvalidRequest", ToUrsaString(failure.what()));
			}
			impl->storage.Remove(source);
		}
		if (error)
		{
			throw std::runtime_error(error.message());
		}
		if (now >= impl->nextCleanup)
		{
			impl->Cleanup();
			impl->nextCleanup = now + std::chrono::seconds(5);
		}
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}

const CaptureRequest* CaptureServer::Pending()
{
	if (!impl || !impl->pending)
	{
		return nullptr;
	}
	try
	{
		if (impl->Expired(*impl->pending))
		{
			impl->Reply(*impl->pending, "Timeout", "Capture request expired or was cancelled");
			impl->storage.Remove(MessagePath(*impl->pending, "cancel"));
			impl->pending.reset();
		}
	}
	catch (...)
	{
		impl->pending.reset();
	}
	return impl->pending ? &*impl->pending : nullptr;
}

Result<void> CaptureServer::Complete(const String& status, const String& message, UInt32 width, UInt32 height,
                                     UInt64 frameSerial, Int64 capturedAtUtc)
{
	if (!Pending())
	{
		return {};
	}
	auto request = *impl->pending;
	impl->pending.reset();
	try
	{
		CaptureResult result;
		result.request = request;
		result.status = status;
		result.message = message;
		result.width = width;
		result.height = height;
		result.frameSerial = frameSerial;
		result.capturedAtUtc = capturedAtUtc;
		impl->storage.Write(MessagePath(request, "replies"), SerializeCaptureResult(result));
		impl->storage.Remove(MessagePath(request, "cancel"));
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}

void CaptureServer::Close()
{
	if (!impl)
	{
		return;
	}
	try
	{
		Complete("TargetClosed", "Window capture endpoint closed");
		if (impl->Current())
		{
			impl->storage.Remove(Registry(impl->identity.pid));
		}
		// Fixed work, with no recursive enumeration or deletion of unread results.
		for (const char* folder : {"inbox", "replies", "cancel"})
		{
			impl->storage.files.RemoveDirectory(impl->storage.Path(Session(impl->identity) + "/" + folder));
		}
		impl->storage.files.RemoveDirectory(impl->storage.Path(Session(impl->identity)));
	}
	catch (...)
	{
	}
	impl.reset();
}

String CaptureServer::SessionId() const
{
	return impl ? impl->identity.sessionId : String{};
}
} // namespace Ursa::Diagnostics
