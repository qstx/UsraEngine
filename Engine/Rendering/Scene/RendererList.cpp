#include <Rendering/Scene/RendererList.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Profiler.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

namespace Ursa::Rendering
{
namespace
{
constexpr Size maxFrameResults = 1024;
std::atomic<UInt32> nextContextIndex{1};

bool Visible(const Bounds& bounds, const Mat4& projection)
{
	if (bounds.empty)
	{
		return true;
	}
	// Vulkan's depth interval is [0,w], while X and Y use [-w,w].
	for (UInt32 plane = 0; plane < 6; ++plane)
	{
		Array<Float32, 4> coefficients{};
		for (Int32 column = 0; column < 4; ++column)
		{
			coefficients[column] =
			    plane == 4 ? projection.At(2, column)
			               : projection.At(3, column) + (plane % 2 == 0 ? 1.0f : -1.0f) *
			                                                projection.At(plane == 5 ? 2 : Int32(plane / 2), column);
		}
		const Float32 distance = coefficients[0] * (coefficients[0] >= 0 ? bounds.maximum.x : bounds.minimum.x) +
		                         coefficients[1] * (coefficients[1] >= 0 ? bounds.maximum.y : bounds.minimum.y) +
		                         coefficients[2] * (coefficients[2] >= 0 ? bounds.maximum.z : bounds.minimum.z) +
		                         coefficients[3];
		if (distance < -0.00001f)
		{
			return false;
		}
	}
	return true;
}

Int32 Queue(const RenderObject& object)
{
	if (object.shader)
	{
		for (const auto& subShader : object.shader->subShaders)
		{
			for (const auto& tag : subShader.tags)
			{
				if (tag.name != "Queue")
				{
					continue;
				}
				const char* begin = tag.value.c_str();
				Int32 queue = 2000;
				const auto offset = tag.value.find_first_of("+-");
				const auto category = [&](const char* name) { return tag.value.compare(0, offset, name) == 0; };
				if (category("Background"))
				{
					queue = 1000;
				}
				else if (category("AlphaTest"))
				{
					queue = 2450;
				}
				else if (category("Transparent"))
				{
					queue = 3000;
				}
				else if (category("Overlay"))
				{
					queue = 4000;
				}
				else if (!category("Geometry"))
				{
					char* end = nullptr;
					const long number = std::strtol(begin, &end, 10);
					if (end != begin && *end == '\0' && number >= 0 && number <= 5000)
					{
						return Int32(number);
					}
				}
				if (offset != String::npos)
				{
					char* end = nullptr;
					const long adjustment = std::strtol(begin + offset, &end, 10);
					if (end == begin + offset || *end != '\0' || adjustment < -5000 || adjustment > 5000)
					{
						return queue;
					}
					queue += Int32(adjustment);
				}
				return std::clamp(queue, 0, 5000);
			}
		}
	}
	return object.parameters.alphaMode == AlphaMode::Blend  ? 3000
	       : object.parameters.alphaMode == AlphaMode::Mask ? 2450
	                                                        : 2000;
}

String PassSelectionKey(const Shader::ShaderAsset& shader, const DrawingSettings& drawing)
{
	String key = "UrsaRendererListPassV1|" + shader.id.ToString() + "|" + shader.sourceHash + "|" +
	             shader.compilerVersion + "|" + drawing.activePipeline + "|" +
	             ToUrsaString(std::to_string(drawing.overrideMaterialPass));
	for (const ShaderTagId& tag : drawing.shaderTags)
	{
		key += "|tag=" + tag.name;
	}
	for (const auto& subShader : shader.subShaders)
	{
		for (const auto& tag : subShader.tags)
		{
			key += "|sub=" + tag.name + "=" + tag.value;
		}
		for (const auto& pass : subShader.passes)
		{
			key += "|pass=" + pass.identity + ":" + pass.name;
			for (const auto& tag : pass.tags)
			{
				key += ";" + tag.name + "=" + tag.value;
			}
		}
	}
	Vector<const Shader::ShaderAsset*> references{&shader};
	Set<const Shader::ShaderAsset*> visited;
	while (!references.empty())
	{
		const auto* current = references.back();
		references.pop_back();
		if (!visited.insert(current).second)
		{
			continue;
		}
		for (const auto& reference : current->resolvedReferences)
		{
			key += "|reference=" + reference.reference.shaderName + "/" + reference.reference.passName;
			if (reference.asset)
			{
				key += "=" + reference.asset->id.ToString() + ":" + reference.asset->sourceHash + ":" +
				       reference.asset->compilerVersion;
				references.push_back(reference.asset.get());
			}
		}
	}
	return key;
}

Result<bool> Pass(const std::shared_ptr<const Shader::ShaderAsset>& shaderAsset, const DrawingSettings& drawing,
                  UInt32& subShaderIndex, UInt32& passIndex, std::optional<ResolvedGraphicsPass>& resolved,
                  Map<String, ShaderPassSelectionCacheEntry>& cache)
{
	if (!shaderAsset)
	{
		return Result<bool>::Success(true);
	}
	const String key = PassSelectionKey(*shaderAsset, drawing);
	if (const auto found = cache.find(key); found != cache.end())
	{
		subShaderIndex = found->second.subShaderIndex;
		passIndex = found->second.passIndex;
		resolved = found->second.resolvedPass;
		return Result<bool>::Success(found->second.matched);
	}
	Vector<String> tags;
	for (const auto& tag : drawing.shaderTags)
	{
		tags.push_back(tag.name);
	}
	auto selected = SelectGraphicsPass(shaderAsset, drawing.activePipeline, tags, drawing.overrideMaterialPass);
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	static std::atomic<UInt64> passResolutions{0};
	Profiling::Plot("Rendering.PassResolutions", Float64(passResolutions.fetch_add(1, std::memory_order_relaxed) + 1));
#endif
	if (!selected)
	{
		return Result<bool>::Failure(selected.error);
	}
	ShaderPassSelectionCacheEntry selection;
	selection.matched = selected.value.has_value();
	selection.resolvedPass = selected.value;
	if (selected.value)
	{
		selection.subShaderIndex = selected.value->subShaderIndex;
		selection.passIndex = selected.value->passIndex;
	}
	if (cache.size() >= 256)
	{
		cache.erase(cache.begin());
	}
	cache.emplace(key, selection);
	subShaderIndex = selection.subShaderIndex;
	passIndex = selection.passIndex;
	resolved = selected.value;
	return Result<bool>::Success(selection.matched);
}
} // namespace

RenderSceneContext::RenderSceneContext() : ownerThread(std::this_thread::get_id())
{
	context.index = nextContextIndex.fetch_add(1);
}

Result<RenderContextToken> RenderSceneContext::Begin(UInt64 sessionId, UInt64 frameId, const RenderScene& renderScene)
{
	if (std::this_thread::get_id() != ownerThread || active || sessionId == 0 || context.index == 0 ||
	    !renderScene.IsValid() || context.generation == 0xffffffffu)
	{
		return Result<RenderContextToken>::Failure(
		    "Render context begin requires its owner thread and a closed valid session");
	}
	context.sessionId = sessionId;
	context.frameId = frameId;
	++context.generation;
	scene = &renderScene;
	sceneVersion = scene->Version();
	active = true;
	visibilityCount = 0;
	listCount = 0;
	return Result<RenderContextToken>::Success(context);
}

Result<void> RenderSceneContext::Validate(RenderContextToken token) const
{
	if (std::this_thread::get_id() != ownerThread || !active || !scene || scene->Version() != sceneVersion ||
	    token.sessionId != context.sessionId || token.frameId != context.frameId || token.index != context.index ||
	    token.generation != context.generation)
	{
		return Result<void>::Failure("Render result belongs to a closed, changed or different frame/session/thread");
	}
	return {};
}

Result<void> RenderSceneContext::End()
{
	if (std::this_thread::get_id() != ownerThread || !active)
	{
		return Result<void>::Failure("Render context end requires its active owner thread");
	}
	auto valid = Validate(context);
	for (Size index = 0; index < listCount; ++index)
	{
		lists[index]->entries.clear();
	}
	active = false;
	scene = nullptr;
	return valid;
}

Result<CullingResults> RenderSceneContext::Cull(const CameraData& camera)
{
	auto valid = Validate(context);
	if (!valid)
	{
		return Result<CullingResults>::Failure(valid.error);
	}
	valid = camera.settings.Validate();
	if (!valid)
	{
		return Result<CullingResults>::Failure(valid.error);
	}
	for (Float32 value : camera.viewProjection.values)
	{
		if (!std::isfinite(value))
		{
			return Result<CullingResults>::Failure("Camera culling matrix must be finite");
		}
	}
	if (visibilityCount == maxFrameResults)
	{
		return Result<CullingResults>::Failure("Frame culling result budget exceeded");
	}
	if (visibilityCount == visibility.size())
	{
		visibility.push_back(MakeUnique<Visibility>(Memory::MemoryTag::Renderer));
	}
	auto& output = *visibility[visibilityCount];
	output.indices.clear();
	output.indices.reserve(scene->Objects().size());
	if (++output.generation == 0)
	{
		return Result<CullingResults>::Failure("Culling result generation exhausted");
	}
	for (Size index = 0; index < scene->Objects().size(); ++index)
	{
		const auto& object = scene->Objects()[index];
		if ((camera.settings.cullingMask & (1u << object.layer)) && Visible(object.worldBounds, camera.viewProjection))
		{
			output.indices.push_back(UInt32(index));
		}
	}
	return Result<CullingResults>::Success({context, UInt32(visibilityCount++), output.generation});
}

Result<const Vector<UInt32>*> RenderSceneContext::VisibleObjects(CullingResults results) const
{
	auto valid = Validate(results.context);
	if (!valid || results.index >= visibilityCount || visibility[results.index]->generation != results.generation)
	{
		return Result<const Vector<UInt32>*>::Failure(valid ? "Invalid culling result generation" : valid.error);
	}
	return Result<const Vector<UInt32>*>::Success(&visibility[results.index]->indices);
}

Result<RendererList> RenderSceneContext::CreateRendererList(CullingResults results, const DrawingSettings& drawing,
                                                            const FilteringSettings& filtering)
{
	auto input = VisibleObjects(results);
	if (!input)
	{
		return Result<RendererList>::Failure(input.error);
	}
	if (filtering.minimumQueue < 0 || filtering.maximumQueue > 5000 ||
	    filtering.minimumQueue > filtering.maximumQueue || drawing.overrideMaterialPass < -1 ||
	    (drawing.overrideMaterial && !drawing.overrideMaterial->IsValid()) ||
	    static_cast<UInt32>(drawing.sorting.criteria) > static_cast<UInt32>(SortingCriteria::CommonTransparent) ||
	    !std::isfinite(drawing.sorting.cameraPosition.x) || !std::isfinite(drawing.sorting.cameraPosition.y) ||
	    !std::isfinite(drawing.sorting.cameraPosition.z))
	{
		return Result<RendererList>::Failure("RendererList has invalid filtering, material or sorting settings");
	}
	if (listCount == maxFrameResults)
	{
		return Result<RendererList>::Failure("Frame renderer list budget exceeded");
	}
	if (listCount == lists.size())
	{
		lists.push_back(MakeUnique<List>(Memory::MemoryTag::Renderer));
	}
	auto& output = *lists[listCount];
	output.entries.clear();
	output.entries.reserve(input.value->size());
	if (++output.generation == 0)
	{
		return Result<RendererList>::Failure("RendererList generation exhausted");
	}
	for (UInt32 index : *input.value)
	{
		const auto& object = scene->Objects()[index];
		const Int32 queue = Queue(object);
		if (!(filtering.layerMask & (1u << object.layer)) || queue < filtering.minimumQueue ||
		    queue > filtering.maximumQueue)
		{
			continue;
		}
		auto material = drawing.overrideMaterial ? drawing.overrideMaterial : object.material;
		UInt32 subShaderIndex = 0, passIndex = 0;
		std::optional<ResolvedGraphicsPass> resolvedPass;
		auto matching = Pass(drawing.overrideMaterial ? drawing.overrideMaterial->Shader() : object.shader, drawing,
		                     subShaderIndex, passIndex, resolvedPass, passSelectionCache);
		if (!matching)
		{
			return Result<RendererList>::Failure(matching.error);
		}
		if (!matching.value)
		{
			continue;
		}
		const auto& bounds = object.worldBounds;
		const Vec3 center = bounds.empty ? Vec3{object.world.At(0, 3), object.world.At(1, 3), object.world.At(2, 3)}
		                                 : Vec3{(bounds.minimum.x + bounds.maximum.x) * 0.5f,
		                                        (bounds.minimum.y + bounds.maximum.y) * 0.5f,
		                                        (bounds.minimum.z + bounds.maximum.z) * 0.5f};
		const Vec3 delta = center - drawing.sorting.cameraPosition;
		output.entries.push_back({index, std::move(material), subShaderIndex, passIndex, queue, Dot(delta, delta),
		                          drawing.activePipeline, resolvedPass});
	}
	if (drawing.sorting.criteria != SortingCriteria::None)
	{
		std::sort(output.entries.begin(), output.entries.end(),
		          [&](const RendererListEntry& a, const RendererListEntry& b)
		          {
			          if (a.renderQueue != b.renderQueue)
			          {
				          return a.renderQueue < b.renderQueue;
			          }
			          if (a.distanceSquared != b.distanceSquared)
			          {
				          return drawing.sorting.criteria == SortingCriteria::CommonTransparent
				                     ? a.distanceSquared > b.distanceSquared
				                     : a.distanceSquared < b.distanceSquared;
			          }
			          return a.objectIndex < b.objectIndex;
		          });
	}
	return Result<RendererList>::Success({context, UInt32(listCount++), output.generation});
}

Result<const Vector<RendererListEntry>*> RenderSceneContext::Entries(RendererList list) const
{
	auto valid = Validate(list.context);
	if (!valid || list.index >= listCount || lists[list.index]->generation != list.generation)
	{
		return Result<const Vector<RendererListEntry>*>::Failure(valid ? "Invalid renderer list generation"
		                                                               : valid.error);
	}
	return Result<const Vector<RendererListEntry>*>::Success(&lists[list.index]->entries);
}
} // namespace Ursa::Rendering
