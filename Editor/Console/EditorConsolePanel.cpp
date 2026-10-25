#include <Ui/EditorIcons.hpp>
#include <Console/EditorConsolePanel.hpp>

#include <Core/Containers.hpp>
#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <ctime>
#include <mutex>

namespace Ursa::Editor
{
struct EditorConsolePanel::State
{
	std::mutex mutex;
	Deque<LogRecord> records;
};

namespace
{
constexpr Size maximumLogRecords = 10000;

bool VisibleLevel(LogLevel level, bool trace, bool debug, bool info, bool warnings, bool errors)
{
	switch (level)
	{
		case LogLevel::Trace:
			return trace;
		case LogLevel::Debug:
			return debug;
		case LogLevel::Info:
			return info;
		case LogLevel::Warn:
			return warnings;
		case LogLevel::Error:
		case LogLevel::Critical:
			return errors;
		case LogLevel::Off:
			return false;
	}
	return false;
}

const char* LevelLabel(LogLevel level)
{
	switch (level)
	{
		case LogLevel::Trace:
			return "Trace";
		case LogLevel::Debug:
			return "Debug";
		case LogLevel::Info:
			return "Info";
		case LogLevel::Warn:
			return "Warning";
		case LogLevel::Error:
			return "Error";
		case LogLevel::Critical:
			return "Critical";
		case LogLevel::Off:
			return "Off";
	}
	return "Unknown";
}

ImVec4 LevelColor(LogLevel level)
{
	switch (level)
	{
		case LogLevel::Trace:
			return {0.65f, 0.65f, 0.65f, 1.0f};
		case LogLevel::Debug:
			return {0.55f, 0.75f, 0.95f, 1.0f};
		case LogLevel::Info:
			return {0.85f, 0.85f, 0.85f, 1.0f};
		case LogLevel::Warn:
			return {1.0f, 0.75f, 0.25f, 1.0f};
		case LogLevel::Error:
		case LogLevel::Critical:
			return {1.0f, 0.35f, 0.35f, 1.0f};
		case LogLevel::Off:
			return {0.5f, 0.5f, 0.5f, 1.0f};
	}
	return {1.0f, 1.0f, 1.0f, 1.0f};
}

String FormatTimestamp(const LogRecord& record)
{
	const std::time_t time = std::chrono::system_clock::to_time_t(record.timestamp);
	std::tm localTime{};
#ifdef _WIN32
	localtime_s(&localTime, &time);
#else
	localtime_r(&time, &localTime);
#endif
	char buffer[32]{};
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
	return buffer;
}

} // namespace

EditorConsolePanel::EditorConsolePanel() : state(std::make_shared<State>())
{
	std::weak_ptr<State> weakState = state;
	listenerId = Log::Subscribe(
	    [weakState](const LogRecord& record)
	    {
		    if (auto sharedState = weakState.lock())
		    {
			    std::lock_guard<std::mutex> lock(sharedState->mutex);
			    sharedState->records.push_back(record);
			    if (sharedState->records.size() > maximumLogRecords)
			    {
				    sharedState->records.pop_front();
			    }
		    }
	    });
}

EditorConsolePanel::~EditorConsolePanel()
{
	Log::Unsubscribe(listenerId);
}

Vector<LogRecord> EditorConsolePanel::Records() const
{
	std::lock_guard<std::mutex> lock(state->mutex);
	return Vector<LogRecord>(state->records.begin(), state->records.end());
}

void EditorConsolePanel::Clear()
{
	std::lock_guard<std::mutex> lock(state->mutex);
	state->records.clear();
}

Array<Size, 5> EditorConsolePanel::Counts(const Vector<LogRecord>& records)
{
	Array<Size, 5> counts{};
	for (const auto& record : records)
	{
		switch (record.level)
		{
			case LogLevel::Trace:
				++counts[0];
				break;
			case LogLevel::Debug:
				++counts[1];
				break;
			case LogLevel::Info:
				++counts[2];
				break;
			case LogLevel::Warn:
				++counts[3];
				break;
			case LogLevel::Error:
			case LogLevel::Critical:
				++counts[4];
				break;
			case LogLevel::Off:
				break;
		}
	}
	return counts;
}

void EditorConsolePanel::Draw(bool& open, String& diagnostic, const ContextHandler& contextHandler,
                              ContextActionProbe* contextProbe)
{
	if (!open)
	{
		return;
	}
	if (!ImGui::Begin("Console", &open))
	{
		ImGui::End();
		return;
	}
	if (ImGui::BeginPopupContextWindow("ConsolePanelActions", ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Close Panel"))
		{
			open = false;
		}
		ImGui::EndPopup();
	}
	if (Icons::Button("Clear", Icons::Icon::Delete, "Clear console"))
	{
		Clear();
	}
	const auto snapshot = Records();
	const auto counts = Counts(snapshot);
	const char* labels[]{"Trace", "Debug", "Info", "Warning", "Error"};
	const Icons::Icon icons[]{Icons::Icon::Trace, Icons::Icon::Debug, Icons::Icon::Info, Icons::Icon::Warning,
	                          Icons::Icon::Error};
	bool* filters[]{&showTrace, &showDebug, &showInfo, &showWarnings, &showErrors};
	for (Size level = 0; level < 5; ++level)
	{
		const String label = String(labels[level]) + " " + ToUrsaString(std::to_string(counts[level]));
		Icons::SameLine(ImGui::CalcTextSize(label.c_str()).x + 48.f);
		if (Icons::Button(labels[level], icons[level], label.c_str(), *filters[level], true))
		{
			*filters[level] = !*filters[level];
		}
	}
	if (ImGui::BeginTable("ConsoleEntries", 4,
	                      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable |
	                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 142.0f);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
		ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 110.0f);
		ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();
		for (Size index = 0; index < snapshot.size(); ++index)
		{
			const LogRecord& record = snapshot[index];
			if (!VisibleLevel(record.level, showTrace, showDebug, showInfo, showWarnings, showErrors))
			{
				continue;
			}
			ImGui::TableNextRow();
			ImGui::PushID(static_cast<int>(index));
			ImGui::TableSetColumnIndex(0);
			const String timestamp = FormatTimestamp(record);
			ImGui::TextUnformatted(timestamp.c_str());
			ImGui::TableSetColumnIndex(1);
			const ImVec4 color = LevelColor(record.level);
			const auto icon = record.level == LogLevel::Trace   ? Icons::Icon::Trace
			                  : record.level == LogLevel::Debug ? Icons::Icon::Debug
			                  : record.level == LogLevel::Info  ? Icons::Icon::Info
			                  : record.level == LogLevel::Warn  ? Icons::Icon::Warning
			                                                    : Icons::Icon::Error;
			Icons::Inline(icon);
			ImGui::TextColored(color, "%s", LevelLabel(record.level));
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(record.category.c_str());
			ImGui::TableSetColumnIndex(3);
			ImGui::TextWrapped("%s", record.message.c_str());
			if (contextProbe && record.message == contextProbe->message)
			{
				const auto minimum = ImGui::GetItemRectMin();
				const auto maximum = ImGui::GetItemRectMax();
				contextProbe->messageCenter = {(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f};
				contextProbe->messageHovered = ImGui::IsItemHovered();
			}
			if (ImGui::BeginPopupContextItem("ConsoleEntryActions"))
			{
				const bool copy = ImGui::MenuItem("Copy Message");
				if (contextProbe && record.message == contextProbe->message)
				{
					contextProbe->copyItem = ImGui::GetItemID();
					const auto minimum = ImGui::GetItemRectMin();
					const auto maximum = ImGui::GetItemRectMax();
					contextProbe->copyCenter = {(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f};
					contextProbe->copyHovered = ImGui::IsItemHovered();
				}
				if (copy)
				{
					ImGui::SetClipboardText(record.message.c_str());
				}
				const bool canLocate =
				    contextHandler && (!record.context.resourcePath.empty() || !record.context.objectId.empty());
				const bool locate = canLocate && ImGui::MenuItem("Locate");
				if (contextProbe && record.message == contextProbe->message && canLocate)
				{
					contextProbe->locateItem = ImGui::GetItemID();
					const auto minimum = ImGui::GetItemRectMin();
					const auto maximum = ImGui::GetItemRectMax();
					contextProbe->locateCenter = {(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f};
					contextProbe->locateHovered = ImGui::IsItemHovered();
				}
				if (locate)
				{
					auto located = contextHandler(record);
					diagnostic = located ? String{} : located.error;
				}
				ImGui::EndPopup();
			}
			if (contextHandler && (!record.context.resourcePath.empty() || !record.context.objectId.empty()))
			{
				ImGui::SameLine();
				if (Icons::Button("Locate", Icons::Icon::Locate, "Locate log context"))
				{
					auto located = contextHandler(record);
					diagnostic = located ? String{} : located.error;
				}
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::End();
}
} // namespace Ursa::Editor
