#include <Application/EditorApplication.hpp>
#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/SceneDraft.hpp>
#include <Core/Profiler.hpp>
#include <Core/StringInterop.hpp>
#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

class ProfilingEditor final : public EditorApplication
{
public:
	explicit ProfilingEditor(bool capture) : waitForCapture(capture)
	{
	}

	bool Verified() const
	{
		return runs == 3 && restored && framesWhileMinimized == 0;
	}

protected:
	Result<void> OnStart() override
	{
		URSA_PROFILE_APP("UrsaProfilerEditorWindowTests");
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
		return EditorApplication::OnStart();
	}

	Result<void> OnGui() override
	{
		++guiFrames;
		const auto now = std::chrono::steady_clock::now();
		if (now > deadline)
		{
			return Result<void>::Failure("Profiling Editor test timed out at phase " +
			                             ToUrsaString(std::to_string(phase)) + ": " + ScriptStatus() + " / " +
			                             AssetStatus());
		}
		if (phase == 2 && now >= pauseAt)
		{
			ActivateToolbar("Pause");
			phase = 3;
			resumeAt = now + std::chrono::milliseconds(300);
			URSA_PROFILE_OPERATION("ProfilerTest.Editor", "", "pause");
		}
		else if (phase == 3 && now >= resumeAt)
		{
			ActivateToolbar("Resume");
			phase = 4;
			stopAt = now + std::chrono::milliseconds(300);
			URSA_PROFILE_OPERATION("ProfilerTest.Editor", "", "resume");
		}
		auto drawn = EditorApplication::OnGui();
		if (!drawn || ScriptWorkPending() || AssetWorkPending())
		{
			return drawn;
		}
		if (waitForCapture && !Profiling::IsConnected())
		{
			return {};
		}
		if (phase == 0)
		{
			if (!Documents() || !Documents()->Current())
			{
				return Result<void>::Failure("Profiling test did not open the Snake scene");
			}
			if (!importsChecked)
			{
				auto checked = CheckImportResults();
				if (!checked)
				{
					return checked;
				}
				importsChecked = true;
				dirtyBeforePlay = Documents()->Current()->Dirty();
			}
			RequestPlayToggle();
			phase = 1;
		}
		else if (phase == 1)
		{
			if (RunningScene())
			{
				pauseAt = now + std::chrono::milliseconds(300);
				phase = 2;
				if (runs == 2 && !minimized)
				{
					timerWindow =
					    glfwGetWin32Window(static_cast<GLFWwindow*>(Ursa::Detail::WindowAccess::Native(*window)));
					restoreOwner = this;
					timer = SetTimer(timerWindow, 0, 300, RestoreAfterMinimize);
					if (!timer)
					{
						return Result<void>::Failure("Cannot schedule profiling window restore");
					}
					minimized = true;
					minimizedAtFrame = guiFrames;
					URSA_PROFILE_OPERATION("ProfilerTest.Editor", "", "minimize");
					window->Minimize();
				}
			}
			else
			{
				RequestPlayToggle();
			}
		}
		else if (phase == 4 && now >= stopAt)
		{
			RequestPlayToggle();
			phase = 5;
		}
		else if (phase == 5 && !RunningScene())
		{
			if (Documents()->Current()->Dirty() != dirtyBeforePlay)
			{
				return Result<void>::Failure("Profiling Run/Stop changed the edit document Dirty state");
			}
			if (++runs == 3)
			{
				window->RequestClose();
			}
			else
			{
				phase = 0;
			}
		}
		return {};
	}

	void OnStop() override
	{
		if (timer)
		{
			KillTimer(timerWindow, timer);
			timer = 0;
		}
		restoreOwner = nullptr;
		std::cout << "frames-while-minimized=" << framesWhileMinimized << '\n';
	}

private:
	static void CALLBACK RestoreAfterMinimize(HWND target, UINT, UINT_PTR timerId, DWORD)
	{
		KillTimer(target, timerId);
		if (restoreOwner)
		{
			auto& owner = *restoreOwner;
			owner.timer = 0;
			owner.framesWhileMinimized = owner.guiFrames - owner.minimizedAtFrame;
			owner.restored = true;
			owner.window->Restore();
			glfwHideWindow(static_cast<GLFWwindow*>(Ursa::Detail::WindowAccess::Native(*owner.window)));
			URSA_PROFILE_OPERATION("ProfilerTest.Editor", "", "restore");
		}
	}

	void ActivateToolbar(const char* label)
	{
		// Exercise the real toolbar command without changing global mouse input.
		auto* toolbar = ImGui::FindWindowByName("##EditorToolbar");
		if (toolbar)
		{
			const auto id = toolbar->GetID(label);
			GImGui->NavActivateId = id;
			GImGui->NavActivateDownId = id;
		}
	}

	Result<void> CheckImportResults()
	{
		auto source = VirtualPath::Parse("prj:Snake/Snake.ursascene");
		if (!source || !Workspace()->Database())
		{
			return Result<void>::Failure("Missing profiling import source");
		}
		ProjectAssetImporter importer;
		auto first = importer.Import(*Workspace()->Database(), source.value);
		auto cached = first ? importer.Import(*Workspace()->Database(), source.value) : first;
		if (!cached || !cached.value.cacheHit)
		{
			return Result<void>::Failure("Profiling changed import cache results");
		}
		auto registered =
		    importer.Register(".ursascene", [](AssetDatabase&, const VirtualPath&, bool)
		                      { return Result<AssetImportResult>::Failure("cancelled from profiling test"); });
		if (!registered)
		{
			return registered;
		}
		auto cancelled = importer.Import(*Workspace()->Database(), source.value);
		if (cancelled || cancelled.error != "cancelled from profiling test")
		{
			return Result<void>::Failure("Profiling changed cancellation diagnostics");
		}
		return {};
	}

	bool waitForCapture = false;
	bool importsChecked = false;
	bool dirtyBeforePlay = false;
	bool minimized = false;
	bool restored = false;
	UInt32 guiFrames = 0;
	UInt32 minimizedAtFrame = 0;
	UInt32 framesWhileMinimized = 0;
	HWND timerWindow = nullptr;
	UINT_PTR timer = 0;
	inline static thread_local ProfilingEditor* restoreOwner = nullptr;
	UInt32 phase = 0;
	UInt32 runs = 0;
	std::chrono::steady_clock::time_point deadline;
	std::chrono::steady_clock::time_point pauseAt;
	std::chrono::steady_clock::time_point resumeAt;
	std::chrono::steady_clock::time_point stopAt;
};
} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		return 2;
	}
	try
	{
		ProfilingEditor editor(argc > 2);
		auto opened = editor.OpenProject(std::filesystem::u8path(argv[1]));
		if (!opened)
		{
			throw std::runtime_error(ToStdString(opened.error));
		}
		editor.SetStartupAsset(VirtualPath::Parse("prj:Snake/Snake.ursascene").value);
		auto ran = editor.Run(1280, 720, "Ursa Profiling Editor", false);
		if (!ran || !editor.Verified())
		{
			throw std::runtime_error(ran ? "Profiling Run/Stop verification failed" : ToStdString(ran.error));
		}
		std::cout << "Profiling Editor: imports/cache/cancellation and three Run/Pause/Resume/Stop cycles passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
