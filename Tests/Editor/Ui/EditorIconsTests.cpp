#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <Ui/EditorIcons.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Console/EditorConsolePanel.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/Lighting.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakBuilder.hpp>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
using namespace Ursa::Rhi;
using Icons::Icon;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void TestTypeMapping()
{
	Expect(Icons::ForAsset(true, ResourceKind::Texture, {}, {}) == Icon::Folder, "Directory must take precedence");
	const ResourceKind kinds[]{ResourceKind::Texture, ResourceKind::Material, ResourceKind::Scene,
	                           ResourceKind::Prefab,  ResourceKind::Shader,   ResourceKind::Mesh};
	const Icon expected[]{Icon::Texture, Icon::Material, Icon::Scene, Icon::Prefab, Icon::Shader, Icon::Mesh};
	for (Size i = 0; i < 6; ++i)
	{
		Expect(Icons::ForAsset(false, kinds[i], {}, {}) == expected[i], "Resource type icon mismatch");
	}
	Expect(Icons::ForAsset(false, {}, "ursa.Model", "mesh.unknown") == Icon::Model, "Model importer lost");
	Expect(Icons::ForAsset(false, {}, "ursa.Texture", "image.unknown") == Icon::Texture, "Texture importer lost");
	Expect(Icons::ForAsset(false, {}, "ursa.ScriptableAsset", "data.unknown") == Icon::ScriptableObject,
	       "Scriptable asset importer lost");
	Expect(Icons::ForAsset(false, {}, {}, "Script.CS") == Icon::Script, "Case-insensitive source extension lost");
	Expect(Icons::ForAsset(false, {}, {}, "Model.GLB") == Icon::Model, "Source model icon lost");
	Expect(Icons::ForAsset(false, {}, {}, "data.ursaasset") == Icon::ScriptableObject, "Scriptable source icon lost");
	Expect(Icons::ForAsset(false, {}, {}, "data.unknown") == Icon::File, "Unknown file needs generic fallback");
	Expect(Icons::ForComponent("ursa.MeshFilter") == Icon::Mesh &&
	           Icons::ForComponent("ursa.SkinnedMeshRenderer") == Icon::SkinnedMesh,
	       "Mesh component identity lost");
	Expect(Icons::ForComponent("Project.Custom", true) == Icon::Script &&
	           Icons::ForComponent("Unknown") == Icon::Component,
	       "Custom component fallback lost");
	auto object = GameObject::Create("same name");
	Expect(Icons::ForObject(object) == Icon::GameObject, "Empty object fallback lost");
	Take(object->AddComponent<PointLightComponent>());
	Expect(Icons::ForObject(object) == Icon::PointLight, "Light identity lost");
	Take(object->AddComponent<CameraComponent>());
	Expect(Icons::ForObject(object) == Icon::Camera && Icons::ForObject(object, true) == Icon::Prefab,
	       "Object type priority lost");
}

void CaptureGallery(const DevicePtr& device, Float32 dpi, const char* source = "Loose")
{
	auto bridge = Take(ImGuiBridge::Create(device));
	Expect(Icons::Available(), "Engine icon atlas failed to load");
	constexpr UInt32 logicalWidth = 1120;
	constexpr UInt32 logicalHeight = 720;
	const auto width = static_cast<UInt32>(logicalWidth * dpi);
	const auto height = static_cast<UInt32>(logicalHeight * dpi);
	for (int frame = 0; frame < 2; ++frame)
	{
		Done(bridge->BeginFrame(1.f / 60.f, logicalWidth, logicalHeight, width, height));
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize({static_cast<Float32>(logicalWidth), static_cast<Float32>(logicalHeight)});
		ImGui::Begin("Editor icon acceptance", nullptr, ImGuiWindowFlags_NoDecoration);
		ImGui::Text("Ursa Editor - %.0f%% framebuffer density", dpi * 100.f);
		Icons::Text(Icon::Folder, u8"资源 / 文件夹 / 中文名称");
		for (Size i = 0; i < static_cast<Size>(Icon::Count); ++i)
		{
			if (i % 8 != 0)
			{
				ImGui::SameLine();
			}
			ImGui::PushID(static_cast<int>(i));
			ImGui::BeginGroup();
			Icons::Button("icon", static_cast<Icon>(i), Icons::Name(static_cast<Icon>(i)));
			ImGui::TextUnformatted(Icons::Name(static_cast<Icon>(i)));
			ImGui::Dummy({126.f, 2.f});
			ImGui::EndGroup();
			ImGui::PopID();
		}
		Icons::Section(Icon::Settings, "States and compact controls");
		Icons::Button("run", Icon::Stop, "Stop", true);
		Icons::SameLine();
		ImGui::BeginDisabled();
		Icons::Button("step", Icon::Step, "Step (pause required)");
		ImGui::EndDisabled();
		Icons::SameLine();
		Icons::Button("Save");
		Icons::SameLine();
		Icons::Button("Delete GameObject");
		ImGui::BeginChild("narrow", {190.f, 92.f}, ImGuiChildFlags_Borders);
		for (const char* label : {"Trace 128", "Debug 256", "Info 512", "Warning 3", "Error 1"})
		{
			Icons::SameLine(130.f);
			Icons::Button(label);
		}
		ImGui::EndChild();
		ImGui::End();
		Done(bridge->EndFrame());
	}
	auto color = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto readback = Take(device->CreateBuffer(
	    {static_cast<Size>(width) * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	RenderTarget target;
	target.color = color;
	Done(command->BeginRendering(target));
	Done(bridge->Record(command, color->desc.format, width, height));
	Done(command->EndRendering());
	Done(command->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
	Done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	Done(command->CopyTextureToBuffer(color, readback));
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	Vector<UInt8> pixels(static_cast<Size>(width) * height * 4);
	Done(readback->Read(0, pixels.data(), pixels.size()));
	const std::filesystem::path directory(URSA_ICON_EVIDENCE_DIRECTORY);
	std::filesystem::create_directories(directory);
	const auto path =
	    directory / (std::string(source) + "-EditorIcons-" + std::to_string(static_cast<int>(dpi * 100)) + ".png");
	Expect(stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(),
	                      static_cast<int>(width) * 4) != 0,
	       "Cannot save GPU acceptance image");
}

void TestManifestAndControls(const DevicePtr& device)
{
	auto bridge = Take(ImGuiBridge::Create(device));
	auto files = Take(OpenEngineFileSystem());
	auto bytes = Take(files.Read(Take(VirtualPath::Parse("ursa:Icons/Editor/EditorIcons.json"))));
	const String text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	auto document = nlohmann::json::parse(text.begin(), text.end());
	Expect(!Icons::SetAtlas(bridge->Context(), 1, "{}"), "Malformed manifest accepted");
	document["icons"]["Play"]["uv1"][0] = 2.f;
	Expect(!Icons::SetAtlas(bridge->Context(), 1, ToUrsaString(document.dump())), "Out-of-range manifest accepted");
	document = nlohmann::json::parse(text.begin(), text.end());
	document["icons"]["Stop"] = document["icons"]["Play"];
	Expect(!Icons::SetAtlas(bridge->Context(), 1, ToUrsaString(document.dump())), "Overlapping atlas entries accepted");
	// Use fallback mode to verify controls remain named and reachable without the atlas.
	Done(bridge->BeginFrame(1.f / 60.f, 500, 240));
	ImGui::SetNextWindowPos({0, 0});
	ImGui::SetNextWindowSize({500, 240});
	ImGui::Begin("Control identity");
	Icons::Button("run", Icon::Play, "Play");
	const auto runId = GImGui->LastItemData.ID;
	Icons::Button("run", Icon::Stop, "Stop", true);
	Expect(GImGui->LastItemData.ID == runId, "Run state changed stable button identity");
	ImGui::PushID("first-object");
	Icons::Button("same-name", Icon::More, "Component actions");
	const auto firstId = GImGui->LastItemData.ID;
	ImGui::PopID();
	ImGui::PushID("second-object");
	Icons::Button("same-name", Icon::More, "Component actions");
	Expect(GImGui->LastItemData.ID != firstId, "Repeated labels collided across objects");
	ImGui::PopID();
	ImGui::BeginDisabled();
	Expect(!Icons::Button("disabled", Icon::Step, "Step"), "Disabled action executed");
	Expect((GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0, "Disabled semantics lost");
	ImGui::EndDisabled();
	ImGui::End();
	Done(bridge->EndFrame());
	ImVec2 enabledCenter{};
	ImVec2 disabledCenter{};
	Size enabledClicks = 0;
	Size disabledClicks = 0;
	bool disabledHovered = false;
	for (int frame = 0; frame < 8; ++frame)
	{
		auto& io = ImGui::GetIO();
		if (frame == 2 || frame == 3)
		{
			io.AddMousePosEvent(enabledCenter.x, enabledCenter.y);
		}
		if (frame >= 4)
		{
			io.AddMousePosEvent(disabledCenter.x, disabledCenter.y);
		}
		if (frame == 2 || frame == 5)
		{
			io.AddMouseButtonEvent(0, true);
		}
		if (frame == 3 || frame == 6)
		{
			io.AddMouseButtonEvent(0, false);
		}
		Done(bridge->BeginFrame(1.f / 60.f, 500, 240));
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize({500, 240});
		ImGui::Begin("Click safety", nullptr, ImGuiWindowFlags_NoDecoration);
		enabledClicks += Icons::Button("enabled", Icon::Play, "Play");
		const auto enabledMin = ImGui::GetItemRectMin();
		const auto enabledMax = ImGui::GetItemRectMax();
		enabledCenter = {(enabledMin.x + enabledMax.x) * 0.5f, (enabledMin.y + enabledMax.y) * 0.5f};
		ImGui::BeginDisabled();
		disabledClicks += Icons::Button("disabled", Icon::Step, "Pause required");
		const auto disabledMin = ImGui::GetItemRectMin();
		const auto disabledMax = ImGui::GetItemRectMax();
		disabledCenter = {(disabledMin.x + disabledMax.x) * 0.5f, (disabledMin.y + disabledMax.y) * 0.5f};
		disabledHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		ImGui::End();
		Done(bridge->EndFrame());
	}
	Expect(enabledClicks == 1 && disabledClicks == 0 && disabledHovered,
	       "Pointer clicks or disabled-hover explanations lost their semantics");
}

void TestIconFailure(const DevicePtr& device)
{
	auto files = Take(OpenEngineFileSystem());
	const auto entries = Take(files.List(engineScheme));
	for (bool corrupt : {false, true})
	{
		auto memory = std::make_shared<MemoryFileProvider>();
		for (const auto& entry : entries)
		{
			if (entry.path.relative == "Icons/Editor/EditorIcons.png")
			{
				if (corrupt)
				{
					Done(memory->WriteAtomic(entry.path.relative, Vector<UInt8>{1, 2, 3}, true));
				}
				continue;
			}
			Done(memory->WriteAtomic(entry.path.relative, Take(files.Read(entry.path)), true));
		}
		VirtualFileSystem altered;
		Done(altered.MountProvider(engineScheme, memory, MountAccess::ReadOnly));
		ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(std::move(altered)));
		Size diagnostics = 0;
		const auto listener = Log::Subscribe(
		    [&](const LogRecord& record)
		    {
			    diagnostics +=
			        record.category == "Editor" && record.message.find("Editor icons unavailable") != String::npos;
		    });
		auto bridge = Take(ImGuiBridge::Create(device));
		ConfigureEngineFileSystem({});
		Expect(!Icons::Available(), "Missing or corrupt icons should use text fallback");
		for (int frame = 0; frame < 3; ++frame)
		{
			Done(bridge->BeginFrame(1.f / 60.f, 500, 240));
			ImGui::Begin("Icon fallback");
			Icons::Button("Save");
			ImGui::End();
			Done(bridge->EndFrame());
		}
		Log::Unsubscribe(listener);
		Expect(diagnostics == 1, "Icon failure must diagnose once per initialization");
	}
}

void TestConsoleFilters(const DevicePtr& device)
{
	auto bridge = Take(ImGuiBridge::Create(device));
	EditorConsolePanel panel;
	const auto originalLevel = Log::Level();
	Log::SetLevel(LogLevel::Trace);
	for (Size i = 0; i < 5; ++i)
	{
		Log::Write(static_cast<LogLevel>(i), "FilterProbe", "independent filter");
	}
	Log::SetLevel(originalLevel);
	bool open = true;
	String diagnostic;
	for (int frame = 0; frame < 5; ++frame)
	{
		Done(bridge->BeginFrame(1.f / 60.f, 900, 400));
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize({900, 400});
		panel.Draw(open, diagnostic);
		auto* window = ImGui::FindWindowByName("Console");
		auto* table = ImGui::TableFindByID(ImHashStr("ConsoleEntries", 0, window->ID));
		Expect(table != nullptr, "Console table was not submitted");
		if (frame == 1)
		{
			Expect(table->CurrentRow == 4, "Default filters should hide Trace only");
			ImGui::FocusWindow(window);
			ImGui::ActivateItemByID(ImHashStr("Trace", 0, window->ID));
		}
		if (frame == 2)
		{
			Expect(table->CurrentRow == 5, "Trace filter did not enable its own level");
			ImGui::ActivateItemByID(ImHashStr("Debug", 0, window->ID));
		}
		if (frame == 3)
		{
			Expect(table->CurrentRow == 4, "Debug filter affected a different level");
			Expect(EditorConsolePanel::Counts(panel.Records()) == Array<Size, 5>{1, 1, 1, 1, 1},
			       "Filtering mutated retained severity counts");
			ImGui::ActivateItemByID(ImHashStr("Clear", 0, window->ID));
		}
		if (frame == 4)
		{
			Expect(panel.Records().empty(), "Clear icon did not clear console history");
		}
		Done(bridge->EndFrame());
	}
}

void TestConsoleCounts()
{
	Vector<LogRecord> records;
	for (LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error,
	                       LogLevel::Critical, LogLevel::Off})
	{
		LogRecord record;
		record.level = level;
		record.message = u8"中文日志";
		records.push_back(record);
	}
	const auto counts = EditorConsolePanel::Counts(records);
	Expect(counts == Array<Size, 5>{1, 1, 1, 1, 2},
	       "Five filter counts must include Critical with Error and exclude Off");
	Expect(EditorConsolePanel::Counts({}) == Array<Size, 5>{}, "Empty history counts must be zero");
	EditorConsolePanel panel;
	Log::Write(LogLevel::Info, "IconTests", u8"资源图标验收");
	Expect(!panel.Records().empty(), "Console did not receive diagnostic");
	panel.Clear();
	Expect(panel.Records().empty(), "Clear did not remove in-memory history");
	const auto originalLevel = Log::Level();
	Log::SetLevel(LogLevel::Trace);
	std::thread writer(
	    [&]()
	    {
		    for (Size i = 0; i < 10005; ++i)
		    {
			    Log::Write(static_cast<LogLevel>(i % 5), "BoundedConsole", "history capacity probe");
		    }
	    });
	for (Size i = 0; i < 20; ++i)
	{
		const auto snapshot = panel.Records();
		const auto snapshotCounts = EditorConsolePanel::Counts(snapshot);
		Size count = 0;
		for (const auto value : snapshotCounts)
		{
			count += value;
		}
		Expect(count == snapshot.size(), "Concurrent counts do not describe their own snapshot");
	}
	writer.join();
	Log::SetLevel(originalLevel);
	const auto bounded = panel.Records();
	Expect(bounded.size() == 10000 &&
	           EditorConsolePanel::Counts(bounded) == Array<Size, 5>{2000, 2000, 2000, 2000, 2000},
	       "Evicted log entries still contribute to filter counts");
	panel.Clear();
	Expect(EditorConsolePanel::Counts(panel.Records()) == Array<Size, 5>{}, "Clear retained severity counts");
}
} // namespace

int main()
{
	try
	{
		TestTypeMapping();
		TestConsoleCounts();
		auto device = Take(CreateDevice({}));
		TestConsoleFilters(device);
		for (Float32 dpi : {1.f, 1.5f, 2.f})
		{
			CaptureGallery(device, dpi);
		}
		// Round-trip exactly the published Engine view through a real package, then render at each density.
		auto published = Take(OpenEngineFileSystem());
		VirtualFileSystem output;
		const std::filesystem::path evidence(URSA_ICON_EVIDENCE_DIRECTORY);
		Done(output.Mount("out", evidence, MountAccess::ReadWrite));
		Done(PakBuilder::Build(published, {engineScheme}, output, Take(VirtualPath::Parse("out:EditorIcons.pak"))));
		auto packaged = Take(OpenEngineFileSystem(evidence / "EditorIcons.pak"));
		ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(std::move(packaged)));
		for (Float32 dpi : {1.f, 1.5f, 2.f})
		{
			CaptureGallery(device, dpi, "Pak");
		}
		ConfigureEngineFileSystem({});
		TestManifestAndControls(device);
		TestIconFailure(device);
		Done(device->WaitIdle());
		std::cout << "Editor icon mapping, resources, fallback, identity, counts and DPI captures passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		ConfigureEngineFileSystem({});
		std::cerr << error.what() << '\n';
		return 1;
	}
}
