#include <Assets/AssetPreviewState.hpp>
#include <Assets/AssetPreviewTexture.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Scene/ViewportLayout.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

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

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Take(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Targets()
{
	const auto a = ResourceId::Generate();
	const auto b = ResourceId::Generate();
	const auto pathA = Take(VirtualPath::Parse("prj:A.ursamat"));
	const auto pathB = Take(VirtualPath::Parse("prj:B.ursamat"));
	auto document =
	    Take(EditorDocument::Create(ResourceKind::Material, a, pathA,
	                                [](VirtualFileSystem&, const VirtualPath&, ResourceId) { return Result<void>{}; }));
	document->SetPayload(std::make_shared<MaterialDraft>(MaterialType::Unlit));
	document->MarkDirty();
	ProjectBrowserEntry entry{pathB, b, "B", false, ResourceKind::Material, {}};
	EditorSelectionContext selection;
	selection.SelectAsset(pathB);
	auto target = ResolveAssetPreviewTarget(selection, &entry, document.get(), 1);
	Expect(target.identity.guid == b && !target.draft && document->Dirty() && document->Path() == pathA,
	       "Asset selection was replaced by the open document or changed its state");
	entry = {pathA, a, "A", false, ResourceKind::Material, {}};
	selection.SelectAsset(pathA);
	target = ResolveAssetPreviewTarget(selection, &entry, document.get(), 1);
	Expect(target.draft == document.get(), "Matching unsaved draft was not reused");
	AssetPreviewState state;
	Expect(state.Select(target) && state.NeedsRender(), "Initial target did not request rendering");
	const auto firstGeneration = state.Generation();
	Expect(state.Complete(firstGeneration, "import-one") && state.HasImage(), "Initial result was not published");
	entry.path = Take(VirtualPath::Parse("prj:Renamed.ursamat"));
	selection.SelectAsset(entry.path);
	target.path = entry.path;
	Expect(!state.Select(target) && state.HasImage(), "Renaming changed stable identity");
	target.contentVersion = 2;
	Expect(state.Select(target) && state.HasImage() && !state.Complete(firstGeneration),
	       "A stale generation replaced current contents");
	Expect(state.Fail(state.Generation(), "Failed reimport") && state.HasImage(),
	       "Same-target failure discarded the valid image");
	Expect(state.IsStale() && state.DisplayedImportVersion() == "import-one" &&
	           state.DisplayedTarget().contentVersion == 1 && state.Target().contentVersion == 2 &&
	           !state.DisplayedTarget().draft,
	       "Failed candidate relabeled the published version or retained an author draft pointer");
	const auto failedGeneration = state.Generation();
	target.pipelineVersion = 1;
	Expect(state.Select(target) && state.IsStale() && !state.Fail(failedGeneration, "Late failure"),
	       "Old pipeline failure overwrote a new request");
	Expect(state.Complete(state.Generation(), "import-two") && !state.IsStale() &&
	           state.DisplayedImportVersion() == "import-two" && state.DisplayedTarget().contentVersion == 2 &&
	           state.DisplayedTarget().pipelineVersion == 1,
	       "Recovery did not publish the content and pipeline versions together");
	const auto oldSessionGeneration = state.Generation();
	target.projectSession = ResourceId::Generate();
	Expect(state.Select(target) && !state.HasImage() && !state.Complete(oldSessionGeneration) &&
	           !state.Fail(oldSessionGeneration, "Previous project failure"),
	       "Matching asset identities crossed project session boundaries");
	entry.kind.reset();
	entry.path = Take(VirtualPath::Parse("prj:Model.FBX"));
	selection.SelectAsset(entry.path);
	const auto meshOne = ResolveAssetPreviewTarget(selection, &entry, nullptr, 2, 1, ResourceKind::Mesh);
	const auto meshTwo = ResolveAssetPreviewTarget(selection, &entry, nullptr, 2, 2, ResourceKind::Mesh);
	Expect(!meshOne.SameIdentity(meshTwo), "Different sub-assets share preview identity");
	Expect(ResolveAssetPreviewTarget(selection, &entry, nullptr, 2).kind == AssetPreviewKind::Model,
	       "Model source was not recognized");
	Expect(state.Select(meshOne) && !state.HasImage(), "Another asset retained the old image");
	selection.Clear();
	Expect(state.Select(ResolveAssetPreviewTarget(selection, &entry, document.get(), 2)) && !state.HasImage(),
	       "Clearing selection retained the document preview");
	selection.SelectAsset(entry.path);
	Expect(!ResolveAssetPreviewTarget(selection, nullptr, document.get(), 2).Supported(),
	       "Deleted asset retained a preview");
	entry.folder = true;
	Expect(!ResolveAssetPreviewTarget(selection, &entry, document.get(), 2).Supported(),
	       "Folder was treated as renderable");
}

void Clock()
{
	AssetPreviewClock clock;
	Expect(!clock.Play(), "Empty clip played");
	Take(clock.SetDuration(2));
	Expect(!clock.Playing() && clock.Time() == 0, "New clip did not start paused");
	Take(clock.Play());
	Take(clock.Advance(0, true));
	Take(clock.Advance(0.5, true));
	Expect(clock.Time() == 0.5, "Visible playback did not advance");
	Take(clock.Seek(2));
	Expect(!clock.Playing() && clock.Time() == 2, "Scrubbing the endpoint wrapped to zero");
	Expect(!clock.Seek(-1) && !clock.Seek(3) && clock.Time() == 2, "Invalid seek changed pose");
	Expect(!clock.SetSpeed(0) && !clock.SetSpeed(-1) && !clock.SetSpeed(std::numeric_limits<Float64>::infinity()),
	       "Invalid speed was accepted");
	Take(clock.SetSpeed(2));
	Take(clock.Play());
	Take(clock.Advance(0.25, true));
	Expect(clock.Time() == 0.5, "Loop did not preserve overshoot and playback speed");
	Take(clock.Advance(100, false));
	Take(clock.Advance(100, true));
	Expect(clock.Time() == 0.5, "Restoring visibility caught up hidden time");
	clock.SetLoop(false);
	Take(clock.Advance(1, true));
	Expect(clock.Time() == 2 && !clock.Playing(), "Non-looping clip did not stop at its endpoint");
	Expect(!clock.Advance(std::numeric_limits<Float64>::quiet_NaN(), true), "Invalid delta was accepted");
	Take(clock.SetDuration(0));
	Expect(!clock.Play() && clock.Time() == 0, "Zero duration did not retain static state");
}

void TextureViews()
{
	const Vector<UInt8> original{128, 64, 32, 96, 0, 127, 255, 255};
	const auto texture =
	    Take(MaterialTexture::Create(2, 1, TextureColorSpace::Srgb, original, {{1, 1, {20, 40, 60, 80}}}));
	const auto rgb = Take(CreateTexturePreviewView(texture, 0, TexturePreviewChannel::Rgb));
	Expect(rgb->GetPixels()[0] == 128 && rgb->GetPixels()[3] == 255 && rgb->GetColorSpace() == TextureColorSpace::Srgb,
	       "RGB display changed color values or decoding space");
	const auto alpha = Take(CreateTexturePreviewView(texture, 0, TexturePreviewChannel::Alpha));
	Expect(alpha->GetPixels()[0] == 96 && alpha->GetPixels()[1] == 96 && alpha->GetPixels()[3] == 255 &&
	           alpha->GetColorSpace() == TextureColorSpace::Linear,
	       "Alpha data was gamma-decoded or blended");
	const auto red = Take(CreateTexturePreviewView(texture, 1, TexturePreviewChannel::Red));
	Expect(red->GetWidth() == 1 && red->GetPixels() == Vector<UInt8>({20, 20, 20, 255}),
	       "Mip/channel selection did not use actual mip contents");
	Expect(!CreateTexturePreviewView(texture, 2, TexturePreviewChannel::Rgba) && texture->GetPixels() == original,
	       "Invalid mip or preview conversion mutated source pixels");
	for (const auto selected : {TexturePreviewChannel::Red, TexturePreviewChannel::Green, TexturePreviewChannel::Blue})
	{
		const auto view = Take(CreateTexturePreviewView(texture, 0, selected));
		const auto expected = original[Size(selected) - Size(TexturePreviewChannel::Red)];
		Expect(view->GetPixels()[0] == expected && view->GetPixels()[1] == expected &&
		           view->GetPixels()[2] == expected && view->GetColorSpace() == TextureColorSpace::Srgb,
		       "Single color channel changed data or decoding space");
	}
	Expect(Take(CreateTexturePreviewView(texture, 0, TexturePreviewChannel::Rgba))->GetPixels() == original,
	       "RGBA view changed alpha");
	for (const auto dpi : {1.f, 1.5f, 2.f})
	{
		const auto layout = ComputeViewportLayout({10, 20, 300, 300}, dpi, {400, 200}, 1, {}, false);
		Expect(layout.visible && std::abs(layout.displayRect.width * dpi - 400) < .001f &&
		           std::abs(layout.displayRect.height * dpi - 200) < .001f,
		       "100% did not preserve one texel per framebuffer pixel");
		const auto fit = ComputeViewportLayout({10, 20, 300, 300}, dpi, {400, 200}, 1, {}, true);
		Expect(std::abs(fit.displayRect.width / fit.displayRect.height - 2) < .001f,
		       "Fit changed texture aspect ratio");
		Expect(!ScreenToViewportPixel({0, 0}, layout), "Texture input escaped the clipped image region");
	}
}
} // namespace

int main()
{
	try
	{
		Targets();
		Clock();
		TextureViews();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
