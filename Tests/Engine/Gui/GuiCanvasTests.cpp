#include <Gui/GuiCanvas.hpp>
#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Core/Log.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace
{
using namespace Ursa;
using namespace Ursa::Rendering;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* value)
{
	return Take(VirtualPath::Parse(value));
}

void Paint(Gui::Session& gui, const MaterialTexturePtr& image, bool empty = false)
{
	for (const auto type : {Gui::EventType::Layout, Gui::EventType::Repaint})
	{
		gui.BeginEvent({type}, {192, 112});
		gui.BeginComponent(1);
		if (!empty)
		{
			gui.Button({8, 8, 80, 24}, "");
			gui.DrawTexture({110, 8, 20, 20}, image);
			gui.BeginGroup({8, 60, 60, 22});
			gui.BeginGroup({0, 0, 30, 22});
			gui.Label({0, 0, 160, 22}, "\xe4\xb8\xad\xe6\x96\x87 Unicode");
			gui.EndGroup();
			gui.EndGroup();
		}
		gui.EndComponent();
		gui.EndEvent();
	}
}

void Composite(Rhi::DevicePtr device, const std::shared_ptr<Gui::Canvas>& canvas, bool abort, bool expectGui)
{
	auto executor = Take(RenderCommandExecutor::Create(device));
	auto output = Take(CreateRenderTexture(*device, {192, 112, 0, 0}));
	Done(executor->Begin(output, Rhi::ResourceState::ShaderRead, {}, {1, 1, canvas}));
	RenderCommand clear;
	clear.opcode = RenderCommandOpcode::ClearTarget;
	clear.clearFlags = 1;
	clear.color = {0, 0.25f, 0.5f, 1};
	Done(executor->Execute({clear}));
	Done(executor->Submit());
	auto fence = Take(abort ? executor->Abort() : executor->End());
	if (fence)
	{
		Done(fence->Wait());
	}
	Expect(output->colorState == Rhi::ResourceState::ShaderRead, "GUI output did not restore ShaderRead");
	auto pixels = Take(ReadRenderTexture(*device, *output));
	const auto pixel = [&](int x, int y, int c) { return pixels[(y * 192 + x) * 4 + c]; };
	if (expectGui)
	{
		Expect(pixel(12, 12, 0) >= 48 && pixel(12, 12, 0) <= 52, "GUI was not composed after the scene color");
		Expect(pixel(115, 14, 0) > 120 && pixel(115, 14, 2) > 55 && pixel(115, 14, 2) < 72,
		       "GUI texture alpha did not blend with final scene color");
		bool chineseVisible = false;
		for (int y = 60; y < 82; ++y)
		{
			for (int x = 8; x < 68; ++x)
			{
				chineseVisible |= pixel(x, y, 0) > 100;
			}
		}
		Expect(chineseVisible, "Runtime Chinese font did not draw glyphs");
		Expect(pixel(85, 70, 0) < 5, "Group clipping escaped its visible region");
		Expect(pixel(45, 70, 0) < 5, "Nested group clipping escaped its inner visible region");
	}
	else
	{
		Expect(pixel(12, 12, 0) < 5, "Aborted or empty GUI pass was composed");
	}
	Expect(executor->Statistics().physicalSubmissions == 1, "GUI added an extra GPU submission");
	Done(executor->Drain());
}
} // namespace

int main()
{
#ifdef _MSC_VER
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
	LogSession log;
	try
	{
		Rhi::DeviceDesc desc;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(Rhi::CreateDevice(desc));
		auto source = Take(OpenEngineFileSystem());
		const auto fontPath = Path("ursa:Fonts/Runtime/NotoSansSC-VF.ttf");
		const auto shaderPath = Path("ursa:Shaders/Runtime/GUI.ursashaderasset");
		auto font = Take(source.Read(fontPath)), shader = Take(source.Read(shaderPath));
		const auto* project = std::getenv("URSA_CONTENT_PROJECT");
		Expect(project != nullptr, "GUI resource publication test requires the imported content fixture");
		auto storage = std::make_shared<VirtualFileSystem>();
		Done(storage->Mount("lib", std::filesystem::path(project) / "Library", MountAccess::ReadOnly));
		for (const auto profile : {"Editor", "Player"})
		{
			auto snapshot = Take(ReadRuntimeContent(*storage, URSA_GUI_TEST_CONFIGURATION, profile));
			auto view = Take(OpenRuntimeContentView(storage, snapshot));
			Expect(Take(view.Read(fontPath)) == font && Take(view.Read(shaderPath)) == shader,
			       "Published GUI resource identity differs between Editor and Player");
			Expect(bool(Gui::Canvas::Create(device, view)), "Published profile cannot initialize runtime GUI");
		}
		VirtualFileSystem invalid;
		auto memory = std::make_shared<MemoryFileProvider>();
		Done(invalid.MountProvider("ursa", memory, MountAccess::ReadWrite));
		auto missing = Gui::Canvas::Create(device, invalid);
		Expect(!missing && missing.error.find("Fonts/Runtime") != String::npos,
		       "Missing runtime font diagnostic omitted the path");
		Done(invalid.Write(fontPath, {0, 1, 0, 0}));
		Expect(!Gui::Canvas::Create(device, invalid), "Truncated runtime font was accepted");
		Done(invalid.Write(fontPath, font));
		missing = Gui::Canvas::Create(device, invalid);
		Expect(!missing && missing.error.find("Shaders/Runtime") != String::npos,
		       "Missing runtime shader diagnostic omitted the path");
		Done(invalid.Write(shaderPath, {1, 2, 3}));
		Expect(!Gui::Canvas::Create(device, invalid), "Corrupt runtime shader was accepted");
		// This package contains exactly two imported runtime resources. No Editor,
		// source shader, native compiler or project source directory is mounted.
		auto bytes = Take(PakBuilder::Encode({{fontPath, font}, {shaderPath, shader}}));
		const std::filesystem::path folder(URSA_GUI_CANVAS_TEST_ROOT);
		std::filesystem::create_directories(folder);
		const auto package = folder / "Gui.pak";
		{
			std::ofstream stream(package, std::ios::binary | std::ios::trunc);
			stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			Expect(bool(stream), "Cannot write runtime GUI package");
		}
		auto archive = Take(PakArchive::Open(package));

		struct WorkingDirectory
		{
			std::filesystem::path previous = std::filesystem::current_path();

			~WorkingDirectory()
			{
				std::filesystem::current_path(previous);
			}
		} workingDirectory;

		std::filesystem::current_path(folder);
		VirtualFileSystem files;
		Done(archive->Mount(files));
		Expect(Take(files.List("ursa")).size() == 2, "Minimal runtime GUI Pak included extra resources");
		auto canvas = Take(Gui::Canvas::Create(device, files));
		Gui::Session gui;
		auto image = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Linear, {255, 0, 0, 128}));
		Paint(gui, image);
		Done(canvas->Build(gui, {192, 112}));
		Composite(device, canvas, false, true);
		Composite(device, canvas, true, false);
		Paint(gui, image, true);
		Done(canvas->Build(gui, {192, 112}));
		Composite(device, canvas, false, false);
		Done(device->WaitIdle());
		std::cout << "Runtime GUI Pak font/shader, final-output alpha/clipping, Abort and empty pass passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
