#include <Ui/EditorIcons.hpp>
#include <Scene/SceneViewPanel.hpp>
#include <Scene/SceneViewNavigation.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Workspace/EditorSelectionContext.hpp>

#include <Scene/EditorScenePreview.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Math/Vec4.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string>

namespace Ursa::Editor
{
namespace
{
ImVec2 CurrentViewportFramebufferScale()
{
	const ImVec2 viewportScale = ImGui::GetWindowViewport()->FramebufferScale;
	const ImVec2 fallback = ImGui::GetIO().DisplayFramebufferScale;
	return ImVec2(viewportScale.x > 0.f ? viewportScale.x : std::max(fallback.x, 1.f),
	              viewportScale.y > 0.f ? viewportScale.y : std::max(fallback.y, 1.f));
}

Vector<UInt8> ToBytes(const String& text)
{
	Vector<UInt8> bytes;
	bytes.reserve(text.size());
	for (unsigned char value : text)
	{
		bytes.push_back(value);
	}
	return bytes;
}

bool HasEffectiveGameCamera(const Scene& scene)
{
	const ComponentId cameraId = scene.ActiveCamera();
	if (!cameraId || !dynamic_cast<const CameraComponent*>(scene.GetComponent(cameraId)))
	{
		return false;
	}
	auto active = scene.IsComponentActive(cameraId);
	return active && active.value;
}

Result<void> ClearViewportColor(const Rhi::DevicePtr& device,
                                const std::shared_ptr<Rendering::RenderTextureResource>& target, Color color)
{
	if (!device || !target)
	{
		return Result<void>::Failure("Cannot clear an unavailable viewport target");
	}
	auto executor = Rendering::RenderCommandExecutor::Create(device);
	if (!executor)
	{
		return Result<void>::Failure(executor.error);
	}
	auto begun = executor.value->Begin(target, Rhi::ResourceState::ShaderRead);
	if (!begun)
	{
		return begun;
	}
	Rendering::RenderCommand clear;
	clear.opcode = Rendering::RenderCommandOpcode::ClearTarget;
	clear.clearFlags = 1;
	clear.color = color;
	auto executed = executor.value->Execute({clear});
	if (!executed)
	{
		return executed;
	}
	auto submitted = executor.value->Submit();
	if (!submitted)
	{
		return Result<void>::Failure(submitted.error);
	}
	auto ended = executor.value->End();
	return ended ? Result<void>{} : Result<void>::Failure(ended.error);
}

bool ManagedRenderServicesUnavailable(const String& error)
{
	return error == "Editing render services are unavailable" ||
	       error == "Project pipeline viewports require the .NET rendering host";
}

Vec4 Transform(const Mat4& matrix, Vec4 vector)
{
	const Float32 input[] = {vector.x, vector.y, vector.z, vector.w};
	Float32 output[4]{};
	for (Int32 row = 0; row < 4; ++row)
	{
		for (Int32 column = 0; column < 4; ++column)
		{
			output[row] += matrix.At(row, column) * input[column];
		}
	}
	return {output[0], output[1], output[2], output[3]};
}

struct SceneViewRay
{
	Vec3 origin;
	Vec3 direction;
};

Result<SceneViewRay> CreateSceneViewRay(const Camera& camera, Float32 aspect, const ViewportLayout& layout,
                                        ViewportPoint screenPosition)
{
	const auto pixel = ScreenToViewportPixel(screenPosition, layout);
	if (!pixel || !layout.targetPixels.width || !layout.targetPixels.height || !std::isfinite(aspect) || aspect <= 0.f)
	{
		return Result<SceneViewRay>::Failure("The pointer is outside the Scene image viewport");
	}
	try
	{
		const Float32 x = 2.f * pixel->x / static_cast<Float32>(layout.targetPixels.width) - 1.f;
		const Float32 y = 1.f - 2.f * pixel->y / static_cast<Float32>(layout.targetPixels.height);
		const Mat4 inverse = camera.ViewProjection(aspect).Inverse();
		auto unproject = [&](Float32 depth) -> Result<Vec3>
		{
			const Vec4 homogeneous = Transform(inverse, {x, y, depth, 1.f});
			if (!std::isfinite(homogeneous.w) || std::abs(homogeneous.w) < 1.0e-7f)
			{
				return Result<Vec3>::Failure("The Scene camera ray has an invalid homogeneous coordinate");
			}
			return Result<Vec3>::Success(
			    {homogeneous.x / homogeneous.w, homogeneous.y / homogeneous.w, homogeneous.z / homogeneous.w});
		};
		auto nearPoint = unproject(0.f);
		auto farPoint = unproject(1.f);
		if (!nearPoint || !farPoint)
		{
			return Result<SceneViewRay>::Failure(!nearPoint ? nearPoint.error : farPoint.error);
		}
		const Vec3 direction = Normalized(farPoint.value - nearPoint.value);
		if (Dot(direction, direction) < 0.99f)
		{
			return Result<SceneViewRay>::Failure("The Scene camera ray is degenerate");
		}
		return Result<SceneViewRay>::Success({nearPoint.value, direction});
	}
	catch (const std::exception& error)
	{
		return Result<SceneViewRay>::Failure(ToUrsaString(error.what()));
	}
}

bool IntersectBounds(Vec3 origin, Vec3 direction, const Bounds& bounds, Float32& distance)
{
	if (bounds.empty)
	{
		return false;
	}
	Float32 minimum = 0.f;
	Float32 maximum = std::numeric_limits<Float32>::max();
	const Float32 origins[] = {origin.x, origin.y, origin.z};
	const Float32 directions[] = {direction.x, direction.y, direction.z};
	const Float32 lower[] = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
	const Float32 upper[] = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
	for (Size axis = 0; axis < 3; ++axis)
	{
		if (std::abs(directions[axis]) < 1.0e-7f)
		{
			if (origins[axis] < lower[axis] || origins[axis] > upper[axis])
			{
				return false;
			}
			continue;
		}
		Float32 first = (lower[axis] - origins[axis]) / directions[axis];
		Float32 second = (upper[axis] - origins[axis]) / directions[axis];
		if (first > second)
		{
			std::swap(first, second);
		}
		minimum = std::max(minimum, first);
		maximum = std::min(maximum, second);
		if (minimum > maximum)
		{
			return false;
		}
	}
	distance = minimum;
	return maximum >= 0.f && std::isfinite(distance);
}

bool IntersectTriangle(Vec3 origin, Vec3 direction, Vec3 first, Vec3 second, Vec3 third, Float32& distance)
{
	const Vec3 edgeOne = second - first;
	const Vec3 edgeTwo = third - first;
	const Vec3 p = Cross(direction, edgeTwo);
	const Float32 determinant = Dot(edgeOne, p);
	if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-7f)
	{
		return false;
	}
	const Float32 inverse = 1.f / determinant;
	const Vec3 offset = origin - first;
	const Float32 u = Dot(offset, p) * inverse;
	if (u < 0.f || u > 1.f)
	{
		return false;
	}
	const Vec3 q = Cross(offset, edgeOne);
	const Float32 v = Dot(direction, q) * inverse;
	if (v < 0.f || u + v > 1.f)
	{
		return false;
	}
	distance = Dot(edgeTwo, q) * inverse;
	return std::isfinite(distance) && distance >= 0.f;
}

void FilterSceneViewRenderObjects(Vector<Rendering::RenderObject>& objects, const EditorHierarchy& hierarchy,
                                  bool requirePickable)
{
	objects.erase(std::remove_if(objects.begin(), objects.end(),
	                             [&](const Rendering::RenderObject& object)
	                             {
		                             return !hierarchy.SceneObjectVisible(object.objectId) ||
		                                    (requirePickable && !hierarchy.SceneObjectPickable(object.objectId));
	                             }),
	              objects.end());
}

Vec3 TransformSceneVertex(const Mat4& world, Vec3 position)
{
	const Vec4 transformed = Transform(world, {position.x, position.y, position.z, 1.f});
	if (!std::isfinite(transformed.w) || std::abs(transformed.w) < 1.0e-7f)
	{
		return {std::numeric_limits<Float32>::quiet_NaN(), 0.f, 0.f};
	}
	return {transformed.x / transformed.w, transformed.y / transformed.w, transformed.z / transformed.w};
}

Result<ResourceId> PickSceneObject(const Camera& camera, Float32 aspect, const ViewportLayout& layout,
                                   ViewportPoint screenPosition, const Vector<Rendering::RenderObject>& renderObjects,
                                   Scene& scene, const EditorHierarchy& hierarchy,
                                   Vector<ResourceId>* overlapping = nullptr, Vec3* pickedPosition = nullptr)
{
	auto ray = CreateSceneViewRay(camera, aspect, layout, screenPosition);
	if (!ray)
	{
		return Result<ResourceId>::Failure(ray.error);
	}
	Vector<std::pair<Float32, ResourceId>> candidates;
	for (const auto& object : renderObjects)
	{
		Float32 boundsDistance = 0.f;
		const bool boundsHit =
		    IntersectBounds(ray.value.origin, ray.value.direction, object.worldBounds, boundsDistance);
		Float32 objectDistance = std::numeric_limits<Float32>::max();
		if (object.meshSnapshot && object.jointPalette.empty() &&
		    object.subMesh < object.meshSnapshot->data.subMeshes.size())
		{
			const auto& data = object.meshSnapshot->data;
			const auto& subMesh = data.subMeshes[object.subMesh];
			const Size first = subMesh.firstIndex;
			if (first >= data.indices.size())
			{
				continue;
			}
			const Size end = first + std::min<Size>(data.indices.size() - first, subMesh.indexCount);
			for (Size index = first; index + 2 < end; index += 3)
			{
				const Int64 firstIndex = static_cast<Int64>(data.indices[index]) + subMesh.baseVertex;
				const Int64 secondIndex = static_cast<Int64>(data.indices[index + 1]) + subMesh.baseVertex;
				const Int64 thirdIndex = static_cast<Int64>(data.indices[index + 2]) + subMesh.baseVertex;
				if (firstIndex < 0 || secondIndex < 0 || thirdIndex < 0 ||
				    firstIndex >= static_cast<Int64>(data.positions.size()) ||
				    secondIndex >= static_cast<Int64>(data.positions.size()) ||
				    thirdIndex >= static_cast<Int64>(data.positions.size()))
				{
					continue;
				}
				const Vec3 firstVertex =
				    TransformSceneVertex(object.world, data.positions[static_cast<Size>(firstIndex)]);
				const Vec3 secondVertex =
				    TransformSceneVertex(object.world, data.positions[static_cast<Size>(secondIndex)]);
				const Vec3 thirdVertex =
				    TransformSceneVertex(object.world, data.positions[static_cast<Size>(thirdIndex)]);
				Float32 triangleDistance = 0.f;
				if (IntersectTriangle(ray.value.origin, ray.value.direction, firstVertex, secondVertex, thirdVertex,
				                      triangleDistance) &&
				    triangleDistance < objectDistance)
				{
					objectDistance = triangleDistance;
				}
			}
		}
		else if (boundsHit)
		{
			objectDistance = boundsDistance;
		}
		if (object.objectId && std::isfinite(objectDistance))
		{
			const auto existing = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate)
			                                   { return candidate.second == object.objectId; });
			if (existing == candidates.end())
			{
				candidates.emplace_back(objectDistance, object.objectId);
			}
			else
			{
				existing->first = std::min(existing->first, objectDistance);
			}
		}
	}
	std::function<void(const GameObject::Ptr&)> considerTransform = [&](const GameObject::Ptr& object)
	{
		if (!object)
		{
			return;
		}
		auto active = scene.ActiveInHierarchy(object);
		if (!active || !active.value)
		{
			return;
		}
		auto id = scene.PersistentId(object);
		const bool hasRenderedBounds =
		    id && std::any_of(renderObjects.begin(), renderObjects.end(), [&](const auto& rendered)
		                      { return rendered.objectId == id.value && !rendered.worldBounds.empty; });
		if (id && hierarchy.SceneObjectVisible(id.value) && hierarchy.SceneObjectPickable(id.value) &&
		    !hasRenderedBounds)
		{
			auto world = scene.WorldTransform(object);
			if (world)
			{
				const Vec3 position{world.value.At(0, 3), world.value.At(1, 3), world.value.At(2, 3)};
				const Vec4 clip = Transform(camera.ViewProjection(aspect), {position.x, position.y, position.z, 1.f});
				if (std::isfinite(clip.w) && clip.w > 1.0e-7f && clip.z >= 0.f && clip.z <= clip.w)
				{
					const Float32 x = layout.displayRect.x + (clip.x / clip.w + 1.f) * layout.displayRect.width * 0.5f;
					const Float32 y = layout.displayRect.y + (1.f - clip.y / clip.w) * layout.displayRect.height * 0.5f;
					const Float32 deltaX = x - screenPosition.x;
					const Float32 deltaY = y - screenPosition.y;
					const Float32 distanceSquared = deltaX * deltaX + deltaY * deltaY;
					const Float32 rayDistance = Dot(position - ray.value.origin, ray.value.direction);
					if (distanceSquared <= 8.f * 8.f && rayDistance >= 0.f)
					{
						const auto existing =
						    std::find_if(candidates.begin(), candidates.end(),
						                 [&](const auto& candidate) { return candidate.second == id.value; });
						if (existing == candidates.end())
						{
							candidates.emplace_back(rayDistance, id.value);
						}
						else
						{
							existing->first = std::min(existing->first, rayDistance);
						}
					}
				}
			}
		}
		for (const auto& child : object->Children())
		{
			considerTransform(child);
		}
	};
	for (const auto& root : scene.RootGameObjects())
	{
		considerTransform(root);
	}
	std::stable_sort(candidates.begin(), candidates.end(),
	                 [](const auto& first, const auto& second) { return first.first < second.first; });
	if (overlapping)
	{
		overlapping->clear();
		overlapping->reserve(candidates.size());
		for (const auto& candidate : candidates)
		{
			overlapping->push_back(candidate.second);
		}
	}
	if (!candidates.empty())
	{
		if (pickedPosition)
		{
			const Float32 distance = candidates.front().first;
			*pickedPosition = {ray.value.origin.x + ray.value.direction.x * distance,
			                   ray.value.origin.y + ray.value.direction.y * distance,
			                   ray.value.origin.z + ray.value.direction.z * distance};
		}
		return Result<ResourceId>::Success(candidates.front().second);
	}
	return Result<ResourceId>::Failure("No scene object is under the pointer");
}

bool ProjectObjectOrigin(const Camera& camera, Float32 aspect, const ViewportLayout& layout, const Scene& scene,
                         const GameObject::Ptr& object, ViewportPoint& screen)
{
	if (!object || !std::isfinite(aspect) || aspect <= 0.f)
	{
		return false;
	}
	auto world = scene.WorldTransform(object);
	if (!world)
	{
		return false;
	}
	const Vec4 clip = Transform(camera.ViewProjection(aspect),
	                            {world.value.At(0, 3), world.value.At(1, 3), world.value.At(2, 3), 1.f});
	if (!std::isfinite(clip.w) || clip.w <= 1.0e-7f || clip.z < 0.f || clip.z > clip.w)
	{
		return false;
	}
	screen = {layout.displayRect.x + (clip.x / clip.w + 1.f) * layout.displayRect.width * 0.5f,
	          layout.displayRect.y + (1.f - clip.y / clip.w) * layout.displayRect.height * 0.5f};
	return screen.x >= layout.displayRect.x && screen.x <= layout.displayRect.x + layout.displayRect.width &&
	       screen.y >= layout.displayRect.y && screen.y <= layout.displayRect.y + layout.displayRect.height;
}

bool SceneGizmoOccluded(Vec3 cameraPosition, Vec3 objectPosition, ResourceId objectId,
                        const Vector<Rendering::RenderObject>& occluders)
{
	const Float32 directionX = objectPosition.x - cameraPosition.x;
	const Float32 directionY = objectPosition.y - cameraPosition.y;
	const Float32 directionZ = objectPosition.z - cameraPosition.z;
	const Float32 distance = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
	const Float32 maximumDistance = distance - 0.03f;
	if (maximumDistance <= 0.f)
	{
		return false;
	}
	const Float32 direction[3]{directionX / distance, directionY / distance, directionZ / distance};
	const Float32 origin[3]{cameraPosition.x, cameraPosition.y, cameraPosition.z};
	for (const Rendering::RenderObject& candidate : occluders)
	{
		if (candidate.objectId == objectId || candidate.worldBounds.empty)
		{
			continue;
		}
		const Float32 minimum[3]{candidate.worldBounds.minimum.x, candidate.worldBounds.minimum.y,
		                         candidate.worldBounds.minimum.z};
		const Float32 maximum[3]{candidate.worldBounds.maximum.x, candidate.worldBounds.maximum.y,
		                         candidate.worldBounds.maximum.z};
		Float32 nearDistance = 0.f;
		Float32 farDistance = maximumDistance;
		bool intersects = true;
		for (Size axis = 0; axis < 3; ++axis)
		{
			if (std::abs(direction[axis]) < 1.0e-7f)
			{
				if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis])
				{
					intersects = false;
					break;
				}
				continue;
			}
			Float32 first = (minimum[axis] - origin[axis]) / direction[axis];
			Float32 last = (maximum[axis] - origin[axis]) / direction[axis];
			if (first > last)
			{
				std::swap(first, last);
			}
			nearDistance = std::max(nearDistance, first);
			farDistance = std::min(farDistance, last);
			if (nearDistance > farDistance)
			{
				intersects = false;
				break;
			}
		}
		if (intersects && farDistance > 0.f && nearDistance < maximumDistance)
		{
			return true;
		}
	}
	return false;
}

bool ProjectSelectionBounds(const Mat4& viewProjection, const ViewportLayout& layout, const Bounds& bounds,
                            ViewportRect& projected)
{
	if (bounds.empty)
	{
		return false;
	}
	Float32 minimumX = std::numeric_limits<Float32>::max();
	Float32 minimumY = std::numeric_limits<Float32>::max();
	Float32 maximumX = std::numeric_limits<Float32>::lowest();
	Float32 maximumY = std::numeric_limits<Float32>::lowest();
	bool hasVisibleCorner = false;
	for (UInt32 corner = 0; corner < 8; ++corner)
	{
		const Vec4 clip = Transform(viewProjection, {(corner & 1) ? bounds.maximum.x : bounds.minimum.x,
		                                             (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
		                                             (corner & 4) ? bounds.maximum.z : bounds.minimum.z, 1.f});
		if (!std::isfinite(clip.w) || clip.w <= 1.0e-7f || clip.z < 0.f || clip.z > clip.w)
		{
			continue;
		}
		const Float32 x = layout.displayRect.x + (clip.x / clip.w + 1.f) * layout.displayRect.width * 0.5f;
		const Float32 y = layout.displayRect.y + (1.f - clip.y / clip.w) * layout.displayRect.height * 0.5f;
		minimumX = std::min(minimumX, x);
		minimumY = std::min(minimumY, y);
		maximumX = std::max(maximumX, x);
		maximumY = std::max(maximumY, y);
		hasVisibleCorner = true;
	}
	if (!hasVisibleCorner)
	{
		return false;
	}
	projected = {minimumX, minimumY, maximumX - minimumX, maximumY - minimumY};
	return projected.x <= layout.displayRect.x + layout.displayRect.width &&
	       projected.y <= layout.displayRect.y + layout.displayRect.height &&
	       projected.x + projected.width >= layout.displayRect.x &&
	       projected.y + projected.height >= layout.displayRect.y;
}

bool ProjectSelectionBounds(const Camera& camera, Float32 aspect, const ViewportLayout& layout, const Bounds& bounds,
                            ViewportRect& projected)
{
	return std::isfinite(aspect) && aspect > 0.f &&
	       ProjectSelectionBounds(camera.ViewProjection(aspect), layout, bounds, projected);
}

void DrawProjectedBounds(ImDrawList* drawList, const Mat4& viewProjection, const ViewportLayout& layout,
                         const Bounds& bounds, ImU32 color)
{
	if (!drawList || bounds.empty)
	{
		return;
	}
	std::array<ImVec2, 8> projected{};
	std::array<bool, 8> visible{};
	for (UInt32 corner = 0; corner < 8; ++corner)
	{
		const Vec4 clip = Transform(viewProjection, {(corner & 1) ? bounds.maximum.x : bounds.minimum.x,
		                                             (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
		                                             (corner & 4) ? bounds.maximum.z : bounds.minimum.z, 1.f});
		if (!std::isfinite(clip.w) || clip.w <= 1.0e-7f || clip.z < 0.f || clip.z > clip.w)
		{
			continue;
		}
		projected[corner] = {layout.displayRect.x + (clip.x / clip.w + 1.f) * layout.displayRect.width * 0.5f,
		                     layout.displayRect.y + (1.f - clip.y / clip.w) * layout.displayRect.height * 0.5f};
		visible[corner] = true;
	}
	for (UInt32 corner = 0; corner < 8; ++corner)
	{
		for (UInt32 axis = 0; axis < 3; ++axis)
		{
			if ((corner & (1u << axis)) || !visible[corner] || !visible[corner | (1u << axis)])
			{
				continue;
			}
			drawList->AddLine(projected[corner], projected[corner | (1u << axis)], color, 1.5f);
		}
	}
}

bool ProjectScenePoint(const Mat4& viewProjection, const ViewportLayout& layout, Vec3 point, ImVec2& screen)
{
	const Vec4 clip = Transform(viewProjection, {point.x, point.y, point.z, 1.f});
	if (!std::isfinite(clip.w) || clip.w <= 1.0e-7f || clip.z < 0.f || clip.z > clip.w)
	{
		return false;
	}
	screen = {layout.displayRect.x + (clip.x / clip.w + 1.f) * layout.displayRect.width * 0.5f,
	          layout.displayRect.y + (1.f - clip.y / clip.w) * layout.displayRect.height * 0.5f};
	return std::isfinite(screen.x) && std::isfinite(screen.y);
}

void DrawSceneGrid(ImDrawList* drawList, const Camera& camera, Float32 aspect, const ViewportLayout& layout,
                   bool twoDMode, Float32 gridSize)
{
	if (!drawList || !layout.visible || !std::isfinite(aspect) || aspect <= 0.f || !std::isfinite(gridSize) ||
	    gridSize <= 0.f)
	{
		return;
	}
	const Vec3 viewDirection = camera.target - camera.position;
	const Float32 viewDistance = std::sqrt(Dot(viewDirection, viewDirection));
	const Float32 halfHeight = camera.orthographic
	                                ? camera.orthographicSize
	                                : viewDistance * std::tan(camera.verticalFovRadians * 0.5f);
	if (!std::isfinite(halfHeight) || halfHeight <= 0.f)
	{
		return;
	}
	const Float32 halfWidth = halfHeight * aspect;
	const Float32 halfSpan = std::max(halfWidth, halfHeight) * 1.5f;
	if (!std::isfinite(halfSpan) || halfSpan <= 0.f)
	{
		return;
	}
	Float32 spacing = gridSize * std::pow(
	                            10.f, std::floor(std::log10(std::max(0.0001f, std::min(halfWidth, halfHeight) / 8.f) /
	                                                       gridSize)));
	while (halfSpan / spacing > 80.f)
	{
		spacing *= 10.f;
	}
	if (!std::isfinite(spacing) || spacing <= 0.f)
	{
		return;
	}

	const Float32 centerX = camera.target.x;
	const Float32 centerOther = twoDMode ? camera.target.y : camera.target.z;
	const Float32 indexLimit = static_cast<Float32>(std::numeric_limits<Int64>::max() / 4);
	if (!std::isfinite(centerX) || !std::isfinite(centerOther) ||
	    std::max(std::abs(centerX), std::abs(centerOther)) / spacing > indexLimit)
	{
		return;
	}
	const Int64 firstX = static_cast<Int64>(std::ceil((centerX - halfSpan) / spacing));
	const Int64 lastX = static_cast<Int64>(std::floor((centerX + halfSpan) / spacing));
	const Int64 firstOther = static_cast<Int64>(std::ceil((centerOther - halfSpan) / spacing));
	const Int64 lastOther = static_cast<Int64>(std::floor((centerOther + halfSpan) / spacing));
	const Mat4 viewProjection = camera.ViewProjection(aspect);
	auto project = [&](Float32 x, Float32 other, ImVec2& screen)
	{
		return ProjectScenePoint(viewProjection, layout,
		                        twoDMode ? Vec3{x, other, 0.f} : Vec3{x, 0.f, other}, screen);
	};
	for (Int64 index = firstX; index <= lastX; ++index)
	{
		const Float32 x = static_cast<Float32>(index) * spacing;
		ImVec2 first{};
		ImVec2 second{};
		if (project(x, centerOther - halfSpan, first) && project(x, centerOther + halfSpan, second))
		{
			const ImU32 color = index == 0 ? IM_COL32(224, 92, 92, 190)
			                             : index % 10 == 0 ? IM_COL32(150, 160, 174, 110)
			                                               : IM_COL32(140, 150, 164, 48);
			drawList->AddLine(first, second, color, index == 0 ? 1.4f : 1.f);
		}
	}
	for (Int64 index = firstOther; index <= lastOther; ++index)
	{
		const Float32 other = static_cast<Float32>(index) * spacing;
		ImVec2 first{};
		ImVec2 second{};
		if (project(centerX - halfSpan, other, first) && project(centerX + halfSpan, other, second))
		{
			const ImU32 color = index == 0 ? (twoDMode ? IM_COL32(110, 205, 128, 190) : IM_COL32(105, 155, 230, 190))
			                             : index % 10 == 0 ? IM_COL32(150, 160, 174, 110)
			                                               : IM_COL32(140, 150, 164, 48);
			drawList->AddLine(first, second, color, index == 0 ? 1.4f : 1.f);
		}
	}
}

Vector<ResourceId> PickSceneObjectsInRectangle(const Camera& camera, Float32 aspect, const ViewportLayout& layout,
                                               ViewportPoint first, ViewportPoint second,
                                               const Vector<Rendering::RenderObject>& renderObjects, Scene& scene,
                                               const EditorHierarchy& hierarchy)
{
	const Float32 left = std::min(first.x, second.x);
	const Float32 top = std::min(first.y, second.y);
	const Float32 right = std::max(first.x, second.x);
	const Float32 bottom = std::max(first.y, second.y);
	const bool crossing = second.x < first.x;
	Vector<ResourceId> picked;
	for (const auto& object : renderObjects)
	{
		ViewportRect boundsRect;
		if (!object.objectId || !ProjectSelectionBounds(camera, aspect, layout, object.worldBounds, boundsRect))
		{
			continue;
		}
		if (!hierarchy.SceneObjectVisible(object.objectId) || !hierarchy.SceneObjectPickable(object.objectId))
		{
			continue;
		}
		const bool selected = crossing ? boundsRect.x <= right && boundsRect.x + boundsRect.width >= left &&
		                                     boundsRect.y <= bottom && boundsRect.y + boundsRect.height >= top
		                               : boundsRect.x >= left && boundsRect.x + boundsRect.width <= right &&
		                                     boundsRect.y >= top && boundsRect.y + boundsRect.height <= bottom;
		if (!selected || std::find(picked.begin(), picked.end(), object.objectId) != picked.end())
		{
			continue;
		}
		picked.push_back(object.objectId);
	}
	std::function<void(const GameObject::Ptr&)> collectTransforms = [&](const GameObject::Ptr& object)
	{
		if (!object)
		{
			return;
		}
		auto active = scene.ActiveInHierarchy(object);
		if (!active || !active.value)
		{
			return;
		}
		auto id = scene.PersistentId(object);
		const bool hasRenderedBounds =
		    id && std::any_of(renderObjects.begin(), renderObjects.end(), [&](const auto& rendered)
		                      { return rendered.objectId == id.value && !rendered.worldBounds.empty; });
		if (id && hierarchy.SceneObjectVisible(id.value) && hierarchy.SceneObjectPickable(id.value) &&
		    !hasRenderedBounds)
		{
			ViewportPoint origin;
			if (ProjectObjectOrigin(camera, aspect, layout, scene, object, origin))
			{
				const bool inside = origin.x >= left && origin.x <= right && origin.y >= top && origin.y <= bottom;
				if (inside && std::find(picked.begin(), picked.end(), id.value) == picked.end())
				{
					picked.push_back(id.value);
				}
			}
		}
		for (const auto& child : object->Children())
		{
			collectTransforms(child);
		}
	};
	for (const auto& root : scene.RootGameObjects())
	{
		collectTransforms(root);
	}
	return picked;
}

Vec3 Add(Vec3 left, Vec3 right)
{
	return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 Scale(Vec3 value, Float32 scalar)
{
	return {value.x * scalar, value.y * scalar, value.z * scalar};
}

void FocusSceneSelection(Scene& scene, const Vector<GameObject::Ptr>& selected, Camera& camera, Float32 aspect)
{
	if (selected.empty())
	{
		return;
	}
	auto selectedWorld = scene.WorldTransform(selected.front());
	if (!selectedWorld)
	{
		return;
	}
	const Vec3 pivot{selectedWorld.value.At(0, 3), selectedWorld.value.At(1, 3), selectedWorld.value.At(2, 3)};
	Vector<ResourceId> subtreeIds;
	std::function<void(const GameObject::Ptr&)> collectIds = [&](const GameObject::Ptr& object)
	{
		auto id = scene.PersistentId(object);
		if (!id || std::find(subtreeIds.begin(), subtreeIds.end(), id.value) != subtreeIds.end())
		{
			return;
		}
		subtreeIds.push_back(id.value);
		auto children = scene.ChildrenOf(object);
		if (children)
		{
			for (const auto& child : children.value)
			{
				collectIds(child);
			}
		}
	};
	for (const auto& object : selected)
	{
		collectIds(object);
	}

	Vec3 minimum{};
	Vec3 maximum{};
	bool hasBounds = false;
	auto renderData = scene.CollectPreviewRenderData(camera, aspect);
	if (renderData)
	{
		for (const auto& object : renderData.value.objects)
		{
			if (object.worldBounds.empty ||
			    std::find(subtreeIds.begin(), subtreeIds.end(), object.objectId) == subtreeIds.end())
			{
				continue;
			}
			if (!hasBounds)
			{
				minimum = object.worldBounds.minimum;
				maximum = object.worldBounds.maximum;
				hasBounds = true;
				continue;
			}
			minimum = {std::min(minimum.x, object.worldBounds.minimum.x),
			           std::min(minimum.y, object.worldBounds.minimum.y),
			           std::min(minimum.z, object.worldBounds.minimum.z)};
			maximum = {std::max(maximum.x, object.worldBounds.maximum.x),
			           std::max(maximum.y, object.worldBounds.maximum.y),
			           std::max(maximum.z, object.worldBounds.maximum.z)};
		}
	}
	if (!hasBounds)
	{
		minimum = pivot;
		maximum = pivot;
		for (const ResourceId id : subtreeIds)
		{
			auto object = scene.GameObjectById(id);
			if (!object)
			{
				continue;
			}
			auto world = scene.WorldTransform(object.value);
			if (!world)
			{
				continue;
			}
			const Vec3 position{world.value.At(0, 3), world.value.At(1, 3), world.value.At(2, 3)};
			minimum = {std::min(minimum.x, position.x), std::min(minimum.y, position.y),
			           std::min(minimum.z, position.z)};
			maximum = {std::max(maximum.x, position.x), std::max(maximum.y, position.y),
			           std::max(maximum.z, position.z)};
		}
	}
	const Vec3 center{(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, (minimum.z + maximum.z) * 0.5f};
	const Vec3 extent = maximum - minimum;
	Float32 radius = std::sqrt(Dot(extent, extent)) * 0.5f;
	if (radius <= std::numeric_limits<Float32>::epsilon())
	{
		radius = 10.f;
	}
	FocusSceneView(camera, center, radius, aspect);
}

bool SetSceneView2DMode(Camera& camera, bool& twoDMode, bool enabled)
{
	if (twoDMode == enabled || !std::isfinite(camera.verticalFovRadians) || camera.verticalFovRadians <= 0.f ||
	    camera.verticalFovRadians >= 3.14159265f)
	{
		return false;
	}
	const Vec3 offset = camera.position - camera.target;
	Float32 distance = std::sqrt(Dot(offset, offset));
	if (!std::isfinite(distance) || distance <= 0.f)
	{
		return false;
	}
	if (enabled)
	{
		if (!camera.orthographic)
		{
			camera.orthographicSize = std::max(distance * std::tan(camera.verticalFovRadians * 0.5f), 0.05f);
		}
		camera.orthographic = true;
		camera.position = Add(camera.target, {0.f, 0.f, distance});
		camera.up = {0.f, 1.f, 0.f};
	}
	else
	{
		camera.orthographic = false;
		camera.up = {0.f, 1.f, 0.f};
		distance = std::max(camera.orthographicSize, 0.05f) / std::tan(camera.verticalFovRadians * 0.5f);
		camera.position = Add(camera.target, Scale(Normalized(Vec3{1.f, 1.f, 1.f}), distance));
	}
	twoDMode = enabled;
	return true;
}

bool DrawSceneOrientationGizmo(Camera& camera, ImVec2 viewportMinimum, ImVec2 viewportMaximum,
                               Float32& offsetX, Float32& offsetY, bool& dragging,
                               bool& twoDMode, bool& rotationLocked,
                               bool& showBackground, bool& settingsChanged, bool& hovered)
{
	const Float32 radius = 30.f;
	const Float32 axisHitRadius = 9.f;
	const Float32 centerHitRadius = 7.f;
	const Float32 positiveAxisMarkerRadius = 7.f;
	const Float32 negativeAxisMarkerRadius = 6.f;
	const Float32 defaultCenterX = viewportMaximum.x - 50.f;
	const Float32 defaultCenterY = viewportMinimum.y + 50.f;
	ImVec2 center{std::clamp(defaultCenterX + offsetX, viewportMinimum.x + 42.f, viewportMaximum.x - 42.f),
	              std::clamp(defaultCenterY + offsetY, viewportMinimum.y + 42.f, viewportMaximum.y - 42.f)};
	hovered = false;
	if (twoDMode)
	{
		return false;
	}
	const ImVec2 lockCenter{center.x + 21.f, center.y - radius - 7.f};
	const ImRect lockRect({lockCenter.x - 9.f, lockCenter.y - 9.f}, {lockCenter.x + 9.f, lockCenter.y + 9.f});
	const Vec3 forward = Normalized(camera.target - camera.position);
	const Vec3 right = Normalized(Cross(forward, camera.up));
	const Vec3 screenUp = Normalized(Cross(right, forward));
	const ImRect dragHandleRect({center.x - 38.f, center.y - 38.f}, {center.x - 20.f, center.y - 26.f});
	const bool dragHandleHovered = dragHandleRect.Contains(ImGui::GetIO().MousePos);
	if (dragHandleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		dragging = true;
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		dragging = false;
	}
	if (dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		center.x = std::clamp(center.x + delta.x, viewportMinimum.x + 42.f, viewportMaximum.x - 42.f);
		center.y = std::clamp(center.y + delta.y, viewportMinimum.y + 42.f, viewportMaximum.y - 42.f);
		offsetX = std::clamp(center.x - defaultCenterX, -10000.f, 10000.f);
		offsetY = std::clamp(center.y - defaultCenterY, -10000.f, 10000.f);
		settingsChanged = true;
	}
	hovered |= dragHandleHovered || dragging;

	struct AxisEnd
	{
		Vec3 direction;
		const char* label;
		ImU32 color;
	};

	const AxisEnd ends[]{
	    {{1.f, 0.f, 0.f}, "X", IM_COL32(218, 72, 72, 255)},
	    {{0.f, 1.f, 0.f}, "Y", IM_COL32(88, 190, 88, 255)},
	    {{0.f, 0.f, 1.f}, "Z", IM_COL32(80, 132, 230, 255)},
	    {{-1.f, 0.f, 0.f}, "-X", IM_COL32(218, 72, 72, 255)},
	    {{0.f, -1.f, 0.f}, "-Y", IM_COL32(88, 190, 88, 255)},
	    {{0.f, 0.f, -1.f}, "-Z", IM_COL32(80, 132, 230, 255)}};
	auto* drawList = ImGui::GetWindowDrawList();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	bool clicked = false;
	const bool lockHovered = ImGui::IsMouseHoveringRect(lockRect.Min, lockRect.Max, true);
	hovered |= lockHovered;
	drawList->AddRectFilled(lockRect.Min, lockRect.Max,
	                        ImGui::GetColorU32(rotationLocked ? ImGuiCol_ButtonActive
	                                           : lockHovered    ? ImGuiCol_ButtonHovered
	                                                            : ImGuiCol_FrameBg),
	                        3.f);
	drawList->AddRect(lockRect.Min, lockRect.Max, ImGui::GetColorU32(ImGuiCol_Border), 3.f);
	const ImU32 lockColor = ImGui::GetColorU32(ImGuiCol_Text);
	const ImVec2 lockBodyMinimum{lockCenter.x - 4.f, lockCenter.y};
	const ImVec2 lockBodyMaximum{lockCenter.x + 4.f, lockCenter.y + 6.f};
	drawList->AddRectFilled(lockBodyMinimum, lockBodyMaximum, lockColor, 1.f);
	if (rotationLocked)
	{
		drawList->PathArcTo({lockCenter.x, lockCenter.y + 1.f}, 3.f, 3.14159f, 6.28318f, 12);
	}
	else
	{
		drawList->PathLineTo({lockCenter.x - 3.f, lockCenter.y + 1.f});
		drawList->PathArcTo({lockCenter.x, lockCenter.y + 1.f}, 3.f, 3.14159f, 5.25f, 8);
	}
	drawList->PathStroke(lockColor, false, 1.5f);
	if (lockHovered)
	{
		ImGui::SetTooltip(rotationLocked ? "Unlock Scene rotation" : "Lock Scene rotation");
	}
	if (lockHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		rotationLocked = !rotationLocked;
	}
	const char* projectionLabel = camera.orthographic ? "<Iso>" : "<Persp>";
	const ImVec2 projectionLabelSize = ImGui::CalcTextSize(projectionLabel);
	const ImVec2 projectionLabelMinimum{center.x - projectionLabelSize.x * 0.5f - 5.f,
	                                    center.y + radius + 4.f};
	const ImVec2 projectionLabelMaximum{center.x + projectionLabelSize.x * 0.5f + 5.f,
	                                    projectionLabelMinimum.y + ImGui::GetTextLineHeight() + 4.f};
	const bool projectionHovered = ImGui::IsMouseHoveringRect(projectionLabelMinimum, projectionLabelMaximum, true);
	auto toggleProjection = [&]()
	{
		if (rotationLocked)
		{
			return;
		}
		const Vec3 offset = camera.position - camera.target;
		const Float32 distance = std::sqrt(Dot(offset, offset));
		if (!std::isfinite(distance) || distance <= 0.f || !std::isfinite(camera.verticalFovRadians) ||
		    camera.verticalFovRadians <= 0.f || camera.verticalFovRadians >= 3.14159265f)
		{
			return;
		}
		if (camera.orthographic)
		{
			camera.orthographic = false;
			camera.position = Add(camera.target, Scale(Normalized(offset), std::max(camera.orthographicSize, 0.05f) /
			                                                               std::tan(camera.verticalFovRadians * 0.5f)));
		}
		else
		{
			camera.orthographicSize = std::max(distance * std::tan(camera.verticalFovRadians * 0.5f), 0.05f);
			camera.orthographic = true;
		}
		twoDMode = false;
		clicked = true;
	};
	hovered |= projectionHovered;
	drawList->AddRectFilled(projectionLabelMinimum, projectionLabelMaximum,
	                        ImGui::GetColorU32(projectionHovered ? ImGuiCol_ButtonHovered : ImGuiCol_WindowBg), 3.f);
	drawList->AddText({center.x - projectionLabelSize.x * 0.5f,
	                   projectionLabelMinimum.y + 2.f},
	                  ImGui::GetColorU32(ImGuiCol_Text), projectionLabel);
	if (projectionHovered)
	{
		ImGui::SetTooltip("Toggle Perspective / Orthographic");
	}
	if (projectionHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		toggleProjection();
	}
	const auto distanceToMouse = [&](ImVec2 point)
	{
		const Float32 dx = point.x - mouse.x;
		const Float32 dy = point.y - mouse.y;
		return std::sqrt(dx * dx + dy * dy);
	};
	const auto snapToDirection = [&](Vec3 direction)
	{
		const Vec3 offset = camera.position - camera.target;
		const Float32 distance = std::sqrt(Dot(offset, offset));
		if (std::isfinite(distance) && distance > 0.f)
		{
			twoDMode = false;
			camera.position = Add(camera.target, Scale(direction, distance));
			camera.up = std::abs(direction.y) > 0.5f
			                ? Vec3{0.f, 0.f, direction.y > 0.f ? -1.f : 1.f}
			                : Vec3{0.f, 1.f, 0.f};
			clicked = true;
		}
	};
	const auto snapToNiceAngle = [&](bool forcePerspective)
	{
		const Vec3 view = camera.target - camera.position;
		Float32 distance = std::sqrt(Dot(view, view));
		if (!std::isfinite(distance) || distance <= 0.f)
		{
			return;
		}
		if (forcePerspective && camera.orthographic &&
		    (!std::isfinite(camera.verticalFovRadians) || camera.verticalFovRadians <= 0.f ||
		     camera.verticalFovRadians >= 3.14159265f))
		{
			return;
		}
		Vec3 forward{view.x, 0.f, view.z};
		if (Dot(forward, forward) <= 1.0e-6f)
		{
			forward = {0.f, 0.f, 1.f};
		}
		forward = Normalized(forward);
		forward.y = -0.5f;
		forward = Normalized(forward);
		if (forcePerspective && camera.orthographic)
		{
			camera.orthographic = false;
			distance = std::max(camera.orthographicSize, 0.05f) /
			           std::tan(camera.verticalFovRadians * 0.5f);
		}
		camera.up = {0.f, 1.f, 0.f};
		camera.position = Add(camera.target, Scale(forward, -distance));
		twoDMode = false;
		clicked = true;
	};
	const Float32 centerDistance = distanceToMouse(center);
	if (!twoDMode)
	{
		const ImVec2 gizmoMinimum{center.x - 42.f, center.y - 42.f};
		const ImVec2 gizmoMaximum{center.x + 42.f, center.y + 42.f};
		if (showBackground)
		{
			drawList->AddRectFilled(gizmoMinimum, gizmoMaximum, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.28f), 5.f);
			drawList->AddRect(gizmoMinimum, gizmoMaximum, ImGui::GetColorU32(ImGuiCol_Border, 0.5f), 5.f);
		}
		const ImU32 handleColor = ImGui::GetColorU32(dragHandleHovered || dragging ? ImGuiCol_Text : ImGuiCol_TextDisabled);
		for (Float32 line = 0.f; line < 3.f; ++line)
		{
			const Float32 y = center.y - 36.f + line * 4.f;
			drawList->AddLine({center.x - 35.f, y}, {center.x - 23.f, y}, handleColor, 1.5f);
		}
		Vector<const AxisEnd*> depthSortedEnds;
		depthSortedEnds.reserve(std::size(ends));
		for (const AxisEnd& end : ends)
		{
			depthSortedEnds.push_back(&end);
		}
		std::sort(depthSortedEnds.begin(), depthSortedEnds.end(), [&](const AxisEnd* leftEnd, const AxisEnd* rightEnd)
		          { return Dot(leftEnd->direction, forward) > Dot(rightEnd->direction, forward); });
		const Float32 lockedAlpha = rotationLocked ? 0.4f : 1.f;
		for (const AxisEnd* end : depthSortedEnds)
		{
			const Float32 projectedX = Dot(end->direction, right);
			const Float32 projectedY = -Dot(end->direction, screenUp);
			const Float32 depth = Dot(end->direction, forward);
			const ImVec2 point{center.x + projectedX * 29.f, center.y + projectedY * 29.f};
			const Float32 projectedLength = std::sqrt(projectedX * projectedX + projectedY * projectedY);
			const bool positiveAxis = end->label[0] != '-';
			const Float32 markerRadius = positiveAxis ? positiveAxisMarkerRadius : negativeAxisMarkerRadius;
			const ImVec2 lineEnd = projectedLength > 1.0e-4f
			                           ? ImVec2{point.x - projectedX / projectedLength * markerRadius,
			                                    point.y - projectedY / projectedLength * markerRadius}
			                           : point;
			const bool axisHovered = distanceToMouse(point) <= axisHitRadius;
			hovered |= axisHovered;
			const Float32 fade = (depth <= 0.3f ? 1.f : std::clamp((1.f - depth) / 0.7f, 0.f, 1.f)) *
			                     lockedAlpha;
			const auto fadedColor = [fade](ImU32 color, Float32 alpha = 1.f)
			{
				return IM_COL32(color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff,
				                 static_cast<Int32>(std::round(255.f * fade * alpha)));
			};
			drawList->AddLine(center, lineEnd, fadedColor(end->color, positiveAxis ? 0.7f : 0.5f), 1.5f);
			if (positiveAxis)
			{
				drawList->AddCircleFilled(point, markerRadius, fadedColor(end->color), 20);
				const ImVec2 labelSize = ImGui::CalcTextSize(end->label);
				drawList->AddText({point.x - labelSize.x * 0.5f, point.y - labelSize.y * 0.5f},
				                  fadedColor(axisHovered ? end->color : IM_COL32(255, 255, 255, 255)), end->label);
			}
			else
			{
				drawList->AddCircle(point, markerRadius, fadedColor(end->color, 0.85f), 20, 2.f);
			}
			if (axisHovered && !rotationLocked &&
			    (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
			     ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
			{
				snapToDirection(end->direction);
			}
		}
		const bool centerHovered = centerDistance <= centerHitRadius;
		const ImU32 centerColor = ImGui::GetColorU32(centerHovered ? ImGuiCol_ButtonActive : ImGuiCol_Text,
		                                            lockedAlpha);
		if (camera.orthographic)
		{
			drawList->AddRectFilled({center.x - 3.f, center.y - 3.f}, {center.x + 3.f, center.y + 3.f}, centerColor);
		}
		else
		{
			drawList->AddCircleFilled(center, 3.f, centerColor, 20);
		}
	}
	const ImRect orientationRect({center.x - radius - 10.f, center.y - radius - 10.f},
	                              {center.x + radius + 10.f, center.y + radius + 10.f});
	hovered |= orientationRect.Contains(mouse);
	if (!twoDMode && orientationRect.Contains(mouse) && !lockRect.Contains(mouse) &&
	    ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("SceneViewAngles");
	}
	if (ImGui::BeginPopup("SceneViewAngles"))
	{
		const Vec3 viewDirection = Normalized(camera.target - camera.position);
		Int32 alignedAxis = -1;
		for (Int32 index = 0; index < 6; ++index)
		{
			if (Dot(viewDirection, Scale(ends[index].direction, -1.f)) > 0.999f)
			{
				alignedAxis = index;
				break;
			}
		}
		ImGui::BeginDisabled(rotationLocked);
		if (ImGui::MenuItem("Free", nullptr, alignedAxis < 0))
		{
			snapToNiceAngle(false);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Right", nullptr, alignedAxis == 0))
		{
			snapToDirection({1.f, 0.f, 0.f});
		}
		if (ImGui::MenuItem("Top", nullptr, alignedAxis == 1))
		{
			snapToDirection({0.f, 1.f, 0.f});
		}
		if (ImGui::MenuItem("Front", nullptr, alignedAxis == 2))
		{
			snapToDirection({0.f, 0.f, 1.f});
		}
		if (ImGui::MenuItem("Left", nullptr, alignedAxis == 3))
		{
			snapToDirection({-1.f, 0.f, 0.f});
		}
		if (ImGui::MenuItem("Bottom", nullptr, alignedAxis == 4))
		{
			snapToDirection({0.f, -1.f, 0.f});
		}
		if (ImGui::MenuItem("Back", nullptr, alignedAxis == 5))
		{
			snapToDirection({0.f, 0.f, -1.f});
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Perspective", nullptr, !camera.orthographic))
		{
			toggleProjection();
		}
		ImGui::EndDisabled();
		ImGui::Separator();
		if (ImGui::MenuItem("Show background", nullptr, &showBackground))
		{
			settingsChanged = true;
		}
		ImGui::EndPopup();
	}
	if (!rotationLocked && orientationRect.Contains(mouse) && centerDistance <= centerHitRadius)
	{
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
		    (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyShift))
		{
			snapToNiceAngle(true);
		}
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			toggleProjection();
		}
	}
	if (hovered)
	{
		ImGui::SetTooltip(dragHandleHovered || dragging ? "Drag to move the orientation gizmo"
		                                               : "Click an axis to align the Scene view");
	}
	return clicked;
}
} // namespace

Result<SceneDropPoint> ResolveSceneDropPoint(const Camera& camera, Float32 aspect, const ViewportLayout& layout,
                                             ViewportPoint screenPosition,
                                             const Vector<Rendering::RenderObject>& renderObjects)
{
	const auto pixel = ScreenToViewportPixel(screenPosition, layout);
	if (!pixel || !layout.targetPixels.width || !layout.targetPixels.height || !std::isfinite(aspect) || aspect <= 0.f)
	{
		return Result<SceneDropPoint>::Failure("The pointer is outside the Scene image viewport");
	}
	try
	{
		const Float32 x = 2.f * pixel->x / static_cast<Float32>(layout.targetPixels.width) - 1.f;
		const Float32 y = 1.f - 2.f * pixel->y / static_cast<Float32>(layout.targetPixels.height);
		const Mat4 inverse = camera.ViewProjection(aspect).Inverse();
		const auto unproject = [&](Float32 depth) -> Result<Vec3>
		{
			const Vec4 homogeneous = Transform(inverse, {x, y, depth, 1.f});
			if (!std::isfinite(homogeneous.w) || std::abs(homogeneous.w) < 1.0e-7f)
			{
				return Result<Vec3>::Failure("The Scene camera ray has an invalid homogeneous coordinate");
			}
			return Result<Vec3>::Success(
			    {homogeneous.x / homogeneous.w, homogeneous.y / homogeneous.w, homogeneous.z / homogeneous.w});
		};
		auto nearPoint = unproject(0.f);
		auto farPoint = unproject(1.f);
		if (!nearPoint || !farPoint)
		{
			return Result<SceneDropPoint>::Failure(!nearPoint ? nearPoint.error : farPoint.error);
		}
		const Vec3 direction = Normalized(farPoint.value - nearPoint.value);
		if (Dot(direction, direction) < 0.99f)
		{
			return Result<SceneDropPoint>::Failure("The Scene camera ray is degenerate");
		}
		Float32 nearest = std::numeric_limits<Float32>::max();
		bool hitGeometry = false;
		for (const auto& object : renderObjects)
		{
			Float32 distance = 0.f;
			if (IntersectBounds(nearPoint.value, direction, object.worldBounds, distance) && distance < nearest)
			{
				nearest = distance;
				hitGeometry = true;
			}
		}
		if (hitGeometry)
		{
			return Result<SceneDropPoint>::Success({Add(nearPoint.value, Scale(direction, nearest)), true});
		}
		const Vec3 planeNormal = Normalized(camera.target - camera.position);
		const Float32 denominator = Dot(direction, planeNormal);
		if (std::abs(denominator) < 1.0e-6f)
		{
			return Result<SceneDropPoint>::Failure("The Scene camera ray is parallel to the camera target plane");
		}
		const Float32 planeDistance = Dot(camera.target - nearPoint.value, planeNormal) / denominator;
		if (!std::isfinite(planeDistance) || planeDistance < 0.f)
		{
			return Result<SceneDropPoint>::Failure("The Scene camera target plane is behind the viewport ray");
		}
		return Result<SceneDropPoint>::Success({Add(nearPoint.value, Scale(direction, planeDistance)), false});
	}
	catch (const std::exception& error)
	{
		return Result<SceneDropPoint>::Failure(ToUrsaString(error.what()));
	}
}

Result<std::unique_ptr<SceneViewPanel>> SceneViewPanel::Create(Rhi::DevicePtr selectedDevice, ImGuiBridge& gui)
{
	if (!selectedDevice)
	{
		return Result<std::unique_ptr<SceneViewPanel>>::Failure("Scene view requires an RHI device");
	}
	auto panel = std::unique_ptr<SceneViewPanel>(new SceneViewPanel(std::move(selectedDevice), gui));
	auto initialized = panel->Initialize();
	if (!initialized)
	{
		return Result<std::unique_ptr<SceneViewPanel>>::Failure(initialized.error);
	}
	return Result<std::unique_ptr<SceneViewPanel>>::Success(std::move(panel));
}

SceneViewPanel::~SceneViewPanel()
{
	SetSceneCursorCaptured(false);
	if (gui)
	{
		if (sceneTarget.imageId != ImTextureID_Invalid)
		{
			gui->UnregisterTexture(sceneTarget.imageId);
		}
		if (gameTarget.imageId != ImTextureID_Invalid)
		{
			gui->UnregisterTexture(gameTarget.imageId);
		}
	}
}

void SceneViewPanel::SetSceneCursorCaptured(bool captured)
{
	if (sceneCursorCaptured == captured)
	{
		return;
	}
	if (captured)
	{
		sceneNavigationWindow = gui ? gui->InputWindowForViewport(ImGui::GetWindowViewport()) : nullptr;
		if (sceneNavigationWindow)
		{
			sceneNavigationWindow->SetCursorCaptured(true);
			sceneCursorCaptured = true;
		}
		return;
	}
	if (sceneNavigationWindow)
	{
		sceneNavigationWindow->SetCursorCaptured(false);
	}
	sceneNavigationWindow.reset();
	sceneCursorCaptured = false;
}

Result<void> SceneViewPanel::Initialize()
{
	auto selectedPreview = EditorScenePreview::Create(device);
	if (!selectedPreview)
	{
		return Result<void>::Failure(selectedPreview.error);
	}
	sceneTarget.preview = std::move(selectedPreview.value);
	auto gamePreview = EditorScenePreview::Create(device);
	if (!gamePreview)
	{
		return Result<void>::Failure(gamePreview.error);
	}
	gameTarget.preview = std::move(gamePreview.value);
	auto selectedSampler = device->CreateSampler({true, false});
	if (!selectedSampler)
	{
		return Result<void>::Failure(selectedSampler.error);
	}
	sampler = std::move(selectedSampler.value);
	return {};
}

void SceneViewPanel::StartFocusTransition(const Camera& current, const Camera& target)
{
	sceneFocusTransition.start = current;
	sceneFocusTransition.target = target;
	sceneFocusTransition.elapsed = 0.f;
	sceneFocusTransition.active = true;
}

void SceneViewPanel::AdvanceFocusTransition(Camera& camera, Float32 deltaTime)
{
	if (!sceneFocusTransition.active)
	{
		return;
	}
	constexpr Float32 duration = 0.25f;
	sceneFocusTransition.elapsed = std::min(duration, sceneFocusTransition.elapsed + std::max(0.f, deltaTime));
	const Float32 progress = sceneFocusTransition.elapsed / duration;
	const Float32 eased = 1.f - (1.f - progress) * (1.f - progress) * (1.f - progress);
	const auto interpolate = [eased](Vec3 from, Vec3 to)
	{
		return Vec3{from.x + (to.x - from.x) * eased, from.y + (to.y - from.y) * eased,
		            from.z + (to.z - from.z) * eased};
	};
	camera.position = interpolate(sceneFocusTransition.start.position, sceneFocusTransition.target.position);
	camera.target = interpolate(sceneFocusTransition.start.target, sceneFocusTransition.target.target);
	camera.orthographicSize = sceneFocusTransition.start.orthographicSize +
	                          (sceneFocusTransition.target.orthographicSize -
	                           sceneFocusTransition.start.orthographicSize) * eased;
	camera.orthographic = sceneFocusTransition.target.orthographic;
	if (progress >= 1.f)
	{
		sceneFocusTransition.active = false;
	}
}

Result<void> SceneViewPanel::EnsureTarget(ViewportTarget& target, ViewportSize size)
{
	if (!size.width || !size.height)
	{
		return Result<void>::Failure("Viewport target size must be positive");
	}
	if (target.color && target.size.width == size.width && target.size.height == size.height)
	{
		return {};
	}
	auto capabilities = device->Capabilities();
	auto validated = ResolveViewportSize(size, GameViewSizeMode::FixedResolution, size.width, size.height,
	                                     capabilities.maxTextureSize, 16'777'216);
	if (!validated)
	{
		return Result<void>::Failure(validated.error);
	}
	auto replacement = Rendering::CreateRenderTexture(
	    *device, {size.width, size.height, static_cast<UInt32>(Rhi::Format::RgbA8Unorm), 32});
	if (!replacement)
	{
		return Result<void>::Failure("Cannot create " + ToUrsaString(std::to_string(size.width)) + "x" +
		                             ToUrsaString(std::to_string(size.height)) +
		                             " viewport target: " + replacement.error);
	}
	// The GUI can present before the first successful pipeline callback (for example during reload).
	// Give a replacement target valid contents and a sampled layout before exposing its image ID.
	auto initializer = Rendering::RenderCommandExecutor::Create(device);
	if (!initializer)
	{
		return Result<void>::Failure(initializer.error);
	}
	auto initialized = initializer.value->Begin(replacement.value, Rhi::ResourceState::ShaderRead);
	if (initialized)
	{
		Rendering::RenderCommand clear;
		clear.opcode = Rendering::RenderCommandOpcode::ClearTarget;
		clear.clearFlags = 1;
		clear.color = {0.035f, 0.055f, 0.09f, 1};
		initialized = initializer.value->Execute({clear});
	}
	if (initialized)
	{
		initialized = initializer.value->Submit();
	}
	if (!initialized)
	{
		return initialized;
	}
	auto submitted = initializer.value->End();
	if (!submitted)
	{
		return Result<void>::Failure(submitted.error);
	}
	auto registered = gui->RegisterTexture(replacement.value->color, sampler);
	if (!registered)
	{
		return Result<void>::Failure("Cannot register viewport target: " + registered.error);
	}
	auto idle = device->WaitIdle();
	if (!idle)
	{
		gui->UnregisterTexture(registered.value);
		return Result<void>::Failure("Cannot replace viewport target safely: " + idle.error);
	}
	if (target.imageId != ImTextureID_Invalid)
	{
		gui->UnregisterTexture(target.imageId);
	}
	target.imageId = registered.value;
	target.output = std::move(replacement.value);
	target.color = target.output->color;
	target.size = size;
	return {};
}

void SceneViewPanel::SaveCustomPresets(VirtualFileSystem& userFiles, String& diagnostic)
{
	auto path = VirtualPath::Parse("user:Settings/GameViewPresets.json");
	if (!path)
	{
		diagnostic = path.error;
		return;
	}
	const String text = SerializeGameViewPresets(customPresets);
	auto written = userFiles.WriteAtomic(path.value, ToBytes(text));
	diagnostic = written ? String{} : written.error;
}

void SceneViewPanel::SaveProjectSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic)
{
	auto path = VirtualPath::Parse("user:Settings/GameViewProjects/" + projectKey + ".json");
	if (!path)
	{
		diagnostic = path.error;
		return;
	}
	GameViewSettingsState settings;
	settings.mode = gameSizeMode;
	settings.width = gamePresetWidth;
	settings.height = gamePresetHeight;
	settings.scale = gameScale;
	settings.fit = gameFit;
	settings.pan = gamePan;
	settings.presetId = selectedCustomPreset;
	settings.showStats = showGameStats;
	settings.showGizmos = showGameGizmos;
	settings.focusGameViewOnPlay = focusGameViewOnPlay;
	settings.disableEditorShortcutsInPlay = disableEditorShortcutsInPlay;
	settings.maximizeOnPlay = maximizeGameViewOnPlay;
	settings.warnIfNoGameCamera = warnIfNoGameCamera;
	settings.clearEveryFrameInEditMode = clearEveryFrameInEditMode;
	settings.gridSnapEnabled = gizmoSnapSettings.enabled;
	settings.snapMoveX = gizmoSnapSettings.moveX;
	settings.snapMoveY = gizmoSnapSettings.moveY;
	settings.snapMoveZ = gizmoSnapSettings.moveZ;
	settings.snapRotation = gizmoSnapSettings.rotation;
	settings.snapScale = gizmoSnapSettings.scale;
	settings.sceneCameraSpeed = sceneFlythroughSpeed;
	settings.sceneCameraSpeedMin = sceneFlythroughMinSpeed;
	settings.sceneCameraSpeedMax = sceneFlythroughMaxSpeed;
	settings.sceneCameraFieldOfView = sceneCameraFieldOfView;
	settings.sceneCameraNearClip = sceneCameraNearClip;
	settings.sceneCameraFarClip = sceneCameraFarClip;
	settings.showSceneGrid = showSceneGrid;
	settings.sceneGridSize = sceneGridSize;
	settings.showSceneGizmos = showSceneGizmos;
	settings.showSceneCameraGizmos = showSceneCameraGizmos;
	settings.showSceneLightGizmos = showSceneLightGizmos;
	settings.showSceneGizmoLabels = showSceneGizmoLabels;
	settings.sceneGizmo3DIcons = sceneGizmo3DIcons;
	settings.sceneGizmoIconSize = sceneGizmoIconSize;
	settings.showSceneOrientationBackground = showSceneOrientationBackground;
	settings.sceneOrientationGizmoOffsetX = sceneOrientationGizmoOffsetX;
	settings.sceneOrientationGizmoOffsetY = sceneOrientationGizmoOffsetY;
	const String text = SerializeGameViewSettings(settings);
	auto written = userFiles.WriteAtomic(path.value, ToBytes(text));
	diagnostic = written ? String{} : written.error;
}

void SceneViewPanel::LoadSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic)
{
	if (!globalSettingsLoaded)
	{
		globalSettingsLoaded = true;
		auto path = VirtualPath::Parse("user:Settings/GameViewPresets.json");
		if (path)
		{
			auto text = userFiles.ReadText(path.value);
			if (text)
			{
				auto presets =
				    DeserializeGameViewPresets(text.value, device->Capabilities().maxTextureSize, 16'777'216);
				if (presets)
				{
					customPresets = std::move(presets.value);
				}
				else
				{
					diagnostic = presets.error;
					customPresets.clear();
				}
			}
		}
	}
	if (projectSettingsLoaded && loadedProjectKey == projectKey)
	{
		return;
	}
	projectSettingsLoaded = true;
	loadedProjectKey = projectKey;
	gameSizeMode = GameViewSizeMode::FreeAspect;
	gamePresetWidth = 16;
	gamePresetHeight = 9;
	gameScale = 1;
	gameFit = true;
	gamePan = {};
	selectedCustomPreset.clear();
	showGameStats = false;
	showGameGizmos = true;
	focusGameViewOnPlay = true;
	disableEditorShortcutsInPlay = false;
	maximizeGameViewOnPlay = false;
	warnIfNoGameCamera = true;
	clearEveryFrameInEditMode = true;
	gizmoSnapSettings = {};
	sceneFlythroughSpeed = 1.f;
	sceneFlythroughMinSpeed = 0.01f;
	sceneFlythroughMaxSpeed = 2.f;
	sceneCameraFieldOfView = 45.f;
	sceneCameraNearClip = 0.1f;
	sceneCameraFarClip = 100.f;
	showSceneGrid = true;
	sceneGridSize = 1.f;
	showSceneGizmos = true;
	auto path = VirtualPath::Parse("user:Settings/GameViewProjects/" + projectKey + ".json");
	if (!path)
	{
		diagnostic = path.error;
		return;
	}
	auto text = userFiles.ReadText(path.value);
	if (!text)
	{
		return;
	}
	auto settings = DeserializeGameViewSettings(text.value);
	if (!settings)
	{
		gameSizeMode = GameViewSizeMode::FreeAspect;
		gamePresetWidth = 16;
		gamePresetHeight = 9;
		gameScale = 1;
		gameFit = true;
		gamePan = {};
		selectedCustomPreset.clear();
		showGameStats = false;
		showGameGizmos = true;
		focusGameViewOnPlay = true;
		disableEditorShortcutsInPlay = false;
		maximizeGameViewOnPlay = false;
		warnIfNoGameCamera = true;
		clearEveryFrameInEditMode = true;
		gizmoSnapSettings = {};
		sceneFlythroughSpeed = 1.f;
		sceneFlythroughMinSpeed = 0.01f;
		sceneFlythroughMaxSpeed = 2.f;
		sceneCameraFieldOfView = 45.f;
		sceneCameraNearClip = 0.1f;
		sceneCameraFarClip = 100.f;
		showSceneGrid = true;
		sceneGridSize = 1.f;
		showSceneGizmos = true;
		showSceneOrientationBackground = true;
		sceneOrientationGizmoOffsetX = 0.f;
		sceneOrientationGizmoOffsetY = 0.f;
		diagnostic = settings.error;
	}
	else
	{
		const auto capabilities = device->Capabilities();
		const auto& saved = settings.value;
		if (saved.width > capabilities.maxTextureSize || saved.height > capabilities.maxTextureSize ||
		    (saved.mode == GameViewSizeMode::FixedResolution &&
		     static_cast<UInt64>(saved.width) * saved.height > 16'777'216))
		{
			gameSizeMode = GameViewSizeMode::FreeAspect;
			gamePresetWidth = 16;
			gamePresetHeight = 9;
			gameScale = 1;
			gameFit = true;
			gamePan = {};
			selectedCustomPreset.clear();
			showGameStats = false;
			showGameGizmos = true;
			focusGameViewOnPlay = true;
			disableEditorShortcutsInPlay = false;
			maximizeGameViewOnPlay = false;
			warnIfNoGameCamera = true;
			clearEveryFrameInEditMode = true;
			gizmoSnapSettings = {};
			sceneFlythroughSpeed = 1.f;
			sceneFlythroughMinSpeed = 0.01f;
			sceneFlythroughMaxSpeed = 2.f;
			sceneCameraFieldOfView = 45.f;
			sceneCameraNearClip = 0.1f;
			sceneCameraFarClip = 100.f;
			showSceneGrid = true;
			sceneGridSize = 1.f;
			showSceneGizmos = true;
			showSceneOrientationBackground = true;
			sceneOrientationGizmoOffsetX = 0.f;
			sceneOrientationGizmoOffsetY = 0.f;
			diagnostic = "Saved Game resolution exceeds supported limits; restored defaults.";
		}
		else
		{
			gameSizeMode = saved.mode;
			gamePresetWidth = saved.width;
			gamePresetHeight = saved.height;
			gameScale = saved.scale;
			gameFit = saved.fit;
			gamePan = saved.pan;
			selectedCustomPreset = saved.presetId;
			showGameStats = saved.showStats;
			showGameGizmos = saved.showGizmos;
			focusGameViewOnPlay = saved.focusGameViewOnPlay;
			disableEditorShortcutsInPlay = saved.disableEditorShortcutsInPlay;
			maximizeGameViewOnPlay = saved.maximizeOnPlay;
			warnIfNoGameCamera = saved.warnIfNoGameCamera;
			clearEveryFrameInEditMode = saved.clearEveryFrameInEditMode;
			gizmoSnapSettings.enabled = saved.gridSnapEnabled;
			gizmoSnapSettings.moveX = saved.snapMoveX;
			gizmoSnapSettings.moveY = saved.snapMoveY;
			gizmoSnapSettings.moveZ = saved.snapMoveZ;
			gizmoSnapSettings.rotation = saved.snapRotation;
			gizmoSnapSettings.scale = saved.snapScale;
			sceneFlythroughSpeed = saved.sceneCameraSpeed;
			sceneFlythroughMinSpeed = saved.sceneCameraSpeedMin;
			sceneFlythroughMaxSpeed = saved.sceneCameraSpeedMax;
			sceneCameraFieldOfView = saved.sceneCameraFieldOfView;
			sceneCameraNearClip = saved.sceneCameraNearClip;
			sceneCameraFarClip = saved.sceneCameraFarClip;
			showSceneGrid = saved.showSceneGrid;
			sceneGridSize = saved.sceneGridSize;
			showSceneGizmos = saved.showSceneGizmos;
			showSceneCameraGizmos = saved.showSceneCameraGizmos;
			showSceneLightGizmos = saved.showSceneLightGizmos;
			showSceneGizmoLabels = saved.showSceneGizmoLabels;
			sceneGizmo3DIcons = saved.sceneGizmo3DIcons;
			sceneGizmoIconSize = saved.sceneGizmoIconSize;
			showSceneOrientationBackground = saved.showSceneOrientationBackground;
			sceneOrientationGizmoOffsetX = saved.sceneOrientationGizmoOffsetX;
			sceneOrientationGizmoOffsetY = saved.sceneOrientationGizmoOffsetY;
		}
		if (!selectedCustomPreset.empty() &&
		    std::none_of(customPresets.begin(), customPresets.end(),
		                 [this](const GameViewPreset& preset) { return preset.id == selectedCustomPreset; }))
		{
			selectedCustomPreset.clear();
			gameSizeMode = GameViewSizeMode::FreeAspect;
			gameFit = true;
			gamePan = {};
			diagnostic = "Saved Game preset no longer exists; restored Free Aspect.";
		}
	}
}

void SceneViewPanel::DrawGameToolbar(VirtualFileSystem* userFiles, const String& projectKey, String& diagnostic)
{
	if (userFiles && userFiles->IsMounted("user"))
	{
		LoadSettings(*userFiles, projectKey, diagnostic);
	}
	bool requestOpenCustomPreset = false;
	const String fixedLabel =
	    ToUrsaString(std::to_string(gamePresetWidth)) + "x" + ToUrsaString(std::to_string(gamePresetHeight));
	const auto selectedCustom =
	    std::find_if(customPresets.begin(), customPresets.end(),
	                 [this](const GameViewPreset& preset) { return preset.id == selectedCustomPreset; });
	const char* selectedLabel = selectedCustom != customPresets.end()          ? selectedCustom->name.c_str()
	                            : gameSizeMode == GameViewSizeMode::FreeAspect ? "Free Aspect"
	                            : gameSizeMode == GameViewSizeMode::AspectRatio
	                                ? (gamePresetWidth == 16 && gamePresetHeight == 9    ? "16:9"
	                                   : gamePresetWidth == 16 && gamePresetHeight == 10 ? "16:10"
	                                   : gamePresetWidth == 4 && gamePresetHeight == 3   ? "4:3"
	                                                                                     : "9:16")
	                                : fixedLabel.c_str();
	const Float32 toolbarSpacing = ImGui::GetStyle().ItemSpacing.x;
	const Float32 compactButtonWidth = ImGui::GetFrameHeight();
	const bool hasCustomSelection = selectedCustom != customPresets.end();
	const Float32 postScaleControlsWidth = compactButtonWidth * 4.f + toolbarSpacing * 4.f;
	const Float32 customActionsWidth = hasCustomSelection ? compactButtonWidth * 2.f + toolbarSpacing * 2.f : 0.f;
	constexpr Float32 scaleLabelWidth = 40.f;
	constexpr Float32 scaleValueWidth = 40.f;
	const Float32 availableToolbarWidth = ImGui::GetContentRegionAvail().x;
	const Float32 selectedValueWidth = ImGui::CalcTextSize(selectedLabel).x +
	                                   ImGui::GetStyle().FramePadding.x * 2.f + compactButtonWidth;
	const Float32 availableResolutionWidth =
	    availableToolbarWidth - scaleLabelWidth - scaleValueWidth - toolbarSpacing * 2.f - postScaleControlsWidth -
	    customActionsWidth;
	const Float32 resolutionWidth = std::max(selectedValueWidth, std::min(220.f, availableResolutionWidth));
	ImGui::SetNextItemWidth(resolutionWidth);
	if (ImGui::BeginCombo("##GameResolution", selectedLabel))
	{
		if (ImGui::Selectable("Free Aspect", gameSizeMode == GameViewSizeMode::FreeAspect))
		{
			gameSizeMode = GameViewSizeMode::FreeAspect;
			selectedCustomPreset.clear();
			gameFit = true;
			gamePan = {};
			settingsChanged = true;
		}
		const char* ratios[] = {"16:9", "16:10", "4:3", "9:16"};
		const UInt32 widths[] = {16, 16, 4, 9};
		const UInt32 heights[] = {9, 10, 3, 16};
		for (int index = 0; index < 4; ++index)
		{
			if (ImGui::Selectable(ratios[index], gameSizeMode == GameViewSizeMode::AspectRatio &&
			                                         gamePresetWidth == widths[index] &&
			                                         gamePresetHeight == heights[index]))
			{
				gameSizeMode = GameViewSizeMode::AspectRatio;
				gamePresetWidth = widths[index];
				gamePresetHeight = heights[index];
				selectedCustomPreset.clear();
				gameFit = true;
				gamePan = {};
				settingsChanged = true;
			}
		}
		const UInt32 resolutions[][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {1080, 1920}};
		for (const auto& resolution : resolutions)
		{
			const String label =
			    ToUrsaString(std::to_string(resolution[0])) + "x" + ToUrsaString(std::to_string(resolution[1]));
			if (ImGui::Selectable(label.c_str(), gameSizeMode == GameViewSizeMode::FixedResolution &&
			                                         gamePresetWidth == resolution[0] &&
			                                         gamePresetHeight == resolution[1]))
			{
				gameSizeMode = GameViewSizeMode::FixedResolution;
				gamePresetWidth = resolution[0];
				gamePresetHeight = resolution[1];
				selectedCustomPreset.clear();
				gameFit = true;
				gamePan = {};
				settingsChanged = true;
			}
		}
		if (!customPresets.empty())
		{
			ImGui::Separator();
		}
		for (const GameViewPreset& preset : customPresets)
		{
			const String label = preset.name + "##" + preset.id;
			if (ImGui::Selectable(label.c_str(), selectedCustomPreset == preset.id))
			{
				selectedCustomPreset = preset.id;
				gameSizeMode = preset.mode;
				gamePresetWidth = preset.width;
				gamePresetHeight = preset.height;
				gameFit = true;
				gamePan = {};
				settingsChanged = true;
			}
		}
		ImGui::Separator();
		if (Icons::MenuItem("Add Custom Resolution..."))
		{
			customPresetEditingId.clear();
			customPresetName.fill(0);
			customPresetWidth = 1280;
			customPresetHeight = 720;
			customPresetMode = 0;
			editCustomPreset = true;
			requestOpenCustomPreset = true;
		}
		ImGui::EndCombo();
	}
	if (!selectedCustomPreset.empty())
	{
		Icons::SameLine();
		if (Icons::Button("EditGamePreset", Icons::Icon::Rename, "Edit custom resolution"))
		{
			auto found = std::find_if(customPresets.begin(), customPresets.end(), [this](const GameViewPreset& preset)
			                          { return preset.id == selectedCustomPreset; });
			if (found != customPresets.end())
			{
				customPresetEditingId = found->id;
				std::memcpy(customPresetName.data(), found->name.data(),
				            std::min(found->name.size(), customPresetName.size() - 1));
				customPresetWidth = static_cast<Int32>(found->width);
				customPresetHeight = static_cast<Int32>(found->height);
				customPresetMode = found->mode == GameViewSizeMode::AspectRatio ? 1 : 0;
				editCustomPreset = true;
				requestOpenCustomPreset = true;
			}
		}
		Icons::SameLine();
		if (Icons::Button("DeleteGamePreset", Icons::Icon::Delete, "Delete custom resolution"))
		{
			customPresets.erase(std::remove_if(customPresets.begin(), customPresets.end(),
			                                   [this](const GameViewPreset& preset)
			                                   { return preset.id == selectedCustomPreset; }),
			                    customPresets.end());
			selectedCustomPreset.clear();
			gameSizeMode = GameViewSizeMode::FreeAspect;
			gameFit = true;
			gamePan = {};
			presetsChanged = true;
			settingsChanged = true;
		}
	}
	if (requestOpenCustomPreset)
	{
		ImGui::OpenPopup("Custom Game Resolution");
	}
	if (ImGui::BeginPopupModal("Custom Game Resolution", &editCustomPreset, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("Name", customPresetName.data(), customPresetName.size());
		const char* modes[] = {"Fixed Resolution", "Aspect Ratio"};
		ImGui::Combo("Type", &customPresetMode, modes, 2);
		ImGui::InputInt("Width", &customPresetWidth);
		ImGui::InputInt("Height", &customPresetHeight);
		if (Icons::Button("Save"))
		{
			const auto capabilities = device->Capabilities();
			const auto valid = ResolveViewportSize(
			    {static_cast<UInt32>(std::max(1, customPresetWidth)),
			     static_cast<UInt32>(std::max(1, customPresetHeight))},
			    GameViewSizeMode::FixedResolution, static_cast<UInt32>(std::max(0, customPresetWidth)),
			    static_cast<UInt32>(std::max(0, customPresetHeight)), capabilities.maxTextureSize, 16'777'216);
			if (!customPresetName[0] || customPresetWidth <= 0 || customPresetHeight <= 0 || !valid)
			{
				diagnostic = "Custom resolution needs a name and positive dimensions within the device limits.";
			}
			else
			{
				GameViewPreset preset;
				if (customPresetEditingId.empty())
				{
					UInt32 sequence = 1;
					while (std::find_if(customPresets.begin(), customPresets.end(),
					                    [sequence](const GameViewPreset& item)
					                    { return item.id == ToUrsaString("custom-" + std::to_string(sequence)); }) !=
					       customPresets.end())
					{
						++sequence;
					}
					preset.id = ToUrsaString("custom-" + std::to_string(sequence));
				}
				else
				{
					preset.id = customPresetEditingId;
				}
				preset.name = ToUrsaString(customPresetName.data());
				preset.mode = customPresetMode == 1 ? GameViewSizeMode::AspectRatio : GameViewSizeMode::FixedResolution;
				preset.width = static_cast<UInt32>(customPresetWidth);
				preset.height = static_cast<UInt32>(customPresetHeight);
				if (customPresetEditingId.empty())
				{
					customPresets.push_back(preset);
				}
				else
				{
					auto found =
					    std::find_if(customPresets.begin(), customPresets.end(),
					                 [this](const GameViewPreset& item) { return item.id == customPresetEditingId; });
					if (found != customPresets.end())
					{
						*found = preset;
					}
				}
				selectedCustomPreset = preset.id;
				gameSizeMode = preset.mode;
				gamePresetWidth = preset.width;
				gamePresetHeight = preset.height;
				gameFit = true;
				gamePan = {};
				presetsChanged = true;
				settingsChanged = true;
				editCustomPreset = false;
				ImGui::CloseCurrentPopup();
			}
		}
		Icons::SameLine();
		if (Icons::Button("Cancel"))
		{
			editCustomPreset = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	Icons::SameLine();
	ImGui::TextDisabled("Scale");
	Icons::SameLine();
	const Float32 scaleWidth = std::max(
	    30.f, std::min(150.f, ImGui::GetContentRegionAvail().x - postScaleControlsWidth - scaleValueWidth - toolbarSpacing));
	const ImVec2 scaleDpi = CurrentViewportFramebufferScale();
	const ImVec2 gameContent = ImGui::GetContentRegionAvail();
	const Float32 viewHeight = std::max(0.f, gameContent.y - ImGui::GetFrameHeightWithSpacing());
	const ViewportScaleLimits scaleLimits = GetViewportScaleLimits(
	    {0.f, 0.f, gameContent.x, viewHeight}, scaleDpi.x, gameTarget.size);
	ImGui::SetNextItemWidth(scaleWidth);
	if (ImGui::SliderFloat("##GameScale", &gameScale, scaleLimits.minimum, scaleLimits.maximum, "",
	                       ImGuiSliderFlags_Logarithmic))
	{
		gameScale = SnapViewportScale(gameScale, scaleLimits.minimum, scaleLimits.maximum);
		gameScaleSnapPending = false;
		gameFit = false;
		settingsChanged = true;
	}
	Icons::SameLine();
	ImGui::Text("%.2gx", gameScale);
	Icons::SameLine();
	if (Icons::Button("Fit", Icons::Icon::Fit, "Fit game view"))
	{
		gameFit = true;
		gameScaleSnapPending = false;
		gamePan = {};
		settingsChanged = true;
	}
	Icons::SameLine();
	if (Icons::Button("GameStats", Icons::Icon::Info, "Statistics", showGameStats))
	{
		showGameStats = !showGameStats;
		settingsChanged = true;
	}
	Icons::SameLine();
	if (Icons::Button("GameGizmos", Icons::Icon::Debug, "Gizmos", showGameGizmos))
	{
		showGameGizmos = !showGameGizmos;
		settingsChanged = true;
	}
	Icons::SameLine();
	if (Icons::Button("GamePlayMode", Icons::Icon::Settings, "Play Mode behavior"))
	{
		ImGui::OpenPopup("Game Play Mode");
	}
	if (ImGui::BeginPopup("Game Play Mode"))
	{
		if (ImGui::MenuItem("Focused", nullptr, &focusGameViewOnPlay))
		{
			settingsChanged = true;
		}
		if (ImGui::MenuItem("Unity Shortcuts", nullptr, &disableEditorShortcutsInPlay))
		{
			settingsChanged = true;
		}
		ImGui::Separator();
		ImGui::TextDisabled("Enter Play Mode");
		if (ImGui::MenuItem("Normally", nullptr, !maximizeGameViewOnPlay))
		{
			maximizeGameViewOnPlay = false;
			settingsChanged = true;
		}
		if (ImGui::MenuItem("Maximized", nullptr, &maximizeGameViewOnPlay))
		{
			settingsChanged = true;
		}
		ImGui::EndPopup();
	}
}

Result<void> SceneViewPanel::Draw(SceneDraft& draft, EditorDocument& document, EditorHierarchy& hierarchy,
                                  PlaySession& play, VirtualFileSystem* userFiles, const String& projectKey,
                                  bool& showScene, bool& showGame, bool& viewportFocused, String& diagnostic,
                                  const std::function<Result<void>(const EditorDragSession&, Vec3)>& placeAssets,
                                  ImGuiViewport** focusedGameViewport, EditorSelectionContext* selection)
{
	settingsChanged = false;
	presetsChanged = false;
	bool sceneGizmoSelectionHandled = false;
	const bool focusGameOnThisPlayStart = play.Active() && !gameWasPlaying && focusGameViewOnPlay && showGame;
	if (userFiles && userFiles->IsMounted("user"))
	{
		LoadSettings(*userFiles, projectKey, diagnostic);
	}
	viewportFocused = false;
	if (focusedGameViewport)
	{
		*focusedGameViewport = nullptr;
	}
	sceneTarget.visible = false;
	gameTarget.visible = false;
	if (!showScene)
	{
		sceneNavigationGesture = SceneNavigationGesture::None;
		SetSceneCursorCaptured(false);
	}
	if (sceneCursorCaptured && (!sceneNavigationWindow || !sceneNavigationWindow->Focused()))
	{
		SetSceneCursorCaptured(false);
		sceneNavigationGesture = SceneNavigationGesture::None;
	}
	Scene& scene = play.Active() ? *play.GetScene() : draft.GetScene();
	if (sceneTarget.preview)
	{
		Camera& camera = sceneTarget.preview->GetCamera();
		camera.verticalFovRadians = sceneCameraFieldOfView * (3.14159265358979323846f / 180.f);
		camera.nearPlane = sceneCameraNearClip;
		camera.farPlane = sceneCameraFarClip;
		AdvanceFocusTransition(camera, ImGui::GetIO().DeltaTime);
	}
	const String sceneViewStateKey = projectKey + "/" + document.Id().ToString();
	if (loadedSceneViewStateKey != sceneViewStateKey)
	{
		loadedSceneViewStateKey = sceneViewStateKey;
		const String statePath = "user:Settings/SceneView/" + sceneViewStateKey + ".json";
		auto path = VirtualPath::Parse(statePath);
		if (!path)
		{
			diagnostic = path.error;
		}
		else
		{
			String stateText = "{\"version\":1,\"visibility\":[],\"pickability\":[]}";
			if (userFiles && userFiles->IsMounted("user"))
			{
				auto text = userFiles->ReadText(path.value);
				if (text)
				{
					stateText = std::move(text.value);
				}
			}
			auto restored = hierarchy.RestoreSceneViewOverrides(scene, stateText);
			if (!restored)
			{
				diagnostic = restored.error;
			}
		}
		savedSceneViewOverridesRevision = hierarchy.SceneViewOverridesRevision();
	}
	if (showScene)
	{
		if (ImGui::Begin("Scene", &showScene))
		{
			const ImVec2 dpi = CurrentViewportFramebufferScale();
			if (ImGui::BeginPopupContextWindow("ScenePanelActions", ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Close Panel"))
				{
					showScene = false;
				}
				ImGui::EndPopup();
			}
			ImGui::BeginChild("SceneToolRail", ImVec2(40.f, 0.f), true,
			                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			if (Icons::Button("Hand", Icons::Icon::Move, "Hand tool (Q)", sceneHandTool))
			{
				sceneHandTool = true;
			}
			if (Icons::Button("Translate", Icons::Icon::Move, "Move (W)",
			                  !sceneHandTool && gizmoOperation == GizmoOperation::Translate))
			{
				sceneHandTool = false;
				gizmoOperation = GizmoOperation::Translate;
			}
			if (Icons::Button("Rotate", Icons::Icon::Rotate, "Rotate (E)",
			                  !sceneHandTool && gizmoOperation == GizmoOperation::Rotate))
			{
				sceneHandTool = false;
				gizmoOperation = GizmoOperation::Rotate;
			}
			if (Icons::Button("Scale", Icons::Icon::Scale, "Scale (R)",
			                  !sceneHandTool && gizmoOperation == GizmoOperation::Scale))
			{
				sceneHandTool = false;
				gizmoOperation = GizmoOperation::Scale;
			}
			ImGui::EndChild();
			Icons::SameLine();
			ImGui::BeginChild("SceneViewportArea", ImVec2(0.f, 0.f), false,
			                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			bool requestedTwoDMode = sceneView2DMode;
			if (ImGui::Checkbox("2D", &requestedTwoDMode))
			{
				sceneView2DModePending = true;
				sceneView2DModePendingTarget = requestedTwoDMode;
			}
			Icons::SameLine();
			if (ImGui::Checkbox("Grid", &showSceneGrid))
			{
				settingsChanged = true;
			}
			Icons::SameLine();
			ImGui::SetNextItemWidth(64.f);
			if (ImGui::DragFloat("##SceneGridSize", &sceneGridSize, 0.01f, 0.0001f, 10000.f, "%.3g"))
			{
				settingsChanged = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Grid size");
			}
			Icons::SameLine();
			const ImGuiIO& sceneSearchInput = ImGui::GetIO();
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !sceneSearchInput.WantTextInput &&
			    sceneSearchInput.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
			{
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(std::min(180.f, std::max(110.f, ImGui::GetContentRegionAvail().x * 0.34f)));
			ImGui::InputTextWithHint("##SceneSearch", "Search (name or type)", hierarchy.SearchText().data(),
			                         hierarchy.SearchText().size(), ImGuiInputTextFlags_EscapeClearsAll);
			if (draft.Kind() == ResourceKind::Scene)
			{
				hierarchy.RefreshSceneSearchMatches(scene, document.Id());
			}
			Icons::SameLine();
			if (Icons::SmallButton(gizmoPivotMode == GizmoPivotMode::Pivot ? "Pivot" : "Center"))
			{
				gizmoPivotMode =
				    gizmoPivotMode == GizmoPivotMode::Pivot ? GizmoPivotMode::Center : GizmoPivotMode::Pivot;
			}
			Icons::SameLine();
			if (Icons::SmallButton(gizmoSpace == GizmoSpace::Global ? "Global" : "Local"))
			{
				gizmoSpace = gizmoSpace == GizmoSpace::Global ? GizmoSpace::Local : GizmoSpace::Global;
			}
			Icons::SameLine();
			if (Icons::Button("Snap", Icons::Icon::Trace, "Toggle transform snapping (Ctrl temporarily snaps)",
			                  gizmoSnapSettings.enabled))
			{
				gizmoSnapSettings.enabled = !gizmoSnapSettings.enabled;
				settingsChanged = true;
			}
			Icons::SameLine();
			if (Icons::SmallButton("Snap Settings"))
			{
				ImGui::OpenPopup("Scene Snap Settings");
			}
			if (ImGui::BeginPopup("Scene Snap Settings"))
			{
				Float32 moveSnap[3]{gizmoSnapSettings.moveX, gizmoSnapSettings.moveY, gizmoSnapSettings.moveZ};
				if (ImGui::DragFloat3("Move Increment", moveSnap, 0.1f, 0.001f, 10000.f, "%.3f"))
				{
					gizmoSnapSettings.moveX = moveSnap[0];
					gizmoSnapSettings.moveY = moveSnap[1];
					gizmoSnapSettings.moveZ = moveSnap[2];
					settingsChanged = true;
				}
				if (ImGui::DragFloat("Rotate Increment", &gizmoSnapSettings.rotation, 0.5f, 0.1f, 180.f, "%.2f°"))
				{
					settingsChanged = true;
				}
				if (ImGui::DragFloat("Scale Increment", &gizmoSnapSettings.scale, 0.01f, 0.001f, 100.f, "%.3f"))
				{
					settingsChanged = true;
				}
				if (Icons::Button("Reset Snap Values"))
				{
					gizmoSnapSettings.moveX = 1.f;
					gizmoSnapSettings.moveY = 1.f;
					gizmoSnapSettings.moveZ = 1.f;
					gizmoSnapSettings.rotation = 15.f;
					gizmoSnapSettings.scale = 0.1f;
					settingsChanged = true;
				}
				ImGui::EndPopup();
			}
			Icons::SameLine();
			if (Icons::Button("SceneGizmos", Icons::Icon::Debug, "Gizmos", showSceneGizmos, true))
			{
				showSceneGizmos = !showSceneGizmos;
				settingsChanged = true;
			}
			Icons::SameLine();
			if (ImGui::ArrowButton("##SceneGizmoSettings", ImGuiDir_Down))
			{
				ImGui::OpenPopup("Scene Gizmo Settings");
			}
			if (ImGui::BeginPopup("Scene Gizmo Settings"))
			{
				settingsChanged = ImGui::Checkbox("Cameras", &showSceneCameraGizmos) || settingsChanged;
				settingsChanged = ImGui::Checkbox("Lights", &showSceneLightGizmos) || settingsChanged;
				settingsChanged = ImGui::Checkbox("Labels", &showSceneGizmoLabels) || settingsChanged;
				settingsChanged = ImGui::Checkbox("3D Icons", &sceneGizmo3DIcons) || settingsChanged;
				if (sceneGizmo3DIcons)
				{
					settingsChanged = ImGui::SliderFloat("Icon Size", &sceneGizmoIconSize, 0.1f, 3.f, "%.1fx") ||
					                   settingsChanged;
				}
				ImGui::EndPopup();
			}
			Icons::SameLine();
			if (Icons::SmallButton("Camera"))
			{
				ImGui::OpenPopup("Scene Camera Settings");
			}
			if (ImGui::BeginPopup("Scene Camera Settings"))
			{
				ImGui::TextUnformatted("Scene Camera");
				Float32 fieldOfView = sceneCameraFieldOfView;
				if (ImGui::SliderFloat("Field of View", &fieldOfView, 1.f, 179.f, "%.1f°"))
				{
					sceneCameraFieldOfView = fieldOfView;
					sceneTarget.preview->GetCamera().verticalFovRadians =
					    fieldOfView * (3.14159265358979323846f / 180.f);
					settingsChanged = true;
				}
				Float32 nearClip = sceneCameraNearClip;
				if (ImGui::DragFloat("Near", &nearClip, 0.01f, 0.01f, 99999.f, "%.2f"))
				{
					sceneCameraNearClip = nearClip;
					if (sceneCameraFarClip <= sceneCameraNearClip)
					{
						sceneCameraFarClip = sceneCameraNearClip + 0.01f;
					}
					sceneTarget.preview->GetCamera().nearPlane = sceneCameraNearClip;
					sceneTarget.preview->GetCamera().farPlane = sceneCameraFarClip;
					settingsChanged = true;
				}
				Float32 farClip = sceneCameraFarClip;
				if (ImGui::DragFloat("Far", &farClip, 1.f, sceneCameraNearClip + 0.01f, 1000000.f, "%.2f"))
				{
					sceneCameraFarClip = farClip;
					sceneTarget.preview->GetCamera().farPlane = sceneCameraFarClip;
					settingsChanged = true;
				}
				ImGui::Separator();
				ImGui::SetNextItemWidth(190.f);
				if (ImGui::SliderFloat("Camera Speed", &sceneFlythroughSpeed, sceneFlythroughMinSpeed,
				                       sceneFlythroughMaxSpeed, "%.2f"))
				{
					settingsChanged = true;
				}
				Float32 minimumSpeed = sceneFlythroughMinSpeed;
				if (ImGui::DragFloat("Min", &minimumSpeed, 0.01f, 0.0001f, 9999.f, "%.4f"))
				{
					sceneFlythroughMinSpeed = minimumSpeed;
					if (sceneFlythroughMinSpeed >= sceneFlythroughMaxSpeed)
					{
						sceneFlythroughMaxSpeed = std::min(10000.f, sceneFlythroughMinSpeed + 0.01f);
					}
					sceneFlythroughSpeed =
					    std::clamp(sceneFlythroughSpeed, sceneFlythroughMinSpeed, sceneFlythroughMaxSpeed);
					settingsChanged = true;
				}
				Float32 maximumSpeed = sceneFlythroughMaxSpeed;
				if (ImGui::DragFloat("Max", &maximumSpeed, 0.01f, 0.001f, 10000.f, "%.4f"))
				{
					sceneFlythroughMaxSpeed = maximumSpeed;
					if (sceneFlythroughMaxSpeed <= sceneFlythroughMinSpeed)
					{
						sceneFlythroughMinSpeed = std::max(0.0001f, sceneFlythroughMaxSpeed - 0.01f);
					}
					sceneFlythroughSpeed =
					    std::clamp(sceneFlythroughSpeed, sceneFlythroughMinSpeed, sceneFlythroughMaxSpeed);
					settingsChanged = true;
				}
				if (Icons::Button("Reset Camera Settings"))
				{
					sceneFlythroughSpeed = 1.f;
					sceneFlythroughMinSpeed = 0.01f;
					sceneFlythroughMaxSpeed = 2.f;
					sceneCameraFieldOfView = 45.f;
					sceneCameraNearClip = 0.1f;
					sceneCameraFarClip = 100.f;
					sceneTarget.preview->GetCamera().verticalFovRadians = 45.f * (3.14159265358979323846f / 180.f);
					sceneTarget.preview->GetCamera().nearPlane = sceneCameraNearClip;
					sceneTarget.preview->GetCamera().farPlane = sceneCameraFarClip;
					settingsChanged = true;
				}
				ImGui::EndPopup();
			}
			const ImGuiIO& sceneInput = ImGui::GetIO();
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !sceneInput.WantTextInput &&
			    !sceneInput.KeyCtrl && !sceneInput.KeySuper && !sceneInput.KeyAlt && !sceneInput.KeyShift &&
			    !sceneInput.MouseDown[ImGuiMouseButton_Right])
			{
				if (ImGui::IsKeyPressed(ImGuiKey_2, false))
				{
					sceneView2DModePending = true;
					sceneView2DModePendingTarget = !sceneView2DMode;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
				{
					sceneHandTool = true;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
				{
					sceneHandTool = false;
					gizmoOperation = GizmoOperation::Translate;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
				{
					sceneHandTool = false;
					gizmoOperation = GizmoOperation::Rotate;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
				{
					sceneHandTool = false;
					gizmoOperation = GizmoOperation::Scale;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
				{
					gizmoPivotMode =
					    gizmoPivotMode == GizmoPivotMode::Pivot ? GizmoPivotMode::Center : GizmoPivotMode::Pivot;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_X, false))
				{
					gizmoSpace = gizmoSpace == GizmoSpace::Global ? GizmoSpace::Local : GizmoSpace::Global;
				}
			}
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const ImVec2 position = ImGui::GetCursorScreenPos();
			const UInt32 width =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.x * dpi.x))));
			const UInt32 height =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.y * dpi.y))));
			if (width && height)
			{
				auto target = EnsureTarget(sceneTarget, {width, height});
				if (!target)
				{
					diagnostic = target.error;
				}
				else if (sceneView2DModePending && sceneTarget.preview &&
				         SetSceneView2DMode(sceneTarget.preview->GetCamera(), sceneView2DMode,
				                           sceneView2DModePendingTarget))
				{
					sceneView2DModePending = false;
					sceneViewLockedTarget = {};
					sceneViewLockedDocument = {};
					sceneViewLockedPositionOffset = {};
					sceneViewLockedTargetOffset = {};
					sceneViewLockedPlaySession = false;
				}
			}
			if (sceneTarget.color && width && height)
			{
				sceneTarget.visible = true;
				const ViewportRect content{position.x, position.y, available.x, available.y};
				const auto layout = ComputeViewportLayout(content, dpi.x, sceneTarget.size, 1, {}, true);
				sceneLayout = layout;
				const Float32 clipX = std::max(content.x, layout.displayRect.x);
				const Float32 clipY = std::max(content.y, layout.displayRect.y);
				ImGui::PushClipRect({clipX, clipY}, {clipX + layout.clipRect.width, clipY + layout.clipRect.height},
				                    true);
				ImGui::SetCursorScreenPos({layout.displayRect.x, layout.displayRect.y});
				ImGui::Image(sceneTarget.imageId, {layout.displayRect.width, layout.displayRect.height});
				const Float32 sceneAspect = static_cast<Float32>(sceneTarget.size.width) /
				                            static_cast<Float32>(sceneTarget.size.height);
				if (draft.Kind() == ResourceKind::Scene && showSceneGrid)
				{
					DrawSceneGrid(ImGui::GetWindowDrawList(), sceneTarget.preview->GetCamera(), sceneAspect, layout,
					             sceneView2DMode, sceneGridSize);
				}
				if (draft.Kind() == ResourceKind::Scene && layout.visible && showSceneGizmos &&
				    (showSceneCameraGizmos || showSceneLightGizmos))
				{
					struct SceneGizmoHit
					{
					ResourceId id;
					GameObject::Ptr object;
					ViewportPoint screen;
					String label;
					bool camera = false;
						Int32 iconIndex = 0;
						Int32 iconCount = 1;
						bool occluded = false;
				};
					Vector<SceneGizmoHit> gizmos;
					const Float32 aspect = sceneAspect;
					auto* drawList = ImGui::GetWindowDrawList();
					Vector<Rendering::RenderObject> sceneGizmoOccluders;
					if (sceneGizmo3DIcons)
					{
						auto renderData = scene.CollectPreviewRenderData(sceneTarget.preview->GetCamera(), aspect);
						if (renderData)
						{
							FilterSceneViewRenderObjects(renderData.value.objects, hierarchy, false);
						sceneGizmoOccluders = std::move(renderData.value.objects);
						}
					}
					std::function<void(const GameObject::Ptr&)> drawObjectGizmos = [&](const GameObject::Ptr& object)
					{
						const auto identity = scene.PersistentId(object);
						if (identity && hierarchy.SceneObjectVisible(identity.value))
						{
							ViewportPoint screen;
							if (ProjectObjectOrigin(sceneTarget.preview->GetCamera(), aspect, layout, scene, object, screen))
							{
								auto name = scene.GameObjectName(object);
								const String objectName = name ? name.value : String("GameObject");
									auto world = scene.WorldTransform(object);
									const Camera& editorCamera = sceneTarget.preview->GetCamera();
									const bool occluded = sceneGizmo3DIcons && world &&
									                     SceneGizmoOccluded(
									                         editorCamera.position,
									                         {world.value.At(0, 3), world.value.At(1, 3), world.value.At(2, 3)},
									                         identity.value, sceneGizmoOccluders);
								Vector<SceneGizmoHit> objectGizmos;
								for (Component* component : object->Components())
								{
									if (showSceneCameraGizmos && dynamic_cast<CameraComponent*>(component))
									{
										objectGizmos.push_back({identity.value, object, screen, "Camera: " + objectName, true});
									}
									else if (showSceneLightGizmos &&
									         (dynamic_cast<DirectionalLightComponent*>(component) ||
									          dynamic_cast<PointLightComponent*>(component)))
									{
										objectGizmos.push_back({identity.value, object, screen, "Light: " + objectName, false});
									}
								}
								for (Size index = 0; index < objectGizmos.size(); ++index)
								{
									objectGizmos[index].iconIndex = static_cast<Int32>(index);
									objectGizmos[index].iconCount = static_cast<Int32>(objectGizmos.size());
									objectGizmos[index].occluded = occluded;
									gizmos.push_back(std::move(objectGizmos[index]));
								}
							}
						}
						const auto children = scene.ChildrenOf(object);
						if (children)
						{
							for (const GameObject::Ptr& child : children.value)
							{
								drawObjectGizmos(child);
							}
						}
					};
					for (const GameObject::Ptr& root : scene.RootGameObjects())
					{
						drawObjectGizmos(root);
					}
					for (const SceneGizmoHit& gizmo : gizmos)
					{
						if (gizmo.occluded)
						{
							continue;
						}
						const Float32 iconOffset =
						    (static_cast<Float32>(gizmo.iconIndex) - static_cast<Float32>(gizmo.iconCount - 1) * 0.5f) * 14.f;
						const ImVec2 center{gizmo.screen.x + iconOffset, gizmo.screen.y};
						Float32 iconScale = sceneGizmoIconSize;
						if (sceneGizmo3DIcons)
						{
							auto world = scene.WorldTransform(gizmo.object);
							if (world)
							{
								const Camera& editorCamera = sceneTarget.preview->GetCamera();
								const Float32 dx = world.value.At(0, 3) - editorCamera.position.x;
								const Float32 dy = world.value.At(1, 3) - editorCamera.position.y;
								const Float32 dz = world.value.At(2, 3) - editorCamera.position.z;
								const Float32 distance = std::sqrt(dx * dx + dy * dy + dz * dz);
								iconScale *= std::clamp(10.f / std::max(distance, 0.1f), 0.4f, 2.2f);
							}
						}
						const Float32 iconRadius = 7.f * iconScale;
						const bool hovered = ImGui::IsMouseHoveringRect(
						    {center.x - 11.f * iconScale, center.y - 11.f * iconScale},
						    {center.x + 11.f * iconScale, center.y + 11.f * iconScale});
						const ImU32 color = gizmo.camera ? IM_COL32(90, 190, 255, 255) : IM_COL32(255, 205, 80, 255);
						drawList->AddCircleFilled(center, iconRadius, IM_COL32(25, 31, 40, 235), 16);
						if (gizmo.camera)
						{
							drawList->AddRect({center.x - 5.f * iconScale, center.y - 3.5f * iconScale},
							                  {center.x + 2.f * iconScale, center.y + 3.5f * iconScale}, color,
							                  iconScale, 0, 1.5f * iconScale);
							drawList->AddTriangle({center.x + 2.f * iconScale, center.y - 2.5f * iconScale},
							                     {center.x + 6.f * iconScale, center.y - 5.f * iconScale},
							                     {center.x + 6.f * iconScale, center.y + 5.f * iconScale}, color,
							                     1.4f * iconScale);
						}
						else
						{
							drawList->AddCircle(center, 3.f * iconScale, color, 12, 1.5f * iconScale);
							for (int ray = 0; ray < 8; ++ray)
							{
								const Float32 angle = ray * 0.78539816f;
								const ImVec2 a{center.x + std::cos(angle) * 4.5f * iconScale,
								               center.y + std::sin(angle) * 4.5f * iconScale};
								const ImVec2 b{center.x + std::cos(angle) * 6.5f * iconScale,
								               center.y + std::sin(angle) * 6.5f * iconScale};
								drawList->AddLine(a, b, color, 1.3f * iconScale);
							}
						}
						if (showSceneGizmoLabels && hovered)
						{
							const ImVec2 textSize = ImGui::CalcTextSize(gizmo.label.c_str());
							const ImVec2 textPos{center.x + 9.f, center.y - textSize.y * 0.5f};
							drawList->AddRectFilled({textPos.x - 3.f, textPos.y - 2.f},
							                        {textPos.x + textSize.x + 3.f, textPos.y + textSize.y + 2.f},
							                        IM_COL32(24, 28, 35, 225), 2.f);
							drawList->AddText(textPos, IM_COL32_WHITE, gizmo.label.c_str());
						}
						if (hovered)
						{
							ImGui::SetTooltip("%s\nClick to select", gizmo.label.c_str());
							if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
							{
								sceneGizmoSelectionHandled = true;
								bool accepted = selection ? selection->SelectGameObject(document.Id(), gizmo.id, play.Active())
								                          : true;
								if (accepted)
								{
									auto selected = hierarchy.Select(scene, gizmo.object);
									if (!selected) diagnostic = selected.error;
								}
								else diagnostic = "Scene selection could not be updated";
							}
						}
					}
				}
				const auto& hierarchySearch = hierarchy.SearchText();
				if (draft.Kind() == ResourceKind::Scene && hierarchySearch[0] != '\0' &&
				    hierarchy.HasDirectSearchMatches() && layout.visible)
				{
					const Float32 aspect = static_cast<Float32>(sceneTarget.size.width) /
					                       static_cast<Float32>(sceneTarget.size.height);
					auto searchRenderData = scene.CollectPreviewRenderData(sceneTarget.preview->GetCamera(), aspect);
					Vector<ResourceId> highlightedObjects;
					auto* drawList = ImGui::GetWindowDrawList();
					if (searchRenderData)
					{
						FilterSceneViewRenderObjects(searchRenderData.value.objects, hierarchy, false);
						for (const Rendering::RenderObject& object : searchRenderData.value.objects)
						{
							if (!hierarchy.IsDirectSearchMatch(object.objectId) ||
							    std::find(highlightedObjects.begin(), highlightedObjects.end(), object.objectId) !=
							        highlightedObjects.end())
							{
								continue;
							}
							highlightedObjects.push_back(object.objectId);
							DrawProjectedBounds(drawList, searchRenderData.value.viewProjection, layout, object.worldBounds,
							                   IM_COL32(255, 166, 48, 235));
						}
					}
					std::function<void(const GameObject::Ptr&)> drawSearchMarker = [&](const GameObject::Ptr& object)
					{
						const auto identity = scene.PersistentId(object);
						if (identity && hierarchy.IsDirectSearchMatch(identity.value) &&
						    hierarchy.SceneObjectVisible(identity.value) &&
						    std::find(highlightedObjects.begin(), highlightedObjects.end(), identity.value) ==
						        highlightedObjects.end())
						{
							ViewportPoint screen;
							if (ProjectObjectOrigin(sceneTarget.preview->GetCamera(), aspect, layout, scene, object, screen))
							{
								drawList->AddCircleFilled({screen.x, screen.y}, 7.f, IM_COL32(255, 166, 48, 90), 16);
								drawList->AddCircle({screen.x, screen.y}, 7.f, IM_COL32(255, 190, 92, 245), 16, 2.f);
							}
						}
						const auto children = scene.ChildrenOf(object);
						if (children)
						{
							for (const GameObject::Ptr& child : children.value)
							{
								drawSearchMarker(child);
							}
						}
					};
					for (const GameObject::Ptr& root : scene.RootGameObjects())
					{
						drawSearchMarker(root);
					}
				}
				const bool imageHovered = ImGui::IsItemHovered();
				bool sceneOrientationHovered = false;
				if (layout.displayRect.width >= 100.f && layout.displayRect.height >= 100.f &&
				    DrawSceneOrientationGizmo(
				        sceneTarget.preview->GetCamera(),
				        {layout.displayRect.x, layout.displayRect.y},
				        {layout.displayRect.x + layout.displayRect.width, layout.displayRect.y + layout.displayRect.height},
				        sceneOrientationGizmoOffsetX,
				        sceneOrientationGizmoOffsetY,
				        sceneOrientationGizmoDragging,
				        sceneView2DMode,
				        sceneViewRotationLocked,
				        showSceneOrientationBackground,
				        settingsChanged,
				        sceneOrientationHovered))
				{
					sceneViewLockedTarget = {};
					sceneViewLockedDocument = {};
					sceneViewLockedPositionOffset = {};
					sceneViewLockedTargetOffset = {};
					sceneViewLockedPlaySession = false;
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					        editorDragSessionPayloadType,
					        ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptBeforeDelivery))
					{
						auto drag = DecodeEditorDragPayload(payload->DataType, payload->Data,
						                                    static_cast<Size>(payload->DataSize));
						if (!drag)
						{
							if (payload->IsDelivery())
							{
								diagnostic = drag.error;
							}
						}
						else if (!play.Active() && draft.Kind() == ResourceKind::Scene && placeAssets)
						{
							const ImVec2 mouse = ImGui::GetIO().MousePos;
							const Float32 aspect = static_cast<Float32>(sceneTarget.size.width) /
							                       static_cast<Float32>(sceneTarget.size.height);
							auto renderData = scene.CollectPreviewRenderData(sceneTarget.preview->GetCamera(), aspect);
							if (renderData)
							{
								FilterSceneViewRenderObjects(renderData.value.objects, hierarchy, false);
							}
							auto dropPoint =
							    renderData ? ResolveSceneDropPoint(sceneTarget.preview->GetCamera(), aspect, layout,
							                                       {mouse.x, mouse.y}, renderData.value.objects)
							               : Result<SceneDropPoint>::Failure(renderData.error);
							if (dropPoint)
							{
								const Vec4 clip = Transform(
								    sceneTarget.preview->GetCamera().ViewProjection(aspect),
								    {dropPoint.value.world.x, dropPoint.value.world.y, dropPoint.value.world.z, 1.f});
								if (std::isfinite(clip.w) && std::abs(clip.w) > 1.0e-7f)
								{
									const ImVec2 previewPoint{
									    layout.displayRect.x +
									        (clip.x / clip.w + 1.f) * layout.displayRect.width * 0.5f,
									    layout.displayRect.y +
									        (1.f - clip.y / clip.w) * layout.displayRect.height * 0.5f};
									auto* drawList = ImGui::GetWindowDrawList();
									drawList->AddCircleFilled(previewPoint, 6.f,
									                          ImGui::GetColorU32(ImGuiCol_DragDropTarget), 16);
									drawList->AddCircle(previewPoint, 9.f, ImGui::GetColorU32(ImGuiCol_Text), 16, 1.5f);
								}
								if (ClaimEditorDragDropDelivery(payload))
								{
									auto placed = placeAssets(drag.value, dropPoint.value.world);
									diagnostic = placed ? String{} : placed.error;
								}
							}
							else if (payload->IsDelivery())
							{
								diagnostic = dropPoint.error;
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
				const ImGuiIO& scenePointerInput = ImGui::GetIO();
				if (imageHovered && scenePointerInput.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
				    !gizmoWasOver && !gizmoWasUsing)
				{
					const Float32 aspect =
					    static_cast<Float32>(sceneTarget.size.width) / static_cast<Float32>(sceneTarget.size.height);
					auto renderData = scene.CollectPreviewRenderData(sceneTarget.preview->GetCamera(), aspect);
					if (renderData)
					{
						FilterSceneViewRenderObjects(renderData.value.objects, hierarchy, true);
					}
					Vector<ResourceId> overlapping;
					auto picked = renderData
					                  ? PickSceneObject(sceneTarget.preview->GetCamera(), aspect, layout,
					                                    {scenePointerInput.MousePos.x, scenePointerInput.MousePos.y},
					                                    renderData.value.objects, scene, hierarchy, &overlapping)
					                  : Result<ResourceId>::Failure(renderData.error);
					scenePiercingCandidates = picked ? std::move(overlapping) : Vector<ResourceId>{};
					scenePiercingAddSelection = scenePointerInput.KeyShift;
					if (scenePiercingCandidates.size() > 1)
					{
						sceneNavigationGesture = SceneNavigationGesture::None;
						ImGui::OpenPopup("Scene Selection Piercing");
					}
				}
				if (!sceneOrientationHovered && !scenePointerInput.KeyCtrl &&
				    ImGui::BeginPopupContextItem("SceneViewportActions"))
				{
					const bool canCreate = !play.Active() && draft.Kind() == ResourceKind::Scene;
					if (ImGui::MenuItem("Create Empty", nullptr, false, canCreate))
					{
						auto created = scene.CreateGameObject("GameObject");
						if (created)
						{
							auto selectedCreated = hierarchy.Select(scene, created.value);
							document.MarkDirty();
							diagnostic = selectedCreated ? String{} : selectedCreated.error;
						}
						else
						{
							diagnostic = created.error;
						}
					}
					ImGui::EndPopup();
				}
				if (ImGui::IsPopupOpen("Scene Selection Piercing"))
				{
					ImGui::SetNextWindowPos(scenePointerInput.MousePos, ImGuiCond_Appearing);
				}
				if (ImGui::BeginPopup("Scene Selection Piercing"))
				{
					for (const ResourceId id : scenePiercingCandidates)
					{
						auto object = scene.GameObjectById(id);
						if (!object)
						{
							continue;
						}
						auto name = scene.GameObjectName(object.value);
						std::string label = name ? ToStdString(name.value) : "GameObject";
						label += "##" + ToStdString(id.ToString());
						if (ImGui::MenuItem(label.c_str()))
						{
							bool accepted = true;
							if (selection)
							{
								EditorSelectionEntry target;
								target.kind = EditorSelectionKind::GameObject;
								target.documentId = document.Id();
								target.objectId = id;
								target.playSession = play.Active();
								if (scenePiercingAddSelection)
								{
									Vector<EditorSelectionEntry> next;
									for (const auto& entry : selection->entries)
									{
										if (entry.kind == EditorSelectionKind::GameObject &&
										    entry.documentId == document.Id() && entry.playSession == play.Active() &&
										    !entry.IsMissing())
										{
											next.push_back(entry);
										}
									}
									if (std::none_of(next.begin(), next.end(),
									                 [&](const auto& entry) { return entry.SameIdentity(target); }))
									{
										next.push_back(target);
									}
									const auto primary = std::find_if(next.begin(), next.end(), [&](const auto& entry)
									                                  { return entry.SameIdentity(target); });
									const Size primaryIndex = static_cast<Size>(std::distance(next.begin(), primary));
									accepted = selection->SelectEntries(std::move(next), primaryIndex, target);
								}
								else
								{
									accepted = selection->SelectGameObject(document.Id(), id, play.Active());
								}
								const auto* primary = selection->PrimaryEntry();
								if (accepted && primary && primary->kind == EditorSelectionKind::GameObject &&
								    primary->documentId == document.Id() && primary->playSession == play.Active())
								{
									auto primaryObject = scene.GameObjectById(primary->objectId);
									if (primaryObject)
									{
										auto synced = hierarchy.Select(scene, primaryObject.value);
										accepted = static_cast<bool>(synced);
									}
								}
							}
							else
							{
								auto synced = hierarchy.Select(scene, object.value);
								accepted = static_cast<bool>(synced);
							}
							if (!accepted)
							{
								diagnostic = "Scene selection could not be updated";
							}
						}
					}
					ImGui::EndPopup();
				}
				ImGui::PopClipRect();
				GameObject::Ptr gizmoTarget;
				Vector<GameObject::Ptr> gizmoTargets;
				ResourceId prefabObjectId;
				if (!play.Active() && draft.Kind() == ResourceKind::Prefab && draft.GetPrefabAsset())
				{
					auto selectedAssetObject = hierarchy.SelectedGameObject(*draft.GetPrefabAsset());
					if (selectedAssetObject)
					{
						prefabObjectId = selectedAssetObject.value->Id();
						auto selectedPreviewObject = draft.PreviewGameObject(prefabObjectId);
						if (selectedPreviewObject)
						{
							gizmoTarget = selectedPreviewObject.value;
							gizmoTargets.push_back(gizmoTarget);
						}
					}
				}
				else
				{
					auto selected = hierarchy.SelectedGameObject(scene);
					if (selected)
					{
						gizmoTarget = selected.value;
						if (selection && selection->kind == EditorSelectionKind::GameObject)
						{
							for (const auto& entry : selection->entries)
							{
								if (entry.kind != EditorSelectionKind::GameObject || entry.IsMissing() ||
								    entry.documentId != document.Id() || entry.playSession != play.Active())
								{
									continue;
								}
								auto object = scene.GameObjectById(entry.objectId);
								if (object && std::none_of(gizmoTargets.begin(), gizmoTargets.end(),
								                           [&](const GameObject::Ptr& item)
								                           { return item->Id() == object.value->Id(); }))
								{
									gizmoTargets.push_back(object.value);
								}
							}
						}
						if (std::none_of(gizmoTargets.begin(), gizmoTargets.end(),
						                 [&](const GameObject::Ptr& item) { return item->Id() == gizmoTarget->Id(); }))
						{
							gizmoTargets.push_back(gizmoTarget);
						}
					}
				}
				if (gizmoTarget && layout.visible && !sceneHandTool)
				{
					GizmoInteractionReport interaction;
					auto manipulated =
					    DrawGizmo(scene, gizmoTargets, gizmoTarget, sceneTarget.preview->GetCamera(),
					              {layout.displayRect.x, layout.displayRect.y, layout.displayRect.width,
					               layout.displayRect.height},
					              gizmoOperation, gizmoPivotMode, gizmoSpace, gizmoSnapSettings, &interaction);
					if (interaction.isOver != gizmoWasOver || interaction.isUsing != gizmoWasUsing ||
					    interaction.matrixChanged)
					{
						const ImVec2 mouse = ImGui::GetIO().MousePos;
						const std::string message = "Scene Gizmo object=" + ToStdString(gizmoTarget->Id().ToString()) +
						                            " rect=" + std::to_string(static_cast<int>(layout.displayRect.x)) +
						                            "," + std::to_string(static_cast<int>(layout.displayRect.y)) + "," +
						                            std::to_string(static_cast<int>(layout.displayRect.width)) + "," +
						                            std::to_string(static_cast<int>(layout.displayRect.height)) +
						                            " mouse=" + std::to_string(static_cast<int>(mouse.x)) + "," +
						                            std::to_string(static_cast<int>(mouse.y)) +
						                            " over=" + (interaction.isOver ? "true" : "false") +
						                            " using=" + (interaction.isUsing ? "true" : "false") +
						                            " matrixChanged=" + (interaction.matrixChanged ? "true" : "false");
						URSA_LOG_DEBUG("Editor", ToUrsaString(message));
					}
					gizmoWasOver = interaction.isOver;
					gizmoWasUsing = interaction.isUsing;
					if (manipulated && manipulated.value)
					{
						if (prefabObjectId)
						{
							auto synced = draft.SyncPrefabTransform(prefabObjectId);
							if (synced)
							{
								synced = draft.RefreshPrefabPreview();
							}
							if (!synced)
							{
								diagnostic = synced.error;
							}
						}
						if (!play.Active())
						{
							document.MarkDirty();
						}
					}
					else if (!manipulated)
					{
						diagnostic = manipulated.error;
					}
				}
				else if (sceneHandTool)
				{
					gizmoWasOver = false;
					gizmoWasUsing = false;
				}
				{
					auto& camera = sceneTarget.preview->GetCamera();
					const auto& io = ImGui::GetIO();
					auto clearSceneViewLock = [&]()
					{
						sceneViewLockedTarget = {};
						sceneViewLockedDocument = {};
						sceneViewLockedPositionOffset = {};
						sceneViewLockedTargetOffset = {};
						sceneViewLockedPlaySession = false;
					};
					if (sceneViewLockedTarget)
					{
						if (sceneViewLockedDocument != document.Id() || sceneViewLockedPlaySession != play.Active())
						{
							clearSceneViewLock();
						}
						else
						{
							auto selected = hierarchy.SelectedGameObject(scene);
							auto selectedId = selected ? scene.PersistentId(selected.value)
							                           : Result<ResourceId>::Failure("No selected object");
							if (!selectedId || selectedId.value != sceneViewLockedTarget)
							{
								clearSceneViewLock();
							}
							else
							{
								auto world = scene.WorldTransform(selected.value);
								if (world)
								{
									const Vec3 objectPosition{world.value.At(0, 3), world.value.At(1, 3),
									                          world.value.At(2, 3)};
									camera.position = Add(objectPosition, sceneViewLockedPositionOffset);
									camera.target = Add(objectPosition, sceneViewLockedTargetOffset);
								}
								else
								{
									clearSceneViewLock();
								}
							}
						}
					}
					if (!sceneGizmoSelectionHandled && sceneNavigationGesture == SceneNavigationGesture::None && imageHovered &&
					    !sceneOrientationHovered && !sceneHandTool && !io.KeyAlt &&
					    ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoWasOver && !gizmoWasUsing)
					{
						sceneSelectionPending = true;
						sceneSelectionStart = {io.MousePos.x, io.MousePos.y};
						sceneSelectionControl = io.KeyCtrl;
						sceneSelectionShift = io.KeyShift;
					}
					if (sceneSelectionPending)
					{
						const ViewportPoint mouse{io.MousePos.x, io.MousePos.y};
						const Float32 deltaX = mouse.x - sceneSelectionStart.x;
						const Float32 deltaY = mouse.y - sceneSelectionStart.y;
						const bool marquee = deltaX * deltaX + deltaY * deltaY >= 25.f;
						if (marquee && io.MouseDown[ImGuiMouseButton_Left])
						{
							auto* drawList = ImGui::GetWindowDrawList();
							const ImVec2 first(sceneSelectionStart.x, sceneSelectionStart.y);
							const ImVec2 last(mouse.x, mouse.y);
							drawList->AddRectFilled(first, last, IM_COL32(80, 145, 230, 42));
							drawList->AddRect(first, last, IM_COL32(120, 180, 255, 220), 0.f, 0, 1.f);
						}
						if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
						{
							const Float32 aspect = static_cast<Float32>(sceneTarget.size.width) /
							                       static_cast<Float32>(sceneTarget.size.height);
							auto renderData = scene.CollectPreviewRenderData(camera, aspect);
							if (renderData)
							{
								FilterSceneViewRenderObjects(renderData.value.objects, hierarchy, true);
							}
							if (!renderData)
							{
								diagnostic = renderData.error;
							}
							else if (marquee)
							{
								const auto picked =
								    PickSceneObjectsInRectangle(camera, aspect, layout, sceneSelectionStart, mouse,
								                                renderData.value.objects, scene, hierarchy);
								Vector<EditorSelectionEntry> next;
								Size primaryIndex = static_cast<Size>(-1);
								if (selection && (sceneSelectionControl || sceneSelectionShift))
								{
									for (const auto& entry : selection->entries)
									{
										if (entry.kind == EditorSelectionKind::GameObject &&
										    entry.documentId == document.Id() && entry.playSession == play.Active() &&
										    !entry.IsMissing())
										{
											next.push_back(entry);
											if (const auto* currentPrimary = selection->PrimaryEntry();
											    currentPrimary && currentPrimary->SameIdentity(entry))
											{
												primaryIndex = next.size() - 1;
											}
										}
									}
								}
								for (const ResourceId id : picked)
								{
									EditorSelectionEntry entry;
									entry.kind = EditorSelectionKind::GameObject;
									entry.documentId = document.Id();
									entry.objectId = id;
									entry.playSession = play.Active();
									if (std::none_of(next.begin(), next.end(),
									                 [&](const auto& current) { return current.SameIdentity(entry); }))
									{
										next.push_back(entry);
										primaryIndex = next.size() - 1;
									}
									else
									{
										primaryIndex = static_cast<Size>(std::distance(
										    next.begin(),
										    std::find_if(next.begin(), next.end(), [&](const auto& current)
										                 { return current.SameIdentity(entry); })));
									}
								}
								bool accepted = true;
								if (selection)
								{
									accepted = selection->SelectEntries(std::move(next), primaryIndex);
									const auto* primary = selection->PrimaryEntry();
									if (accepted && primary && primary->kind == EditorSelectionKind::GameObject &&
									    primary->documentId == document.Id() && primary->playSession == play.Active())
									{
										auto primaryObject = scene.GameObjectById(primary->objectId);
										if (primaryObject)
										{
											auto synced = hierarchy.Select(scene, primaryObject.value);
											accepted = static_cast<bool>(synced);
										}
									}
									else if (accepted)
									{
										hierarchy.ClearSelection();
									}
								}
								else if (!picked.empty())
								{
									auto primaryObject = scene.GameObjectById(picked.back());
									accepted = primaryObject &&
									           static_cast<bool>(hierarchy.Select(scene, primaryObject.value));
								}
								else
								{
									hierarchy.ClearSelection();
								}
								if (!accepted)
								{
									diagnostic = "Scene selection could not be updated";
								}
							}
							else
							{
								Vector<ResourceId> overlapping;
								auto pickedObject =
								    PickSceneObject(camera, aspect, layout, mouse, renderData.value.objects, scene,
								                    hierarchy, &overlapping);
								if (pickedObject)
								{
									ResourceId pickedObjectId = pickedObject.value;
									if (!sceneSelectionControl && !sceneSelectionShift && overlapping.size() > 1)
									{
										const Float32 repeatDeltaX = mouse.x - scenePickCyclePosition.x;
										const Float32 repeatDeltaY = mouse.y - scenePickCyclePosition.y;
										const bool repeated =
										    scenePickCycleDocument == document.Id() &&
										    scenePickCyclePlaySession == play.Active() &&
										    ImGui::GetTime() - scenePickCycleTime <= 0.8 &&
										    repeatDeltaX * repeatDeltaX + repeatDeltaY * repeatDeltaY <= 25.f &&
										    scenePickCycleCandidates.size() == overlapping.size() &&
										    std::equal(scenePickCycleCandidates.begin(), scenePickCycleCandidates.end(),
										               overlapping.begin());
										if (repeated)
										{
											scenePickCycleIndex = (scenePickCycleIndex + 1) % overlapping.size();
										}
										else
										{
											scenePickCycleCandidates = overlapping;
											scenePickCycleIndex = 0;
										}
										scenePickCyclePosition = mouse;
										scenePickCycleTime = ImGui::GetTime();
										scenePickCycleDocument = document.Id();
										scenePickCyclePlaySession = play.Active();
										pickedObjectId = overlapping[scenePickCycleIndex];
									}
									else
									{
										scenePickCycleCandidates.clear();
										scenePickCycleTime = -1.0;
									}
									auto object = scene.GameObjectById(pickedObjectId);
									if (object)
									{
										bool accepted = true;
										if (selection)
										{
											EditorSelectionEntry entry;
											entry.kind = EditorSelectionKind::GameObject;
											entry.documentId = document.Id();
											entry.objectId = pickedObjectId;
											entry.playSession = play.Active();
											accepted =
											    selection->SelectClicked(entry, hierarchy.VisibleSelectionRows(),
											                             sceneSelectionControl, sceneSelectionShift);
											if (!accepted && sceneSelectionShift)
											{
												accepted = selection->SelectGameObject(document.Id(), pickedObjectId,
												                                       play.Active());
											}
											const auto* primary = selection->PrimaryEntry();
											if (accepted && primary &&
											    primary->kind == EditorSelectionKind::GameObject &&
											    primary->documentId == document.Id() &&
											    primary->playSession == play.Active())
											{
												auto selectedObject = scene.GameObjectById(primary->objectId);
												if (selectedObject)
												{
													accepted = static_cast<bool>(
													    hierarchy.Select(scene, selectedObject.value));
												}
											}
											else if (accepted)
											{
												hierarchy.ClearSelection();
											}
										}
										else
										{
											auto selected = hierarchy.Select(scene, object.value);
											accepted = static_cast<bool>(selected);
										}
										if (!accepted)
										{
											diagnostic = "Scene selection could not be updated";
										}
									}
								}
								else if (!sceneSelectionControl && !sceneSelectionShift)
								{
									scenePickCycleCandidates.clear();
									scenePickCycleTime = -1.0;
									const bool cleared = !selection || selection->Clear();
									if (cleared)
									{
										hierarchy.ClearSelection();
									}
								}
							}
							sceneSelectionPending = false;
						}
					}
					if (sceneNavigationGesture == SceneNavigationGesture::Pan && !io.MouseDown[ImGuiMouseButton_Middle])
					{
						if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle) &&
						    !ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 5.f) &&
						    io.MousePos.x >= layout.displayRect.x &&
						    io.MousePos.x <= layout.displayRect.x + layout.displayRect.width &&
						    io.MousePos.y >= layout.displayRect.y &&
						    io.MousePos.y <= layout.displayRect.y + layout.displayRect.height)
						{
							const Float32 aspect = static_cast<Float32>(sceneTarget.size.width) /
							                       static_cast<Float32>(sceneTarget.size.height);
							auto renderData = scene.CollectPreviewRenderData(camera, aspect);
							if (renderData)
							{
								FilterSceneViewRenderObjects(renderData.value.objects, hierarchy, true);
								Vec3 focusPoint{};
								auto focused = PickSceneObject(
								    camera, aspect, layout, {io.MousePos.x, io.MousePos.y}, renderData.value.objects, scene,
								    hierarchy, nullptr, &focusPoint);
								if (focused)
								{
									Camera focusTarget = camera;
									const Vec3 shift = focusPoint - focusTarget.target;
									focusTarget.position = Add(focusTarget.position, shift);
									focusTarget.target = focusPoint;
									StartFocusTransition(camera, focusTarget);
								}
							}
						}
						sceneNavigationGesture = SceneNavigationGesture::None;
					}
					else if (sceneNavigationGesture == SceneNavigationGesture::HandPan &&
					         !io.MouseDown[ImGuiMouseButton_Left])
					{
						sceneNavigationGesture = SceneNavigationGesture::None;
					}
					else if (sceneNavigationGesture == SceneNavigationGesture::Orbit &&
					         (!io.MouseDown[ImGuiMouseButton_Left] || !io.KeyAlt))
					{
						sceneNavigationGesture = SceneNavigationGesture::None;
					}
					else if (sceneNavigationGesture == SceneNavigationGesture::DragZoom &&
					         (!io.MouseDown[ImGuiMouseButton_Right] || !io.KeyAlt))
					{
						sceneNavigationGesture = SceneNavigationGesture::None;
					}
					else if (sceneNavigationGesture == SceneNavigationGesture::Flythrough &&
					         !io.MouseDown[ImGuiMouseButton_Right])
					{
						sceneNavigationGesture = SceneNavigationGesture::None;
					}
					if (sceneNavigationGesture == SceneNavigationGesture::None && imageHovered &&
					    !sceneOrientationHovered)
					{
						if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && io.KeyCtrl)
						{
							// Ctrl+right-click is reserved for selecting overlapping objects.
						}
						else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !sceneView2DMode &&
						         !sceneViewRotationLocked)
						{
							if (io.KeyAlt)
							{
								sceneNavigationGesture = SceneNavigationGesture::DragZoom;
								const Vec3 offset = camera.position - camera.target;
								const Float32 distance = std::sqrt(Dot(offset, offset));
								sceneDragZoomStart = camera.orthographic
								                         ? camera.orthographicSize * 0.5f
								                         : distance * std::sin(camera.verticalFovRadians * 0.5f);
								sceneDragZoomSpeed = std::max(std::abs(sceneDragZoomStart), 0.3f);
								sceneDragZoomTotalMotion = 0.f;
							}
							else
							{
								sceneNavigationGesture = SceneNavigationGesture::Flythrough;
							}
						}
						else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
						         ((sceneHandTool && !io.KeyAlt) || (sceneView2DMode && io.KeyAlt) ||
						          (!sceneView2DMode && io.KeyAlt && (io.KeyCtrl || io.KeySuper))))
						{
							sceneNavigationGesture = SceneNavigationGesture::HandPan;
						}
						else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
						{
							sceneNavigationGesture = SceneNavigationGesture::Pan;
						}
						else if (io.KeyAlt && !sceneView2DMode && !sceneViewRotationLocked && !gizmoWasOver && !gizmoWasUsing &&
						         ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						{
							sceneNavigationGesture = SceneNavigationGesture::Orbit;
						}
					}
					if (sceneNavigationGesture != SceneNavigationGesture::None &&
					    ImGui::IsKeyPressed(ImGuiKey_Escape, false))
					{
						sceneNavigationGesture = SceneNavigationGesture::None;
					}
					SetSceneCursorCaptured(sceneNavigationGesture == SceneNavigationGesture::Flythrough);
					if (sceneNavigationGesture != SceneNavigationGesture::None)
					{
						sceneFocusTransition.active = false;
						clearSceneViewLock();
					}
					if (sceneNavigationGesture == SceneNavigationGesture::Pan ||
					    sceneNavigationGesture == SceneNavigationGesture::HandPan)
					{
						const Float32 panSpeed = io.KeyShift ? 4.f : 1.f;
						PanSceneView(camera, {io.MouseDelta.x * panSpeed, io.MouseDelta.y * panSpeed},
						             layout.displayRect.height);
					}
					else if (sceneNavigationGesture == SceneNavigationGesture::Orbit)
					{
						OrbitSceneView(camera, {io.MouseDelta.x, io.MouseDelta.y});
					}
					else if (sceneNavigationGesture == SceneNavigationGesture::DragZoom)
					{
						const Float32 deltaX = -io.MouseDelta.x;
						const Float32 deltaY = -io.MouseDelta.y;
						const Float32 dominantDelta = std::max(std::abs(deltaX), std::abs(deltaY));
						if (dominantDelta > 0.f && std::abs(std::abs(deltaX) - std::abs(deltaY)) / dominantDelta > 0.1f)
						{
							sceneZoomUseYSign = std::abs(deltaY) > std::abs(deltaX);
						}
						const Float32 signedAxisDelta = sceneZoomUseYSign ? deltaY : deltaX;
						const Float32 signedDelta = (signedAxisDelta < 0.f ? -1.f : 1.f) *
						                            std::sqrt(deltaX * deltaX + deltaY * deltaY);
						// Unity uses a 9:3 multiplier for Shift vs. normal drag zoom.
						const Float32 zoomSensitivity = io.KeyShift ? 9.f : 3.f;
						ZoomSceneViewByDrag(camera, signedDelta * zoomSensitivity, sceneDragZoomStart,
						                    sceneDragZoomSpeed, sceneDragZoomTotalMotion);
					}
					else if (sceneNavigationGesture == SceneNavigationGesture::Flythrough)
					{
						const Float32 speedScroll = io.KeyShift ? io.MouseWheelH : io.MouseWheel;
						if (speedScroll != 0.f)
						{
							const Float32 speedRange = sceneFlythroughMaxSpeed - sceneFlythroughMinSpeed;
							const Float32 normalizedSpeed = std::clamp(
							    (sceneFlythroughSpeed - sceneFlythroughMinSpeed) / speedRange + speedScroll * 0.03f,
							    0.f, 1.f);
							sceneFlythroughSpeed = sceneFlythroughMinSpeed + normalizedSpeed * speedRange;
							settingsChanged = true;
						}
						const ViewportPoint movement{
						    (ImGui::IsKeyDown(ImGuiKey_D) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.f : 0.f),
						    (ImGui::IsKeyDown(ImGuiKey_W) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.f : 0.f)};
						const Float32 verticalMovement =
						    (ImGui::IsKeyDown(ImGuiKey_E) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.f : 0.f);
						const Float32 movementMagnitude = std::sqrt(movement.x * movement.x + movement.y * movement.y +
						                                            verticalMovement * verticalMovement);
						if (movementMagnitude > 0.f)
						{
							sceneFlythroughTargetSpeed =
							    sceneFlythroughTargetSpeed < std::numeric_limits<Float32>::epsilon()
							        ? 9.f
							        : sceneFlythroughTargetSpeed * std::pow(1.8f, io.DeltaTime);
						}
						else
						{
							sceneFlythroughTargetSpeed = 0.f;
						}
						const Float32 easeProgress = std::clamp(io.DeltaTime / 0.4f, 0.f, 1.f);
						const Float32 easeFactor = 1.f - std::pow(1.f - easeProgress, 4.f);
						sceneFlythroughCurrentSpeed +=
						    (sceneFlythroughTargetSpeed - sceneFlythroughCurrentSpeed) * easeFactor;
						FlythroughSceneView(camera, {io.MouseDelta.x, io.MouseDelta.y}, movement, verticalMovement,
						                    io.DeltaTime, sceneFlythroughCurrentSpeed * sceneFlythroughSpeed,
						                    io.KeyShift);
					}
					if (sceneNavigationGesture == SceneNavigationGesture::None && imageHovered &&
					    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
					    !io.KeyAlt && !io.KeyCtrl)
					{
						const ViewportPoint arrowMovement{(ImGui::IsKeyDown(ImGuiKey_RightArrow) ? 1.f : 0.f) -
						                                      (ImGui::IsKeyDown(ImGuiKey_LeftArrow) ? 1.f : 0.f),
						                                  (ImGui::IsKeyDown(ImGuiKey_UpArrow) ? 1.f : 0.f) -
						                                      (ImGui::IsKeyDown(ImGuiKey_DownArrow) ? 1.f : 0.f)};
						const Float32 movementMagnitude =
						    std::sqrt(arrowMovement.x * arrowMovement.x + arrowMovement.y * arrowMovement.y);
						if (movementMagnitude > 0.f)
						{
							sceneFlythroughTargetSpeed =
							    sceneFlythroughTargetSpeed < std::numeric_limits<Float32>::epsilon()
							        ? 9.f
							        : sceneFlythroughTargetSpeed * std::pow(1.8f, io.DeltaTime);
						}
						else
						{
							sceneFlythroughTargetSpeed = 0.f;
						}
						const Float32 easeProgress = std::clamp(io.DeltaTime / 0.4f, 0.f, 1.f);
						const Float32 easeFactor = 1.f - std::pow(1.f - easeProgress, 4.f);
						sceneFlythroughCurrentSpeed +=
						    (sceneFlythroughTargetSpeed - sceneFlythroughCurrentSpeed) * easeFactor;
						if (arrowMovement.x != 0.f || arrowMovement.y != 0.f)
						{
							sceneFocusTransition.active = false;
							clearSceneViewLock();
							MoveSceneView(camera, arrowMovement, io.DeltaTime,
							              sceneFlythroughCurrentSpeed * sceneFlythroughSpeed, io.KeyShift,
							              sceneView2DMode || camera.orthographic);
						}
					}
					if (imageHovered && io.MouseWheel != 0.f &&
					    sceneNavigationGesture != SceneNavigationGesture::Flythrough &&
					    sceneNavigationGesture != SceneNavigationGesture::DragZoom)
					{
						sceneFocusTransition.active = false;
						clearSceneViewLock();
						const ViewportPoint zoomPosition =
						    sceneView2DMode == io.KeyAlt
						        ? ViewportPoint{layout.displayRect.width * 0.5f, layout.displayRect.height * 0.5f}
						        : ViewportPoint{io.MousePos.x - layout.displayRect.x,
						                        io.MousePos.y - layout.displayRect.y};
						ZoomSceneViewByWheel(camera, io.MouseWheel, zoomPosition,
						                     {layout.displayRect.width, layout.displayRect.height});
					}
					const bool hierarchyFocusRequested = hierarchy.ConsumeFocusSelectionRequest();
					if (hierarchyFocusRequested ||
					    (imageHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
					     !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)))
					{
						const Float32 aspect = static_cast<Float32>(sceneTarget.size.width) /
						                       static_cast<Float32>(sceneTarget.size.height);
						auto selected = hierarchy.SelectedGameObject(scene);
						if (selected)
						{
							auto selectedId = scene.PersistentId(selected.value);
							Vector<GameObject::Ptr> focusTargets;
							if (selection)
							{
								const auto* activeEntry = selection->PrimaryEntry();
								if (activeEntry && activeEntry->kind == EditorSelectionKind::GameObject &&
								    activeEntry->documentId == document.Id() &&
								    activeEntry->playSession == play.Active() && !activeEntry->IsMissing())
								{
									auto activeObject = scene.GameObjectById(activeEntry->objectId);
									if (activeObject)
									{
										focusTargets.push_back(activeObject.value);
									}
								}
								for (const auto& entry : selection->entries)
								{
									if (entry.kind != EditorSelectionKind::GameObject ||
									    entry.documentId != document.Id() || entry.playSession != play.Active() ||
									    entry.IsMissing() ||
									    (activeEntry && entry.SameIdentity(*activeEntry)))
									{
										continue;
									}
									auto object = scene.GameObjectById(entry.objectId);
									if (object)
									{
										focusTargets.push_back(object.value);
									}
								}
							}
							if (focusTargets.empty())
							{
								focusTargets.push_back(selected.value);
							}
							if (!io.KeyShift)
							{
								clearSceneViewLock();
								Camera focusTarget = camera;
								FocusSceneSelection(scene, focusTargets, focusTarget, aspect);
								StartFocusTransition(camera, focusTarget);
							}
							else
							{
								if (selectedId && selectedId.value == sceneViewLockedTarget &&
								    sceneViewLockedDocument == document.Id() &&
								    sceneViewLockedPlaySession == play.Active())
								{
									clearSceneViewLock();
								}
								else
								{
									FocusSceneSelection(scene, focusTargets, camera, aspect);
									auto world = scene.WorldTransform(selected.value);
									if (selectedId && world)
									{
										const Vec3 objectPosition{world.value.At(0, 3), world.value.At(1, 3),
										                          world.value.At(2, 3)};
										sceneViewLockedTarget = selectedId.value;
										sceneViewLockedDocument = document.Id();
										sceneViewLockedPlaySession = play.Active();
										sceneViewLockedPositionOffset =
										    Add(camera.position, Scale(objectPosition, -1.f));
										sceneViewLockedTargetOffset = Add(camera.target, Scale(objectPosition, -1.f));
									}
								}
							}
						}
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
	if (showGame)
	{
		const bool maximizedForPlay = play.Active() && maximizeGameViewOnPlay;
		const char* gameWindowName = maximizedForPlay ? "Game###GameMaximized" : "Game";
		if (focusGameOnThisPlayStart || (maximizedForPlay && !gameWasPlaying))
		{
			ImGui::SetNextWindowFocus();
		}
		ImGuiWindowFlags gameWindowFlags = ImGuiWindowFlags_None;
		if (maximizedForPlay)
		{
			const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always);
			ImGui::SetNextWindowSize(mainViewport->WorkSize, ImGuiCond_Always);
			ImGui::SetNextWindowViewport(mainViewport->ID);
			gameWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
			                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
		}
		if (ImGui::Begin(gameWindowName, &showGame, gameWindowFlags))
		{
			const ImVec2 dpi = CurrentViewportFramebufferScale();
			if (ImGui::BeginPopupContextWindow("GamePanelActions", ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Warn if No Cameras Rendering", nullptr, warnIfNoGameCamera))
				{
					warnIfNoGameCamera = !warnIfNoGameCamera;
					settingsChanged = true;
				}
				if (ImGui::MenuItem("Clear Every Frame in Edit Mode", nullptr, clearEveryFrameInEditMode))
				{
					clearEveryFrameInEditMode = !clearEveryFrameInEditMode;
					settingsChanged = true;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Close Panel"))
				{
					showGame = false;
				}
				ImGui::EndPopup();
			}
			DrawGameToolbar(userFiles, projectKey, diagnostic);
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const ImVec2 position = ImGui::GetCursorScreenPos();
			const UInt32 availableWidth =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.x * dpi.x))));
			const UInt32 availableHeight =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.y * dpi.y))));
			const auto size = ResolveViewportSize({availableWidth, availableHeight}, gameSizeMode, gamePresetWidth,
			                                      gamePresetHeight, device->Capabilities().maxTextureSize, 16'777'216);
			if ((!availableWidth || !availableHeight) || (!size && !gameTarget.color))
			{
				if (!availableWidth || !availableHeight)
				{
					ImGui::TextDisabled("Resize the Game view to render its camera.");
				}
				else
				{
					ImGui::TextWrapped("%s", size.error.c_str());
				}
			}
			else if (!HasEffectiveGameCamera(scene))
			{
				if (size)
				{
					auto target = EnsureTarget(gameTarget, size.value);
					if (!target)
					{
						diagnostic = target.error;
					}
				}
				if (gameTarget.color)
				{
					gameTarget.visible = true;
					const ViewportRect content{position.x, position.y, available.x, available.y};
					if (gameScaleSnapPending && ImGui::GetTime() >= gameScaleSnapAt)
					{
						const ViewportScaleLimits limits = GetViewportScaleLimits(content, dpi.x, gameTarget.size);
						const Float32 snappedScale = SnapViewportScale(gameScale, limits.minimum, limits.maximum);
						settingsChanged |= snappedScale != gameScale;
						if (gameScale > 0.f)
						{
							const Float32 panScale = snappedScale / gameScale;
							gamePan.x *= panScale;
							gamePan.y *= panScale;
						}
						gameScale = snappedScale;
						gameScaleSnapPending = false;
					}
					const auto layout = ComputeViewportLayout(content, dpi.x, gameTarget.size, gameScale, gamePan, gameFit);
					const Float32 clipX = std::max(content.x, layout.displayRect.x);
					const Float32 clipY = std::max(content.y, layout.displayRect.y);
					ImGui::PushClipRect({clipX, clipY}, {clipX + layout.clipRect.width, clipY + layout.clipRect.height},
					                    true);
					ImGui::SetCursorScreenPos({layout.displayRect.x, layout.displayRect.y});
					ImGui::Image(gameTarget.imageId, {layout.displayRect.width, layout.displayRect.height});
					const bool viewportHovered = ImGui::IsWindowHovered() &&
					                             ImGui::IsMouseHoveringRect({content.x, content.y},
					                                                         {content.x + content.width,
					                                                          content.y + content.height}, false);
					if (warnIfNoGameCamera)
					{
						const char* warning = "The current scene has no active game camera.";
						const ImVec2 textSize = ImGui::CalcTextSize(warning);
						const ImVec2 warningPosition{layout.displayRect.x + (layout.displayRect.width - textSize.x) * 0.5f,
						                             layout.displayRect.y + 12.f};
						auto* drawList = ImGui::GetWindowDrawList();
						drawList->AddRectFilled({warningPosition.x - 8.f, warningPosition.y - 4.f},
						                        {warningPosition.x + textSize.x + 8.f, warningPosition.y + textSize.y + 4.f},
						                        IM_COL32(20, 22, 26, 224), 4.f);
						drawList->AddText(warningPosition, IM_COL32(245, 205, 120, 255), warning);
					}
					const ImGuiIO& gameInput = ImGui::GetIO();
					if (viewportHovered && gameInput.MouseWheel != 0.f && (!play.Active() || play.Paused()))
					{
						const Float32 requestedScale = layout.scale * std::exp(gameInput.MouseWheel * 0.15f);
						const auto zoomedLayout =
						    ComputeViewportLayout(content, dpi.x, gameTarget.size, requestedScale, gamePan, false);
						const ImVec2 mouse = gameInput.MousePos;
						const Float32 anchorX = (mouse.x - layout.displayRect.x) / layout.displayRect.width;
						const Float32 anchorY = (mouse.y - layout.displayRect.y) / layout.displayRect.height;
						gamePan.x += (anchorX - 0.5f) * (layout.displayRect.width - zoomedLayout.displayRect.width);
						gamePan.y += (anchorY - 0.5f) * (layout.displayRect.height - zoomedLayout.displayRect.height);
						gameScale = zoomedLayout.scale;
						gameScaleSnapPending = true;
						gameScaleSnapAt = ImGui::GetTime() + 0.2;
						gameFit = false;
						settingsChanged = true;
					}
					if (viewportHovered && (!play.Active() || play.Paused()) &&
					    ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
					{
						const ImVec2 delta = gameInput.MouseDelta;
						gamePan.x += delta.x;
						gamePan.y += delta.y;
						gameFit = false;
						settingsChanged = true;
					}
					ImGui::PopClipRect();
				}
			}
			else
			{
				if (size)
				{
					auto target = EnsureTarget(gameTarget, size.value);
					if (!target)
					{
						diagnostic = target.error;
					}
				}
				if (gameTarget.color)
				{
					gameTarget.visible = true;
					const ViewportRect content{position.x, position.y, available.x, available.y};
					if (gameScaleSnapPending && ImGui::GetTime() >= gameScaleSnapAt)
					{
						const ViewportScaleLimits limits = GetViewportScaleLimits(content, dpi.x, gameTarget.size);
						const Float32 snappedScale = SnapViewportScale(gameScale, limits.minimum, limits.maximum);
						settingsChanged |= snappedScale != gameScale;
						if (gameScale > 0.f)
						{
							const Float32 panScale = snappedScale / gameScale;
							gamePan.x *= panScale;
							gamePan.y *= panScale;
						}
						gameScale = snappedScale;
						gameScaleSnapPending = false;
					}
					const auto layout =
					    ComputeViewportLayout(content, dpi.x, gameTarget.size, gameScale, gamePan, gameFit);
					if (gameScale != layout.scale)
					{
						gameScale = layout.scale;
						if (!gameFit)
						{
							settingsChanged = true;
						}
					}
					const auto panLayout =
					    ComputeViewportLayout(content, dpi.x, gameTarget.size, gameScale, gamePan, gameFit);
					const ViewportPoint clampedPan = ClampViewportPan(
					    gamePan,
					    {static_cast<UInt32>(std::ceil(panLayout.displayRect.width)),
					     static_cast<UInt32>(std::ceil(panLayout.displayRect.height))},
					    {static_cast<UInt32>(std::floor(available.x)), static_cast<UInt32>(std::floor(available.y))});
					if (clampedPan.x != gamePan.x || clampedPan.y != gamePan.y)
					{
						gamePan = clampedPan;
						settingsChanged = true;
					}
					const Float32 clipX = std::max(content.x, layout.displayRect.x);
					const Float32 clipY = std::max(content.y, layout.displayRect.y);
					ImGui::PushClipRect({clipX, clipY}, {clipX + layout.clipRect.width, clipY + layout.clipRect.height},
					                    true);
					ImGui::SetCursorScreenPos({layout.displayRect.x, layout.displayRect.y});
					ImGui::Image(gameTarget.imageId, {layout.displayRect.width, layout.displayRect.height});
					const bool viewportHovered = ImGui::IsWindowHovered() &&
					                             ImGui::IsMouseHoveringRect({content.x, content.y},
					                                                         {content.x + content.width,
					                                                          content.y + content.height}, false);
					const ImGuiIO& gameInput = ImGui::GetIO();
					if (viewportHovered && gameInput.MouseWheel != 0.f && (!play.Active() || play.Paused()))
					{
						const Float32 requestedScale = layout.scale * std::exp(gameInput.MouseWheel * 0.15f);
						const auto zoomedLayout =
						    ComputeViewportLayout(content, dpi.x, gameTarget.size, requestedScale, gamePan, false);
						const ImVec2 mouse = gameInput.MousePos;
						const Float32 anchorX = (mouse.x - layout.displayRect.x) / layout.displayRect.width;
						const Float32 anchorY = (mouse.y - layout.displayRect.y) / layout.displayRect.height;
						gamePan.x += (anchorX - 0.5f) * (layout.displayRect.width - zoomedLayout.displayRect.width);
						gamePan.y += (anchorY - 0.5f) * (layout.displayRect.height - zoomedLayout.displayRect.height);
						gameScale = zoomedLayout.scale;
						gameScaleSnapPending = true;
						gameScaleSnapAt = ImGui::GetTime() + 0.2;
						gameFit = false;
						settingsChanged = true;
					}
					if (showGameStats || showGameGizmos)
					{
						const Float32 aspect =
						    static_cast<Float32>(gameTarget.size.width) / static_cast<Float32>(gameTarget.size.height);
						auto renderData = scene.CollectRenderData(aspect);
						if (showGameGizmos && renderData && selection)
						{
							std::vector<ResourceId> selectedObjects;
							for (const EditorSelectionEntry& entry : selection->entries)
							{
								if (entry.kind == EditorSelectionKind::GameObject && !entry.IsMissing() &&
								    entry.documentId == document.Id() && entry.playSession == play.Active())
								{
									selectedObjects.push_back(entry.objectId);
								}
							}
							const EditorSelectionEntry* primarySelection = selection->PrimaryEntry();
							if (selectedObjects.empty() && primarySelection &&
							    primarySelection->kind == EditorSelectionKind::GameObject &&
							    !primarySelection->IsMissing() && primarySelection->documentId == document.Id() &&
							    primarySelection->playSession == play.Active() && primarySelection->objectId)
							{
								selectedObjects.push_back(primarySelection->objectId);
							}
							auto* drawList = ImGui::GetWindowDrawList();
							for (const Rendering::RenderObject& object : renderData.value.objects)
							{
								if (std::find(selectedObjects.begin(), selectedObjects.end(), object.objectId) ==
								        selectedObjects.end())
								{
									continue;
								}
								DrawProjectedBounds(drawList, renderData.value.viewProjection, layout, object.worldBounds,
								                   IM_COL32(112, 176, 255, 235));
							}
						}
						if (showGameStats)
						{
							UInt64 triangleCount = 0;
							UInt64 vertexCount = 0;
							Size renderObjectCount = 0;
							if (renderData)
							{
								renderObjectCount = renderData.value.objects.size();
								for (const auto& object : renderData.value.objects)
								{
									if (!object.meshSnapshot ||
									    object.subMesh >= object.meshSnapshot->data.subMeshes.size())
									{
										continue;
									}
									const auto& data = object.meshSnapshot->data;
									const auto& subMesh = data.subMeshes[object.subMesh];
									triangleCount += subMesh.indexCount / 3;
									vertexCount += data.positions.size();
								}
							}
							const auto& io = ImGui::GetIO();
							const Float32 frameTimeMs = io.Framerate > 0.f ? 1000.f / io.Framerate : 0.f;
							const std::array<std::string, 6> rows{
							    "FPS " + std::to_string(static_cast<Int32>(std::round(io.Framerate))),
							    "Main " +
							        std::to_string(static_cast<Int32>(std::round(frameTimeMs))) + " ms",
							    "Screen " + std::to_string(gameTarget.size.width) + " x " +
							        std::to_string(gameTarget.size.height),
							    "Draw Entries " + std::to_string(renderObjectCount),
							    "Tris " + std::to_string(triangleCount),
							    "Verts " + std::to_string(vertexCount)};
							Float32 textWidth = 0.f;
							for (const auto& row : rows)
							{
								textWidth = std::max(textWidth, ImGui::CalcTextSize(row.c_str()).x);
							}
							const Float32 rowHeight = ImGui::GetTextLineHeight();
							const ImVec2 overlaySize{textWidth + 16.f, rowHeight * rows.size() + 14.f};
							const ImVec2 overlayPosition{
							    std::max(layout.displayRect.x + 4.f,
							             layout.displayRect.x + layout.displayRect.width - overlaySize.x - 8.f),
							    layout.displayRect.y + 8.f};
							auto* drawList = ImGui::GetWindowDrawList();
							drawList->AddRectFilled(overlayPosition,
							                        {overlayPosition.x + overlaySize.x, overlayPosition.y + overlaySize.y},
							                        IM_COL32(20, 22, 26, 224), 4.f);
							for (Size row = 0; row < rows.size(); ++row)
							{
								drawList->AddText({overlayPosition.x + 8.f,
								                   overlayPosition.y + 7.f + rowHeight * static_cast<Float32>(row)},
								                  IM_COL32(235, 235, 235, 255), rows[row].c_str());
							}
						}
					}
					const bool panning = viewportHovered && (!play.Active() || play.Paused()) &&
					                     ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
					if (panning)
					{
						const ImVec2 delta = ImGui::GetIO().MouseDelta;
						gamePan.x += delta.x;
						gamePan.y += delta.y;
						gameFit = false;
						settingsChanged = true;
					}
					ImGui::PopClipRect();
					viewportFocused =
					    play.Active() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !panning;
					if (viewportFocused && focusedGameViewport)
					{
						*focusedGameViewport = ImGui::GetWindowViewport();
					}
				}
			}
		}
		ImGui::End();
	}
	gameWasPlaying = play.Active();
	if (userFiles && userFiles->IsMounted("user"))
	{
		const UInt64 overrideRevision = hierarchy.SceneViewOverridesRevision();
		if (overrideRevision != savedSceneViewOverridesRevision)
		{
			const String statePath = "user:Settings/SceneView/" + sceneViewStateKey + ".json";
			auto path = VirtualPath::Parse(statePath);
			if (!path)
			{
				diagnostic = path.error;
			}
			else
			{
				const String stateText = hierarchy.SerializeSceneViewOverrides(scene);
				auto written = userFiles->WriteAtomic(path.value, ToBytes(stateText));
				if (!written)
				{
					diagnostic = written.error;
				}
				else
				{
					savedSceneViewOverridesRevision = overrideRevision;
				}
			}
		}
		if (presetsChanged)
		{
			SaveCustomPresets(*userFiles, diagnostic);
		}
		if (settingsChanged)
		{
			SaveProjectSettings(*userFiles, projectKey, diagnostic);
		}
	}
	return {};
}

Result<void> SceneViewPanel::TransformSelectionToView(Scene& scene, const EditorSelectionContext& selection,
                                                     ResourceId documentId, bool playSession, bool alignRotation)
{
	if (!sceneTarget.preview || selection.entries.empty())
	{
		return Result<void>::Failure("Open the Scene view and select one or more GameObjects");
	}
	struct TransformSnapshot
	{
		GameObject::Ptr object;
		Mat4 original;
		Mat4 target;
	};
	Vector<TransformSnapshot> snapshots;
	snapshots.reserve(selection.entries.size());
	const Camera& camera = sceneTarget.preview->GetCamera();
	const Mat4 cameraTransform = camera.View().Inverse();
	for (const auto& entry : selection.entries)
	{
		if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != documentId ||
		    entry.playSession != playSession || entry.IsMissing())
		{
			return Result<void>::Failure("Select GameObjects in the active Hierarchy first");
		}
		auto object = scene.GameObjectById(entry.objectId);
		if (!object)
		{
			return Result<void>::Failure(object.error);
		}
		auto world = scene.WorldTransform(object.value);
		if (!world)
		{
			return Result<void>::Failure(world.error);
		}
		Mat4 target = world.value;
		if (alignRotation)
		{
			for (int column = 0; column < 3; ++column)
			{
				const Float32 scale = std::sqrt(world.value.At(0, column) * world.value.At(0, column) +
				                                world.value.At(1, column) * world.value.At(1, column) +
				                                world.value.At(2, column) * world.value.At(2, column));
				for (int row = 0; row < 3; ++row)
				{
					target.At(row, column) = cameraTransform.At(row, column) * scale;
				}
			}
		}
		target.At(0, 3) = camera.position.x;
		target.At(1, 3) = camera.position.y;
		target.At(2, 3) = camera.position.z;
		snapshots.push_back({object.value, world.value, target});
	}
	Size applied = 0;
	for (; applied < snapshots.size(); ++applied)
	{
		auto transformed = ApplyGizmoWorldTransform(scene, snapshots[applied].object, snapshots[applied].target);
		if (!transformed)
		{
			while (applied > 0)
			{
				--applied;
				ApplyGizmoWorldTransform(scene, snapshots[applied].object, snapshots[applied].original);
			}
			return transformed;
		}
	}
	return {};
}

Result<void> SceneViewPanel::RenderPending(SceneDraft& draft, PlaySession& play,
                                           const EditorRenderViewport& renderViewport)
{
	Scene& scene = play.Active() ? *play.GetScene() : draft.GetScene();
	if (sceneTarget.visible)
	{
		if (!renderViewport || !sceneTarget.output)
		{
			return Result<void>::Failure("Scene viewport has no active render pipeline or target");
		}
		const Float32 aspect = Float32(sceneTarget.size.width) / Float32(sceneTarget.size.height);
		const Camera& editorCamera = sceneTarget.preview->GetCamera();
		Rendering::CameraSettings settings;
		settings.type = Rendering::CameraType::SceneView;
		settings.background = {0.035f, 0.055f, 0.09f, 1.0f};
		settings.targetWidth = sceneTarget.size.width;
		settings.targetHeight = sceneTarget.size.height;
		Rendering::CameraData camera{sceneViewCameraId, editorCamera.ViewProjection(aspect), editorCamera.position,
		                             settings};
		camera.view = editorCamera.View();
		camera.projection = editorCamera.Projection(aspect);
		camera.inverseViewProjection = camera.viewProjection.Inverse();
		camera.nearPlane = editorCamera.nearPlane;
		camera.farPlane = editorCamera.farPlane;
		camera.orthographic = editorCamera.orthographic;
		camera.debugName = "Scene View";
		auto rendered = renderViewport(scene, sceneTarget.output, Rendering::CameraType::SceneView,
		                               std::optional<Rendering::CameraData>{std::move(camera)});
		if (!rendered && ManagedRenderServicesUnavailable(rendered.error))
		{
			rendered = sceneTarget.preview->Render(scene, sceneTarget.output);
		}
		if (!rendered)
		{
			return Result<void>::Failure("Scene viewport render failed: " + rendered.error);
		}
	}
	const bool hasGameCamera = HasEffectiveGameCamera(scene);
	if (gameTarget.visible && clearEveryFrameInEditMode && !play.Active())
	{
		bool needsClear = !hasGameCamera;
		if (hasGameCamera)
		{
			const auto* camera = dynamic_cast<const CameraComponent*>(scene.GetComponent(scene.ActiveCamera()));
			needsClear = camera && camera->rendering.clearFlags != Rendering::CameraClearFlags::ColorAndDepth;
		}
		if (needsClear)
		{
			auto cleared = ClearViewportColor(device, gameTarget.output, {0.f, 0.f, 0.f, 1.f});
			if (!cleared)
			{
				return Result<void>::Failure("Game viewport clear failed: " + cleared.error);
			}
		}
	}
	if (gameTarget.visible && hasGameCamera)
	{
		if (!renderViewport || !gameTarget.output)
		{
			return Result<void>::Failure("Game viewport has no active render pipeline or target");
		}
		auto rendered = renderViewport(scene, gameTarget.output, Rendering::CameraType::Game, {});
		if (!rendered && ManagedRenderServicesUnavailable(rendered.error))
		{
			rendered = gameTarget.preview->RenderGame(scene, gameTarget.output);
		}
		if (!rendered)
		{
			return Result<void>::Failure("Game viewport render failed: " + rendered.error);
		}
	}
	return {};
}

void SceneViewPanel::DrawEmpty(bool& showScene, bool& showGame)
{
	sceneNavigationGesture = SceneNavigationGesture::None;
	SetSceneCursorCaptured(false);
	if (showScene)
	{
		ImGui::Begin("Scene", &showScene);
		ImGui::TextDisabled("Open a scene to view and edit it.");
		ImGui::End();
	}
	if (showGame)
	{
		ImGui::Begin("Game", &showGame);
		ImGui::TextDisabled("Open a scene to preview its active game camera.");
		ImGui::End();
	}
}

Result<void> SceneViewPanel::ReloadResources()
{
	auto scenePreview = EditorScenePreview::Create(device);
	if (!scenePreview)
	{
		return Result<void>::Failure(scenePreview.error);
	}
	scenePreview.value->GetCamera() = sceneTarget.preview->GetCamera();
	auto gamePreview = EditorScenePreview::Create(device);
	if (!gamePreview)
	{
		return Result<void>::Failure(gamePreview.error);
	}
	gameTarget.preview = std::move(gamePreview.value);
	sceneTarget.preview = std::move(scenePreview.value);
	return {};
}
} // namespace Ursa::Editor
