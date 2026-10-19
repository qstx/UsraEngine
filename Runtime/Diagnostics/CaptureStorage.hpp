#pragma once

#include <Diagnostics/CaptureChannel.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Ursa::Diagnostics::Detail
{
template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

inline void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

inline bool ValidId(const String& id)
{
	return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == String::npos;
}

inline String Registry(UInt32 pid)
{
	return "instances/" + ToUrsaString(std::to_string(pid)) + ".json";
}

inline String Session(const CaptureRequest& request)
{
	return "sessions/" + ToUrsaString(std::to_string(request.pid)) + "/" + request.sessionId;
}

inline String MessagePath(const CaptureRequest& request, const char* folder)
{
	return Session(request) + "/" + folder + "/" + request.requestId + ".json";
}

struct CaptureStorage
{
	VirtualFileSystem files;
	std::filesystem::path root;

	void Open(const std::filesystem::path& selectedRoot, bool create)
	{
		root = selectedRoot.empty() ? Take(ResolveUserDataDirectory("UrsaCapture")) : selectedRoot;
		if (!root.is_absolute())
		{
			throw std::runtime_error("Capture control root must be absolute");
		}
		Done(files.MountProvider("control", Take(DirectoryFileProvider::Open(root, create)), MountAccess::ReadWrite));
	}

	VirtualPath Path(const String& relative) const
	{
		return Take(VirtualPath::Parse("control:" + relative));
	}

	bool Exists(const String& relative) const
	{
		return Take(files.Exists(Path(relative)));
	}

	String Read(const String& relative) const
	{
		const auto path = Path(relative);
		if (Take(files.Size(path)) > 64 * 1024)
		{
			throw std::runtime_error("Capture message exceeds 64 KiB");
		}
		const auto bytes = Take(files.ReadPrefix(path, 64 * 1024 + 1));
		if (bytes.size() > 64 * 1024)
		{
			throw std::runtime_error("Capture message exceeds 64 KiB");
		}
		return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	void Write(const String& relative, const String& text)
	{
		Done(files.WriteAtomic(Path(relative), Vector<UInt8>(text.begin(), text.end())));
	}

	void Remove(const String& relative)
	{
		files.Remove(Path(relative));
	}
};

inline nlohmann::json Json(const String& text)
{
	return nlohmann::json::parse(ToStdString(text));
}

inline String Text(const nlohmann::json& json)
{
	return ToUrsaString(json.dump());
}

inline UInt32 ReadPid(const nlohmann::json& json)
{
	const auto& value = json.at("Pid");
	if (!value.is_number_integer() || value < 1 || value > std::numeric_limits<UInt32>::max())
	{
		throw std::runtime_error("Invalid capture PID");
	}
	return value.get<UInt32>();
}

inline void CheckVersion(const nlohmann::json& json)
{
	if (!json.contains("Version") || !json["Version"].is_number_integer() || json["Version"] != 1)
	{
		throw std::runtime_error("UnsupportedProtocol: expected capture protocol version 1");
	}
}

inline String RegistrationSession(const nlohmann::json& json, UInt32 pid)
{
	CheckVersion(json);
	const auto session = ToUrsaString(json.at("SessionId").get<std::string>());
	if (ReadPid(json) != pid || !ValidId(session))
	{
		throw std::runtime_error("Invalid capture registration");
	}
	return session;
}
} // namespace Ursa::Diagnostics::Detail
