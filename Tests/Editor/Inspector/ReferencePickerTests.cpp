#include <Inspector/ReferencePicker.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <imgui_internal.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

Vector<ReferencePickerOption> Options(bool reverse = false)
{
	Vector<ReferencePickerOption> options{{"none", "None", "None", {}, true},
	                                      {"asset:a", "Alpha", "Texture", "Assets/Alpha.png", true},
	                                      {"asset:b", "Beta", "Texture", "Assets/Beta.png", true}};
	if (reverse)
	{
		std::swap(options[1], options[2]);
	}
	return options;
}

ImGuiID PickerButtonId(const String& controlId)
{
	return ImHashStr("...", 0, ImHashStr(controlId.c_str(), 0, ImHashStr("Reference Picker Test")));
}

void ClickSearchInput(ImGuiWindow* popup)
{
	const auto& style = ImGui::GetStyle();
	const ImVec2 position(popup->Pos.x + style.WindowPadding.x + 120.0f,
	                      popup->Pos.y + style.WindowPadding.y + ImGui::CalcTextSize("Select Texture").y +
	                          style.ItemSpacing.y + ImGui::GetFrameHeight() * 0.5f);
	ImGui::GetIO().AddMousePosEvent(position.x, position.y);
	ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
}

std::optional<Size> DrawFrame(const String& controlId, const Vector<ReferencePickerOption>& options,
                              const String& currentKey = {}, const String& sessionOwner = "test-owner",
                              const ReferencePickerTransaction* transaction = nullptr,
                              nlohmann::json* acceptedCheckpoint = nullptr, bool handleExternalClick = false,
                              bool* externalClickHandled = nullptr, const ImVec2* simulatedExternalClick = nullptr,
                              ReferencePickerContextActionIds* contextActionIds = nullptr,
                              std::function<Result<void>()> locate = {})
{
	ImGui::NewFrame();
	if (handleExternalClick)
	{
		if (simulatedExternalClick)
		{
			ImGui::GetIO().MousePos = *simulatedExternalClick;
			ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left] = true;
		}
		const bool handled = RequestCancelReferencePickerForExternalClick(ImGui::GetCurrentContext());
		if (externalClickHandled)
		{
			*externalClickHandled = handled;
		}
	}
	ImGui::SetNextWindowSize({600, 400}, ImGuiCond_Always);
	ImGui::Begin("Reference Picker Test");
	auto result = DrawReferencePickerButton(controlId, "Alpha", "Texture", options, currentKey, sessionOwner,
	                                        transaction, acceptedCheckpoint, std::move(locate), contextActionIds);
	ImGui::End();
	ImGui::Render();
	return result;
}

void DrawTwoPickerFrame(const String& firstControl, const String& secondControl,
                        const ReferencePickerTransaction* transaction)
{
	ImGui::NewFrame();
	ImGui::SetNextWindowSize({600, 400}, ImGuiCond_Always);
	ImGui::Begin("Reference Picker Test");
	DrawReferencePickerButton(firstControl, "Alpha", "Texture", Options(), "asset:a", "test-owner", transaction);
	DrawReferencePickerButton(secondControl, "Alpha", "Texture", Options(), "asset:a", "test-owner", transaction);
	ImGui::End();
	ImGui::Render();
}

void TestReferencePickerContextClear()
{
	const String control = "scene:context-clear";
	ReferencePickerContextActionIds actions;
	Expect(
	    !DrawFrame(control, Options(), "asset:a", "context-clear", nullptr, nullptr, false, nullptr, nullptr, &actions),
	    "Drawing a reference field changed its value before a context command");
	auto& io = ImGui::GetIO();
	io.AddMousePosEvent(actions.anchorCenter.x, actions.anchorCenter.y);
	DrawFrame(control, Options(), "asset:a", "context-clear", nullptr, nullptr, false, nullptr, nullptr, &actions);
	io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
	DrawFrame(control, Options(), "asset:a", "context-clear", nullptr, nullptr, false, nullptr, nullptr, &actions);
	io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
	DrawFrame(control, Options(), "asset:a", "context-clear", nullptr, nullptr, false, nullptr, nullptr, &actions);
	Expect(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
	       "Right-clicking a reference field did not open a context popup");
	Expect(actions.clear != 0, "Reference context popup did not submit a Clear item");
	ImGui::ActivateItemByID(actions.clear);
	const auto cleared =
	    DrawFrame(control, Options(), "asset:a", "context-clear", nullptr, nullptr, false, nullptr, nullptr, &actions);
	Expect(cleared && *cleared == 0, "The reference field's Clear context action did not return None");
}

void OpenReferenceContextMenu(const String& control, const String& currentKey, ReferencePickerContextActionIds& actions,
                              const std::function<Result<void>()>& locate = {})
{
	Expect(!DrawFrame(control, Options(), currentKey, "context-actions", nullptr, nullptr, false, nullptr, nullptr,
	                  &actions, locate),
	       "Drawing a reference field changed its value before a context command");
	auto& io = ImGui::GetIO();
	io.AddMousePosEvent(actions.anchorCenter.x, actions.anchorCenter.y);
	DrawFrame(control, Options(), currentKey, "context-actions", nullptr, nullptr, false, nullptr, nullptr, &actions,
	          locate);
	io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
	DrawFrame(control, Options(), currentKey, "context-actions", nullptr, nullptr, false, nullptr, nullptr, &actions,
	          locate);
	io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
	DrawFrame(control, Options(), currentKey, "context-actions", nullptr, nullptr, false, nullptr, nullptr, &actions,
	          locate);
	Expect(!ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Reference field context menu did not open");
}

void TestReferencePickerContextSelectAndLocate()
{
	ReferencePickerContextActionIds actions;
	const String selectControl = "scene:context-select";
	OpenReferenceContextMenu(selectControl, "asset:a", actions);
	Expect(actions.select != 0, "Reference field context menu omitted Select");
	ImGui::ActivateItemByID(actions.select);
	DrawFrame(selectControl, Options(), "asset:a", "context-actions", nullptr, nullptr, false, nullptr, nullptr,
	          &actions);
	Expect(HasActiveReferencePickerSession(ImGui::GetCurrentContext()),
	       "The reference field context Select action did not open its picker session");
	CancelReferencePickerSessions(ImGui::GetCurrentContext());

	const String locateControl = "scene:context-locate";
	actions = {};
	Int32 locateCount = 0;
	auto locate = [&]()
	{
		++locateCount;
		return Result<void>{};
	};
	OpenReferenceContextMenu(locateControl, "asset:a", actions, locate);
	Expect(actions.locate != 0, "Reference field context menu omitted Locate for a navigable target");
	ImGui::ActivateItemByID(actions.locate);
	DrawFrame(locateControl, Options(), "asset:a", "context-actions", nullptr, nullptr, false, nullptr, nullptr,
	          &actions, locate);
	Expect(locateCount == 1, "The reference field context Locate action did not invoke its captured target");
	Expect(ImGui::GetCurrentContext()->OpenPopupStack.empty() &&
	           !HasActiveReferencePickerSession(ImGui::GetCurrentContext()),
	       "Reference Locate navigation left an unrelated popup or picker open");
}
} // namespace

int main()
{
	try
	{
		auto* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		ImGui::GetIO().DisplaySize = {800, 600};
		ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		TestReferencePickerContextClear();
		TestReferencePickerContextSelectAndLocate();

		const String firstControl = "scene:one:position:albedo";
		const ReferencePickerOption assetCandidate{"asset:shared", "Shared", "Texture", "Assets/Shared.png", true};
		const ReferencePickerOption sceneCandidate{"scene:shared", "Shared", "Texture", "Scene", true};
		const ReferencePickerOption noneCandidate{"none", "None", "None", {}, true};
		Expect(IsReferencePickerCandidateVisible(assetCandidate, "shared", ReferencePickerSourceFilter::Assets),
		       "Asset source filtering hid a matching candidate");
		Expect(!IsReferencePickerCandidateVisible(sceneCandidate, "shared", ReferencePickerSourceFilter::Assets),
		       "Asset source filtering included a scene candidate");
		Expect(IsReferencePickerCandidateVisible(sceneCandidate, "shared", ReferencePickerSourceFilter::Scene),
		       "Scene source filtering hid a matching candidate");
		Expect(!IsReferencePickerCandidateVisible(assetCandidate, "shared", ReferencePickerSourceFilter::Scene),
		       "Scene source filtering included an asset candidate");
		Expect(IsReferencePickerCandidateVisible(noneCandidate, "no match", ReferencePickerSourceFilter::Scene, true),
		       "None disappeared when the search or source filter had no matches");
		Expect(!IsReferencePickerCandidateVisible(assetCandidate, "no match", ReferencePickerSourceFilter::All),
		       "A nonmatching search candidate remained visible");
		ImGui::ActivateItemByID(PickerButtonId(firstControl));
		Expect(!DrawFrame(firstControl, Options(), "asset:a"), "Opening a picker unexpectedly assigned a value");
		auto* popup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
		Expect(popup != nullptr, "Reference picker did not create its popup window");
		ImGui::ClosePopupToLevel(0, true);
		const auto reorderedResult = DrawFrame(firstControl, Options(true), "asset:a");
		Expect(reorderedResult && *reorderedResult == 2,
		       "Reordering candidates delivered the selected stable key to the wrong candidate");

		const String transactionControl = "scene:one:surface:transaction";
		Vector<String> references{"asset:original-one", "asset:original-two"};
		Vector<String> prefabOverrides{"override-one", "override-two"};
		bool unrelatedDirty = false;
		int previewCount = 0;
		int restoreCount = 0;
		ReferencePickerTransaction transaction;
		transaction.capture = [&]()
		{
			return Result<nlohmann::json>::Success(
			    nlohmann::json{std::string(references[0].c_str()), std::string(references[1].c_str())});
		};
		transaction.preview = [&](Size index)
		{
			++previewCount;
			references.assign(2, Options()[index].key);
			return Result<void>{};
		};
		transaction.restore = [&](const nlohmann::json& checkpoint)
		{
			++restoreCount;
			references = {String(checkpoint.at(0).get<std::string>().c_str()),
			              String(checkpoint.at(1).get<std::string>().c_str())};
			return Result<void>{};
		};
		ImGui::ActivateItemByID(PickerButtonId(transactionControl));
		Expect(!DrawFrame(transactionControl, Options(), "asset:a", "test-owner", &transaction),
		       "Opening a transactional picker assigned a value");
		Expect(!ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Transactional picker did not open a popup");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
		Expect(!DrawFrame(transactionControl, Options(), "asset:a", "test-owner", &transaction),
		       "Trial selection was reported as an accepted result");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
		Expect(previewCount == 1 && references == Vector<String>({"asset:b", "asset:b"}),
		       "Navigating to a candidate did not apply one live trial to the full target group");
		unrelatedDirty = true;
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		Expect(!DrawFrame(transactionControl, Options(), "asset:a", "test-owner", &transaction),
		       "Cancelling a trial returned an accepted result");
		Expect(restoreCount == 1 && references == Vector<String>({"asset:original-one", "asset:original-two"}),
		       "Cancelling a trial did not restore each target's original value");
		Expect(unrelatedDirty && prefabOverrides == Vector<String>({"override-one", "override-two"}),
		       "Cancelling a trial cleared an unrelated Dirty state or changed Prefab overrides");

		unrelatedDirty = false;
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		Expect(!DrawFrame(transactionControl, Options(), "asset:a", "test-owner", &transaction),
		       "Releasing Escape unexpectedly accepted the cancelled candidate");

		const String singleTargetControl = "scene:one:surface:single-target";
		String singleReference = "asset:original";
		ReferencePickerTransaction singleTargetTransaction;
		singleTargetTransaction.capture = [&]()
		{ return Result<nlohmann::json>::Success(nlohmann::json(singleReference.c_str())); };
		singleTargetTransaction.preview = [&](Size index)
		{
			singleReference = Options()[index].key;
			return Result<void>{};
		};
		singleTargetTransaction.restore = [&](const nlohmann::json& checkpoint)
		{
			singleReference = checkpoint.get<std::string>().c_str();
			return Result<void>{};
		};
		ImGui::ActivateItemByID(PickerButtonId(singleTargetControl));
		Expect(!DrawFrame(singleTargetControl, Options(), "asset:a", "test-owner", &singleTargetTransaction),
		       "Opening a single-target picker assigned a value");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
		Expect(!DrawFrame(singleTargetControl, Options(), "asset:a", "test-owner", &singleTargetTransaction),
		       "Single-target trial selection was reported as accepted");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
		Expect(singleReference == "asset:b", "Single-target picker did not apply its live trial");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		Expect(!DrawFrame(singleTargetControl, Options(), "asset:a", "test-owner", &singleTargetTransaction),
		       "Cancelling a single-target trial returned an accepted result");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		Expect(singleReference == "asset:original", "Cancelling did not restore the single target's original value");
		Expect(!DrawFrame(singleTargetControl, Options(), "asset:a", "test-owner", &singleTargetTransaction),
		       "A released single-target popup delivered a stale selection");

		const String removedCandidateControl = "scene:one:surface:removed-candidate";
		ImGui::ActivateItemByID(PickerButtonId(removedCandidateControl));
		Expect(!DrawFrame(removedCandidateControl, Options(), "asset:a", "test-owner", &transaction),
		       "Opening a picker for candidate invalidation assigned a value");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
		Expect(!DrawFrame(removedCandidateControl, Options(), "asset:a", "test-owner", &transaction),
		       "Trial selection returned an accepted result before candidate invalidation");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
		Expect(previewCount == 2 && references == Vector<String>({"asset:b", "asset:b"}),
		       "Candidate invalidation did not begin from a live trial");
		Vector<ReferencePickerOption> refreshedOptions{Options()[0], Options()[1]};
		Expect(!DrawFrame(removedCandidateControl, refreshedOptions, "asset:a", "test-owner", &transaction),
		       "A candidate removed during refresh was accepted");
		Expect(restoreCount == 2 && references == Vector<String>({"asset:original-one", "asset:original-two"}),
		       "A removed candidate did not cancel its live trial and restore every original target");
		Expect(!DrawFrame(removedCandidateControl, refreshedOptions, "asset:a", "test-owner", &transaction),
		       "A stale removed-candidate session delivered a result on release");

		const String boundaryControl = "scene:one:surface:boundary-cancel";
		ImGui::ActivateItemByID(PickerButtonId(boundaryControl));
		Expect(!DrawFrame(boundaryControl, Options(), "asset:a", "test-owner", &transaction),
		       "Opening a picker for boundary cancellation assigned a value");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
		Expect(!DrawFrame(boundaryControl, Options(), "asset:a", "test-owner", &transaction),
		       "Trial selection returned an accepted result before boundary cancellation");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
		Expect(references == Vector<String>({"asset:b", "asset:b"}),
		       "Boundary cancellation test did not begin from a live trial");
		int boundaryCompleted = 0;
		Expect(RequestCancelReferencePickerSessions(context, [&] { ++boundaryCompleted; }),
		       "An active picker did not defer its boundary action");
		Expect(boundaryCompleted == 0, "Boundary action ran before the original values were restored");
		Expect(!DrawFrame(boundaryControl, Options(), "asset:a", "test-owner", &transaction),
		       "Boundary cancellation accepted a candidate");
		Expect(restoreCount == 3 && references == Vector<String>({"asset:original-one", "asset:original-two"}),
		       "Boundary cancellation did not restore every original target");
		ProcessCompletedReferencePickerCancellations(context);
		Expect(boundaryCompleted == 1 && !HasActiveReferencePickerSession(context),
		       "Boundary action did not run exactly once after cancellation completed");

		const String externalClickControl = "scene:one:surface:external-click";
		ImGui::ActivateItemByID(PickerButtonId(externalClickControl));
		Expect(!DrawFrame(externalClickControl, Options(), "asset:a", "test-owner", &transaction),
		       "Opening a picker for external-click cancellation assigned a value");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
		Expect(!DrawFrame(externalClickControl, Options(), "asset:a", "test-owner", &transaction),
		       "Trial selection returned an accepted result before external click");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
		auto* externalPopup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
		const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		const ImVec2 corners[] = {
		    {mainViewport->Pos.x + 4.0f, mainViewport->Pos.y + 4.0f},
		    {mainViewport->Pos.x + mainViewport->Size.x - 4.0f, mainViewport->Pos.y + 4.0f},
		    {mainViewport->Pos.x + 4.0f, mainViewport->Pos.y + mainViewport->Size.y - 4.0f},
		    {mainViewport->Pos.x + mainViewport->Size.x - 4.0f, mainViewport->Pos.y + mainViewport->Size.y - 4.0f}};
		const auto outsideCorner = std::find_if(std::begin(corners), std::end(corners),
		                                        [&](const ImVec2& point)
		                                        {
			                                        return point.x < externalPopup->Pos.x ||
			                                               point.y < externalPopup->Pos.y ||
			                                               point.x >= externalPopup->Pos.x + externalPopup->Size.x ||
			                                               point.y >= externalPopup->Pos.y + externalPopup->Size.y;
		                                        });
		Expect(outsideCorner != std::end(corners), "Picker unexpectedly covered the entire editor viewport");
		bool externalClickHandled = false;
		Expect(!DrawFrame(externalClickControl, Options(), "asset:a", "test-owner", &transaction, nullptr, true,
		                  &externalClickHandled, outsideCorner),
		       "Clicking another editor panel accepted a picker candidate");
		Expect(externalClickHandled, "External editor click was not recognized while the picker was open");
		Expect(restoreCount == 4 && references == Vector<String>({"asset:original-one", "asset:original-two"}),
		       "An external editor click did not restore the live trial");

		const String otherFieldControl = "scene:one:surface:other-field";
		ImGui::ActivateItemByID(PickerButtonId(externalClickControl));
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
		Expect(references == Vector<String>({"asset:b", "asset:b"}),
		       "First picker did not retain its live trial before opening a second field");
		ImGui::ActivateItemByID(PickerButtonId(otherFieldControl));
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		Expect(references == Vector<String>({"asset:b", "asset:b"}),
		       "Opening another field canceled the first picker before its owner could restore it");
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		Expect(restoreCount == 5 && references == Vector<String>({"asset:original-one", "asset:original-two"}) &&
		           !HasActiveReferencePickerSession(context),
		       "Opening another field did not restore and terminate the prior picker before switching fields");
		ImGui::ActivateItemByID(PickerButtonId(otherFieldControl));
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		Expect(HasActiveReferencePickerSession(context),
		       "The replacement field could not open after prior cancellation");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		DrawTwoPickerFrame(externalClickControl, otherFieldControl, &transaction);
		Expect(restoreCount == 6 && !HasActiveReferencePickerSession(context),
		       "The replacement picker did not cancel cleanly after the previous session was restored");

		const String acceptedControl = "scene:one:surface:accepted";
		ImGui::ActivateItemByID(PickerButtonId(acceptedControl));
		Expect(!DrawFrame(acceptedControl, Options(), "asset:a", "test-owner", &transaction),
		       "Opening a picker for acceptance testing assigned a value");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
		nlohmann::json acceptedCheckpoint;
		const auto acceptedResult =
		    DrawFrame(acceptedControl, Options(), "asset:a", "test-owner", &transaction, &acceptedCheckpoint);
		Expect(acceptedResult && *acceptedResult == 1, "Enter did not accept the selected candidate");
		Expect(acceptedCheckpoint == nlohmann::json({"asset:original-one", "asset:original-two"}),
		       "Accepting a picker did not return the original multi-target checkpoint");
		Expect(restoreCount == 6 && references == Vector<String>({"asset:original-one", "asset:original-two"}),
		       "Accepting without a trial changed or restored the original target values");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);

		const String closeControl = "scene:one:surface:normal-close";
		ImGui::ActivateItemByID(PickerButtonId(closeControl));
		Expect(!DrawFrame(closeControl, Options(), "asset:a", "test-owner", &transaction),
		       "Opening a picker for normal-close testing assigned a value");
		ImGui::ClosePopupToLevel(0, true);
		nlohmann::json closeCheckpoint;
		const auto closeResult =
		    DrawFrame(closeControl, Options(), "asset:a", "test-owner", &transaction, &closeCheckpoint);
		Expect(closeResult && *closeResult == 1, "Normal popup close did not accept the current candidate");
		Expect(closeCheckpoint == nlohmann::json({"asset:original-one", "asset:original-two"}) && restoreCount == 6,
		       "Normal popup close did not preserve the trial checkpoint or restored values");

		const String oldTargetControl = "scene:one:position:normal";
		ImGui::ActivateItemByID(PickerButtonId(oldTargetControl));
		Expect(!DrawFrame(oldTargetControl, Options(), "asset:a"), "Opening a second picker assigned a value");
		const auto newTargetResult = DrawFrame("scene:two:position:normal", Options(), "asset:b", "target-two");
		Expect(!newTargetResult, "A target change received a result from the previous field session");
		Expect(!DrawFrame(oldTargetControl, Options(), "asset:a"),
		       "A stale picker session delivered a result after its target disappeared");

		const String duplicateControl = "scene:one:surface:map";
		Vector<ReferencePickerOption> duplicateNames{{"none", "None", "None", {}, true},
		                                             {"same:a", "Shared Name", "Texture", "Assets/A.png", true},
		                                             {"same:b", "Shared Name", "Texture", "Scene", true}};
		ImGui::ActivateItemByID(PickerButtonId(duplicateControl));
		Expect(!DrawFrame(duplicateControl, duplicateNames, "same:b"),
		       "Opening duplicate-name picker assigned a value");
		ImGui::ClosePopupToLevel(0, true);
		const auto sameNameResult = DrawFrame(duplicateControl, duplicateNames, "same:b");
		Expect(sameNameResult && *sameNameResult == 2,
		       "Candidates with the same display name were merged or rebound by name");

		const String disabledControl = "scene:one:surface:normal";
		Vector<ReferencePickerOption> disabledOptions{
		    {"none", "None", "None", {}, true},
		    {"disabled", "Unavailable", "Texture", "Assets/Unavailable.png", false}};
		ImGui::ActivateItemByID(PickerButtonId(disabledControl));
		Expect(!DrawFrame(disabledControl, disabledOptions, "disabled"), "Opening disabled picker assigned a value");
		ImGui::ClosePopupToLevel(0, true);
		Expect(!DrawFrame(disabledControl, disabledOptions, "disabled"),
		       "Closing a picker accepted a disabled candidate");

		const String searchControl = "scene:one:surface:occlusion";
		ImGui::ActivateItemByID(PickerButtonId(searchControl));
		Expect(!DrawFrame(searchControl, Options(), "asset:a"), "Opening search picker assigned a value");
		auto* searchPopup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
		ClickSearchInput(searchPopup);
		Expect(!DrawFrame(searchControl, Options(), "asset:a"), "Clicking the search field assigned a value");
		ImGui::GetIO().AddInputCharactersUTF8("no-candidate-matches-this");
		Expect(!DrawFrame(searchControl, Options(), "asset:a"), "Typing a search unexpectedly assigned a value");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		Expect(!DrawFrame(searchControl, Options(), "asset:a"), "Clearing the search unexpectedly assigned a value");
		Expect(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
		       "Escape closed the picker instead of clearing search");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		Expect(!DrawFrame(searchControl, Options(), "asset:a"), "Cancelling an empty search produced a reference");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		Expect(!DrawFrame(searchControl, Options(), "asset:a"), "Releasing Escape unexpectedly accepted a reference");

		Int32 previewBeginCount = 0;
		Int32 previewEndCount = 0;
		auto previewOptions = Options();
		previewOptions[1].beginPreview = [&]
		{
			++previewBeginCount;
			return Result<void>::Success();
		};
		previewOptions[1].endPreview = [&] { ++previewEndCount; };
		const String previewLifecycleControl = "scene:one:surface:preview-lifecycle";
		ImGui::ActivateItemByID(PickerButtonId(previewLifecycleControl));
		Expect(!DrawFrame(previewLifecycleControl, previewOptions, "asset:a"),
		       "Opening a picker with a custom preview assigned a value");
		Expect(previewBeginCount == 1 && previewEndCount == 0,
		       "The custom preview lifecycle did not begin once for the selected candidate");
		CancelReferencePickerSessions(context);
		Expect(previewEndCount == 1, "Cancelling the picker did not pair the custom preview lifecycle exactly once");

		Vector<Scripting::ManagedPreviewPhase> previewPhases;
		UInt64 previewInstance = 0;
		auto extensionOptions = Options();
		extensionOptions[1].createCustomPreview = [&]()
		{
			auto extension = std::make_shared<AssetPreviewExtension>(
			    "picker-test",
			    [&](UInt64 instance, Scripting::ManagedPreviewPhase phase, const ViewportRect&, ManagedEditorContext&)
			    {
				    if (!previewInstance)
				    {
					    previewInstance = instance;
				    }
				    Expect(previewInstance == instance, "A picker Preview changed its managed instance token");
				    previewPhases.push_back(phase);
				    Scripting::ManagedPreviewResult result;
				    result.hasPreview = phase == Scripting::ManagedPreviewPhase::Query;
				    return Result<Scripting::ManagedPreviewResult>::Success(std::move(result));
			    });
			extension->Bind({});
			auto queried = extension->Invoke(Scripting::ManagedPreviewPhase::Query);
			if (!queried)
			{
				return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(queried.error);
			}
			return Result<std::shared_ptr<AssetPreviewExtension>>::Success(std::move(extension));
		};
		const String extensionControl = "scene:one:surface:custom-preview";
		ImGui::ActivateItemByID(PickerButtonId(extensionControl));
		Expect(!DrawFrame(extensionControl, extensionOptions, "asset:a"),
		       "Opening a picker with a custom Preview assigned a value");
		Expect(previewPhases == Vector<Scripting::ManagedPreviewPhase>({Scripting::ManagedPreviewPhase::Query}),
		       "The isolated Preview was not queried once on creation");
		(void)RenderReferencePickerPreviews(1.0 / 60.0, {});
		CancelReferencePickerSessions(context);
		Expect(previewPhases == Vector<Scripting::ManagedPreviewPhase>({Scripting::ManagedPreviewPhase::Query,
		                                                                Scripting::ManagedPreviewPhase::Draw,
		                                                                Scripting::ManagedPreviewPhase::Release}),
		       "The isolated Preview did not pair draw and release with its query");

		ImGui::DestroyContext(context);
		std::cout << "Reference picker session tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
