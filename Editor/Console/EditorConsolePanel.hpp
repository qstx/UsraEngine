#pragma once

#include <Core/Log.hpp>
#include <functional>
#include <memory>

namespace Ursa::Editor
{
class EditorConsolePanel final
{
public:
	using ContextHandler = std::function<Result<void>(const LogRecord&)>;

	EditorConsolePanel();

	~EditorConsolePanel();

	EditorConsolePanel(const EditorConsolePanel&) = delete;
	EditorConsolePanel& operator=(const EditorConsolePanel&) = delete;

	Vector<LogRecord> Records() const;

	void Clear();

	static Array<Size, 5> Counts(const Vector<LogRecord>& records);

	void Draw(bool& open, String& diagnostic, const ContextHandler& contextHandler = {});

private:
	struct State;
	std::shared_ptr<State> state;
	Ursa::UInt64 listenerId = 0;
	bool showTrace = false;
	bool showDebug = true;
	bool showInfo = true;
	bool showWarnings = true;
	bool showErrors = true;
};
} // namespace Ursa::Editor
