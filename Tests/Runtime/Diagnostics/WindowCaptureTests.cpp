#include <Diagnostics/CaptureChannel.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace Ursa;
using namespace Ursa::Diagnostics;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

template <class F> void Until(F predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!predicate())
	{
		Expect(std::chrono::steady_clock::now() < deadline, "Capture condition did not complete");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void Ready(CaptureServer& server, const std::filesystem::path& root, UInt32 pid = 12345)
{
	Done(server.Start("ProtocolTest", root, pid));
	Done(server.SetReady());
}

std::optional<CaptureResult> Pump(CaptureServer& server, CaptureClient& client)
{
	Done(server.Poll());
	return client.Poll();
}

void TestProtocol(const std::filesystem::path& root)
{
	CaptureServer server;
	Done(server.Start("ProtocolTest", root, 12345));
	CaptureClient early(12345, root / "early.png", 1000, root);
	Expect(early.Wait().status == "NotReady", "Unready window was accepted");
#ifdef _WIN32
	// Readers without delete sharing must not permanently prevent ready publication.
	std::ifstream registrationReader(root / "instances/12345.json");
	Expect(!server.SetReady(), "Ready replacement did not encounter the held registry reader");
	registrationReader.close();
	Done(server.Poll());
#else
	Done(server.SetReady());
#endif
	CaptureClient first(12345, root / std::filesystem::u8path(u8"中文 截图.png"), 1000, root);
	const auto encoded = SerializeCaptureRequest(first.Request());
	const auto decoded = ParseCaptureRequest(encoded);
	Expect(decoded && decoded.value.output == first.Request().output &&
	           decoded.value.requestId == first.Request().requestId,
	       "Request UTF-8 or identity did not round trip");
	auto json = nlohmann::json::parse(ToStdString(encoded));
	json["Version"] = 2;
	Expect(!ParseCaptureRequest(ToUrsaString(json.dump())), "Unknown protocol version accepted");
	json["Version"] = 1;
	json["Pid"] = -1;
	Expect(!ParseCaptureRequest(ToUrsaString(json.dump())) && !ParseCaptureRequest("{"), "Invalid request accepted");
	Until(
	    [&]
	    {
		    Done(server.Poll());
		    return server.Pending() != nullptr;
	    });
	std::ofstream(first.Request().output) << "protocol fixture";
	Done(server.Complete("Success", {}, 17, 19, 4, CaptureUtcMilliseconds()));
	const auto result = first.Wait();
	auto nativeError = result;
	nativeError.status = "CaptureFailed";
	nativeError.message = String(1, static_cast<char>(0xda));
	Expect(bool(ParseCaptureResult(SerializeCaptureResult(nativeError))),
	       "Native error bytes broke the UTF-8 JSON result");
	Expect(result.Succeeded() && result.width == 17 && result.height == 19 && result.frameSerial == 4,
	       "Successful result did not match its request");
	CaptureClient second(12345, root / "second.png", 1000, root);
	Until(
	    [&]
	    {
		    Done(server.Poll());
		    return server.Pending() != nullptr;
	    });
	Expect(second.Request().requestId != first.Request().requestId, "Repeated request reused an ID");
	Done(server.Complete("WriteFailed", "Deliberate write failure"));
	Expect(second.Wait().status == "WriteFailed", "Failure was not reported independently");
	CaptureClient recovery(12345, root / "recovery.png", 1000, root);
	Until(
	    [&]
	    {
		    Done(server.Poll());
		    return server.Pending() != nullptr;
	    });
	Done(server.Complete("CaptureFailed", "Still alive"));
	Expect(recovery.Wait().status == "CaptureFailed", "Endpoint could not accept a request after failure");
}

void TestBusyAndExpiry(const std::filesystem::path& root)
{
	CaptureServer server;
	Ready(server, root);
	CaptureClient first(12345, root / "first.png", 1000, root);
	CaptureClient second(12345, root / "second.png", 1000, root);
	Until(
	    [&]
	    {
		    Done(server.Poll());
		    return first.Poll().has_value() || second.Poll().has_value();
	    });
	Expect(server.Pending() != nullptr, "Concurrent requests lost the accepted request");
	auto& accepted = server.Pending()->requestId == first.Request().requestId ? first : second;
	auto& rejected = &accepted == &first ? second : first;
	Expect(rejected.Wait().status == "Busy", "Overflowing request did not receive Busy");
	Done(server.Complete("CaptureFailed", "Accepted request retained"));
	Expect(accepted.Wait().status == "CaptureFailed", "Busy request overwrote accepted state");
	CaptureClient timeout(12345, root / "timeout.png", 80, root);
	const auto start = std::chrono::steady_clock::now();
	Expect(timeout.Wait().status == "Timeout", "Idle target did not time out");
	Expect(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500),
	       "Deadline was extended during waiting");
	Done(server.Poll());
	Expect(!server.Pending(), "Expired request was accepted for capture");
	CaptureClient pendingTimeout(12345, root / "pending-timeout.png", 120, root);
	Until(
	    [&]
	    {
		    Done(server.Poll());
		    return server.Pending() != nullptr;
	    });
	Expect(pendingTimeout.Wait().status == "Timeout" && !server.Pending(), "Accepted request did not expire safely");
}

void TestLifecycleAndMessages(const std::filesystem::path& root)
{
	CaptureServer server;
	CaptureServer other;
	Ready(server, root);
	Ready(other, root, 12346);
	CaptureClient client(12345, root / "closed.png", 1000, root);
	Until(
	    [&]
	    {
		    Done(server.Poll());
		    return server.Pending() != nullptr;
	    });
	Expect(!other.Pending(), "Request was sent to another PID");
	const auto previousSession = server.SessionId();
	CaptureServer duplicate;
	Expect(!duplicate.Start("Duplicate", root, 12345), "Two endpoints acquired the same PID lease");
	server.Close();
	Expect(client.Wait().status == "TargetClosed", "Closing target did not complete its pending request");
	Ready(server, root);
	duplicate.Close();
	Expect(server.SessionId() != previousSession, "PID reuse kept the previous instance identity");
	CaptureClient invalid(12345, root / "invalid.png", 1000, root);
	VirtualFileSystem files;
	Done(files.Mount("control", root, MountAccess::ReadWrite));
	const String base = "control:sessions/12345/" + server.SessionId();
	const auto inbox = VirtualPath::Parse(base + "/inbox/" + invalid.Request().requestId + ".json").value;
	Done(files.WriteAtomic(inbox, {'{'}));
	std::optional<CaptureResult> bad;
	Until(
	    [&]
	    {
		    bad = Pump(server, invalid);
		    return bad.has_value();
	    });
	Expect(bad->status == "InvalidRequest", "Malformed message was accepted or killed the endpoint");
	CaptureClient stale(12345, root / "stale.png", 1000, root);
	CaptureResult old;
	old.request = stale.Request();
	old.request.sessionId = previousSession;
	old.status = "Busy";
	const auto reply = VirtualPath::Parse(base + "/replies/" + stale.Request().requestId + ".json").value;
	const auto text = SerializeCaptureResult(old);
	Done(files.WriteAtomic(reply, Vector<UInt8>(text.begin(), text.end())));
	Expect(!stale.Poll(), "Stale instance response was treated as current");
	Until(
	    [&]
	    {
		    Done(server.Poll());
		    return server.Pending() != nullptr;
	    });
	Done(server.Complete("CaptureFailed", "Fresh response"));
	Expect(stale.Wait().request.sessionId == server.SessionId(), "Fresh request used the old instance");
	CaptureClient independent(12346, root / "independent.png", 1000, root);
	Until(
	    [&]
	    {
		    Done(other.Poll());
		    return other.Pending() != nullptr;
	    });
	server.Close();
	Done(other.Complete("CaptureFailed", "Independent instance remains alive"));
	Expect(independent.Wait().status == "CaptureFailed", "Closing one endpoint removed another's registration");
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::current_path() / "Temp" / ("CaptureProtocol-" + ToStdString(NewCaptureId()));
		TestProtocol(root / "protocol");
		TestBusyAndExpiry(root / "busy");
		TestLifecycleAndMessages(root / "lifecycle");
		std::cout << "Capture protocol and lifecycle tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
