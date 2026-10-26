#include <Scripting/Bindings/EditorServices.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingMath.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Math/Camera.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa::Editor::Bindings
{
namespace
{
AssetPreviewExtension& Preview(B::BindingCall& call)
{
	auto* value = EditorServices::Scope(call).source.preview;
	if (!value)
	{
		throw std::runtime_error("This callback has no Preview capability");
	}
	return *value;
}

ViewportRect ReadRect(const EditorServices::RectValue& value)
{
	return {value.F_X, value.F_Y, value.F_Width, value.F_Height};
}
} // namespace

Result<UInt64> EditorServices::CreatePreviewUtility(B::BindingCall& call)
{
	return Result<UInt64>::Success(Preview(call).CreateUtility());
}

Result<void> EditorServices::DestroyPreviewUtility(B::BindingCall& call, UInt64 utility)
{
	Preview(call).DestroyUtility(utility);
	return {};
}

Result<void> EditorServices::BeginPreview(B::BindingCall& call, UInt64 utility, const RectValue& rect)
{
	Preview(call).BeginUtility(utility, ReadRect(rect));
	return {};
}

Result<void> EditorServices::SetPreviewCamera(B::BindingCall& call, UInt64 utility,
                                              const B::Wire::Value_Ursa_Editor_PreviewCamera& camera)
{
	Camera native;
	native.position = B::ReadVector3(camera.F_Position);
	native.target = B::ReadVector3(camera.F_Target);
	native.up = B::ReadVector3(camera.F_Up);
	native.verticalFovRadians = camera.F_FieldOfView * .01745329252f;
	native.nearPlane = camera.F_NearPlane;
	native.farPlane = camera.F_FarPlane;
	Preview(call).SetCamera(utility, native);
	return {};
}

Result<void> EditorServices::SetPreviewLighting(B::BindingCall& call, UInt64 utility, const ColorValue& ambient,
                                                Float32 intensity, const ColorValue& light, Float32 lightIntensity,
                                                const QuaternionValue& rotation)
{
	Preview(call).SetLighting(utility, B::ReadColor(ambient), intensity, B::ReadColor(light), lightIntensity,
	                          B::ReadQuaternion(rotation));
	return {};
}

Result<void> EditorServices::PreviewDrawMesh(B::BindingCall& call, UInt64 utility,
                                             const B::Wire::Value_Ursa_NativeHandle& mesh,
                                             const B::Wire::Value_Ursa_NativeHandle& material,
                                             const Vector3Value& position, const QuaternionValue& rotation,
                                             const Vector3Value& scale)
{
	auto& api = Scope(call).RuntimeApi();
	Preview(call).DrawMesh(utility, B::TakeBindingResult(api.ReadMesh(B::ReadManagedHandle(mesh))),
	                       B::TakeBindingResult(api.ReadMaterial(B::ReadManagedHandle(material))),
	                       {B::ReadVector3(position), B::ReadQuaternion(rotation), B::ReadVector3(scale)});
	return {};
}

Result<void> EditorServices::PreviewInstantiatePrefab(B::BindingCall& call, UInt64 utility, const B::Utf8View& guid,
                                                      UInt64 localFileId)
{
	Preview(call).Instantiate(utility, {B::TakeBindingResult(ResourceId::Parse(B::ReadString(guid))), localFileId});
	return {};
}

Result<void> EditorServices::EndPreview(B::BindingCall& call, UInt64 utility)
{
	Preview(call).EndUtility(utility);
	return {};
}

Result<void> EditorServices::DrawPreviewTexture(B::BindingCall& call, const RectValue& rect,
                                                const B::Wire::Value_Ursa_NativeHandle& texture)
{
	Preview(call).DrawTexture(B::TakeBindingResult(Scope(call).RuntimeApi().ReadTexture(B::ReadManagedHandle(texture))),
	                          ReadRect(rect));
	return {};
}

Result<void> EditorServices::DrawPreviewPixels(B::BindingCall& call, const RectValue& rect, UInt32 width, UInt32 height,
                                               const B::DataView& pixels, bool linear)
{
	if (!width || !height || UInt64(width) * height > B::maximumBytes / 16 || pixels.length != UInt64(width) * height)
	{
		return Result<void>::Failure("Preview dimensions or pixel count are invalid");
	}
	Vector<UInt8> bytes;
	bytes.reserve(Size(width) * height * 4);
	B::ReadArray<ColorValue, UInt8>(pixels,
	                                [&](const auto& color)
	                                {
		                                for (Float32 channel : {color.F_R, color.F_G, color.F_B, color.F_A})
		                                {
			                                if (!std::isfinite(channel))
			                                {
				                                throw std::runtime_error("Preview colors must be finite");
			                                }
			                                bytes.push_back(
			                                    static_cast<UInt8>(std::round(std::clamp(channel, 0.f, 1.f) * 255.f)));
		                                }
		                                return UInt8(0);
	                                });
	Preview(call).DrawTexture(
	    B::TakeBindingResult(MaterialTexture::Create(
	        width, height, linear ? TextureColorSpace::Linear : TextureColorSpace::Srgb, std::move(bytes))),
	    ReadRect(rect));
	return {};
}

Result<B::Wire::Value_Ursa_Editor_PreviewInput> EditorServices::GetPreviewInput(B::BindingCall& call)
{
	const auto& value = Preview(call);
	B::Wire::Value_Ursa_Editor_PreviewInput result{};
	result.F_MousePosition = {value.Mouse().x, value.Mouse().y};
	result.F_MouseDelta = {value.Delta().x, value.Delta().y};
	result.F_ScrollDelta = value.Wheel();
	result.F_Dragging = value.Dragging();
	result.F_Time = value.Time();
	result.F_DeltaTime = value.DeltaTime();
	return Result<B::Wire::Value_Ursa_Editor_PreviewInput>::Success(result);
}
} // namespace Ursa::Editor::Bindings
