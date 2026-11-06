#pragma once

#include <Gui/GuiSession.hpp>
#include <chrono>

namespace Ursa::Gui
{
// Raw events remain ordered and independent of Window's physical polling state.
// Editor hosts may feed only events accepted by the Game viewport gate.
class InputQueue final
{
public:
	~InputQueue();
	Result<void> Attach(std::shared_ptr<Window> window);
	void Detach();
	void Push(const WindowEvent& event);
	void Push(const WindowEvent& event, Point position);
	Vector<Event> Drain();
	void Cancel();
	bool TakeFocusLost();
	void SynchronizeModifiers(const Window& source);

private:
	std::shared_ptr<Window> window;
	UInt64 subscription = 0;
	Vector<Event> events;
	Array<bool, static_cast<Size>(Key::Digit9) + 1> keys{};
	Array<bool, 4> buttons{};
	Point pointer;
	bool focusLost = false;
	std::chrono::steady_clock::time_point lastClickTime{};
	Point lastClickPosition;
	Int32 lastClickButton = -1;
	Int32 clickCount = 0;
};
} // namespace Ursa::Gui
