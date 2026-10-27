#pragma once

#include <Core/Log.hpp>
#include <Scripting/ScriptDiagnostic.hpp>
#include <functional>
#include <imgui.h>
#include <memory>
#include <optional>

namespace Ursa::Editor
{
class EditorConsolePanel final
{
public:
	using ContextHandler = std::function<Result<void>(const LogRecord&)>;

	struct ContextActionProbe
	{
		String message;
		ImVec2 messageCenter{};
		ImGuiID messageItem = 0;
		bool messageHovered = false;
		bool messageSelected = false;
		ImVec2 copyCenter{};
		bool copyHovered = false;
		ImVec2 locateCenter{};
		bool locateHovered = false;
		ImGuiID copyItem = 0;
		ImGuiID locateItem = 0;
	};

	EditorConsolePanel();

	~EditorConsolePanel();

	EditorConsolePanel(const EditorConsolePanel&) = delete;
	EditorConsolePanel& operator=(const EditorConsolePanel&) = delete;

	Vector<LogRecord> Records() const;

	void Clear();
	void AddCompilerDiagnostics(const Vector<ScriptDiagnostic>& diagnostics);
	void NotifyBuildStarted();
	void NotifyRecompileStarted();

	static Array<Size, 3> Counts(const Vector<LogRecord>& records);

	void Draw(bool& open, String& diagnostic, const ContextHandler& contextHandler = {},
	          ContextActionProbe* contextProbe = nullptr, const std::function<void()>& pauseOnError = {},
	          bool isPlaying = false, VirtualFileSystem* userFiles = nullptr);

private:
	struct State;
	void ClearAll();
	std::shared_ptr<State> state;
	Ursa::UInt64 listenerId = 0;
	bool showMessages = true;
	bool showWarnings = true;
	bool showErrors = true;
	bool collapse = false;
	bool errorPause = false;
	bool clearOnPlay = true;
	bool clearOnBuild = false;
	bool clearOnRecompile = false;
	Int32 messageLineCount = 1;
	bool wasPlaying = false;
	std::optional<LogRecord> selectedRecord;
	Vector<LogRecord> selectedRecords;
	std::optional<LogRecord> selectionAnchor;
	std::optional<LogRecord> searchRestoreRecord;
	String previousSearch;
	std::chrono::system_clock::time_point observedLatestError{};
	Array<char, 256> searchText{};
	VirtualFileSystem* settingsFiles = nullptr;
	bool settingsLoaded = false;
};
} // namespace Ursa::Editor
