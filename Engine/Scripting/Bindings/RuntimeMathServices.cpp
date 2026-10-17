#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingMath.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <cmath>

namespace Ursa::Scripting::Bindings
{
Result<RuntimeServices::LocalValue> RuntimeServices::GetLocalTransform(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	const auto transform =
	    TakeBindingResult(api.scene.GetLocalTransform(TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))));
	LocalValue value;
	value.F_Position = WriteVector3<VectorValue>(transform.position);
	value.F_Rotation = WriteQuaternion<Wire::Value_Ursa_Quaternion>(transform.rotation);
	value.F_Scale = WriteVector3<VectorValue>(transform.scale);
	return Result<LocalValue>::Success(value);
}

Result<void> RuntimeServices::SetLocalTransform(BindingCall& call, const Handle& handle, const LocalValue& value)
{
	auto& api = SceneApi(call);
	LocalTransform transform;
	transform.position = ReadVector3(value.F_Position);
	transform.rotation = ReadQuaternion(value.F_Rotation);
	transform.scale = ReadVector3(value.F_Scale);
	return api.scene.SetLocalTransform(TakeBindingResult(api.Resolve(ReadManagedHandle(handle))), transform);
}

Result<RuntimeServices::VectorValue> RuntimeServices::GetWorldPosition(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	const auto world = TakeBindingResult(TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->WorldTransform());
	return Result<VectorValue>::Success(WriteVector3<VectorValue>({world.At(0, 3), world.At(1, 3), world.At(2, 3)}));
}

Result<void> RuntimeServices::SetWorldPosition(BindingCall& call, const Handle& handle, const VectorValue& value)
{
	if (!std::isfinite(value.F_X) || !std::isfinite(value.F_Y) || !std::isfinite(value.F_Z))
	{
		return Result<void>::Failure("World position must be finite");
	}
	auto& api = SceneApi(call);
	const auto object = TakeBindingResult(api.Resolve(ReadManagedHandle(handle)));
	auto transform = TakeBindingResult(object->GetLocalTransform());
	Vec3 position = ReadVector3(value);
	if (const auto parent = object->Parent())
	{
		const auto inverse = TakeBindingResult(parent->WorldTransform()).InverseAffine();
		position = {inverse.At(0, 0) * value.F_X + inverse.At(0, 1) * value.F_Y + inverse.At(0, 2) * value.F_Z +
		                inverse.At(0, 3),
		            inverse.At(1, 0) * value.F_X + inverse.At(1, 1) * value.F_Y + inverse.At(1, 2) * value.F_Z +
		                inverse.At(1, 3),
		            inverse.At(2, 0) * value.F_X + inverse.At(2, 1) * value.F_Y + inverse.At(2, 2) * value.F_Z +
		                inverse.At(2, 3)};
	}
	transform.position = position;
	return object->SetLocalTransform(transform);
}

Result<RuntimeServices::Projection> RuntimeServices::GetCameraProjection(BindingCall& call, const Handle& camera)
{
	auto& api = SceneApi(call);
	const auto* value =
	    dynamic_cast<const CameraComponent*>(TakeBindingResult(api.ResolveComponent(ReadManagedHandle(camera))));
	if (!value)
	{
		return Result<Projection>::Failure("Projection requires a CameraComponent");
	}
	return Result<Projection>::Success({value->verticalFovRadians, value->nearPlane, value->farPlane});
}

Result<void> RuntimeServices::SetCameraProjection(BindingCall& call, const Handle& camera, const Projection& value)
{
	if (!std::isfinite(value.F_VerticalFovRadians) || value.F_VerticalFovRadians <= 0 ||
	    value.F_VerticalFovRadians >= 3.14159265f || !std::isfinite(value.F_NearPlane) || value.F_NearPlane <= 0 ||
	    !std::isfinite(value.F_FarPlane) || value.F_FarPlane <= value.F_NearPlane)
	{
		return Result<void>::Failure("Camera projection requires radians in (0, pi) and 0 < near < far");
	}
	auto& api = SceneApi(call);
	auto* target = dynamic_cast<CameraComponent*>(TakeBindingResult(api.ResolveComponent(ReadManagedHandle(camera))));
	if (!target)
	{
		return Result<void>::Failure("Projection requires a CameraComponent");
	}
	target->verticalFovRadians = value.F_VerticalFovRadians;
	target->nearPlane = value.F_NearPlane;
	target->farPlane = value.F_FarPlane;
	return {};
}

Result<RuntimeServices::CameraSettingsValue> RuntimeServices::GetCameraRenderingSettings(BindingCall& call,
                                                                                         const Handle& camera)
{
	auto& api = SceneApi(call);
	const auto* target =
	    dynamic_cast<const CameraComponent*>(TakeBindingResult(api.ResolveComponent(ReadManagedHandle(camera))));
	if (!target)
	{
		return Result<CameraSettingsValue>::Failure("Rendering settings require a CameraComponent");
	}
	const auto& source = target->rendering;
	CameraSettingsValue value;
	value.present = 1;
	value.F_Type = static_cast<UInt32>(source.type);
	value.F_Depth = source.depth;
	value.F_ClearFlags = static_cast<UInt32>(source.clearFlags);
	value.F_Background = WriteColor<Wire::Value_Ursa_Color>(source.background);
	value.F_CullingMask = source.cullingMask;
	value.F_Viewport = {source.viewport.x, source.viewport.y, source.viewport.width, source.viewport.height};
	value.F_TargetWidth = source.targetWidth;
	value.F_TargetHeight = source.targetHeight;
	return Result<CameraSettingsValue>::Success(value);
}

Result<void> RuntimeServices::SetCameraRenderingSettings(BindingCall& call, const Handle& camera,
                                                         const CameraSettingsValue& value)
{
	auto& api = SceneApi(call);
	auto* target = dynamic_cast<CameraComponent*>(TakeBindingResult(api.ResolveComponent(ReadManagedHandle(camera))));
	if (!target)
	{
		return Result<void>::Failure("Rendering settings require a CameraComponent");
	}
	Rendering::CameraSettings candidate;
	candidate.type = static_cast<Rendering::CameraType>(value.F_Type);
	candidate.depth = value.F_Depth;
	candidate.clearFlags = static_cast<Rendering::CameraClearFlags>(value.F_ClearFlags);
	candidate.background = ReadColor(value.F_Background);
	candidate.cullingMask = value.F_CullingMask;
	candidate.viewport = {value.F_Viewport.F_X, value.F_Viewport.F_Y, value.F_Viewport.F_Width,
	                      value.F_Viewport.F_Height};
	candidate.targetWidth = value.F_TargetWidth;
	candidate.targetHeight = value.F_TargetHeight;
	auto valid = candidate.Validate();
	if (!valid)
	{
		return valid;
	}
	target->rendering = candidate;
	return {};
}
} // namespace Ursa::Scripting::Bindings
