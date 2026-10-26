#pragma once

#include <BindingTypes.generated.hpp>
#include <Scripting/ManagedEditorBridge.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Scripting/ManagedSceneApi.hpp>

namespace Ursa::Editor::Bindings
{
namespace B = Ursa::Scripting::Bindings;

class EditorInvocation final : public B::BindingContext
{
public:
	EditorInvocation(ManagedEditorContext& editorContext, UInt64 scriptSession);
	~EditorInvocation() override;
	void Require(UInt32 policy) const override;
	void CheckHandle(const void* value, Size size, UInt32 kind) const override;
	UInt64 SessionIdentity() const override;
	UInt64 TransferBudget() const override;
	void RequireProperties(UInt64 expected) const;
	ResourceSession& Resources();
	UInt64 Generation() const;
	Scripting::ManagedSceneApi& RuntimeApi();
	Result<bool> SynchronizeRuntimeTarget();
	void* hostToken = nullptr;
	ManagedEditorContext& source;
	std::unique_ptr<SerializedObject> properties;
	Vector<std::unique_ptr<SerializedObject>> additionalProperties;
	std::unique_ptr<SerializedObjectGroup> propertyGroup;
	UInt64 revision = 1;
	UInt64 generation = 0;
	String expectedAssetType;
	String expectedObjectType;
	Vector<String> pendingPropertyPaths;
	Vector<String> appliedPropertyPaths;
	Int32 groups = 0;
	bool changed = false;

private:
	friend struct EditorServices;
	UInt64 session;
	std::weak_ptr<GameObject> targetOwner;
	ResourceId targetId;
	bool attachedTarget = false;
	std::unique_ptr<ResourceSession> fallbackResources;
	Scene privateScene;
	std::unique_ptr<Scripting::ManagedSceneApi> runtimeApi;
	std::shared_ptr<MaterialInstance> runtimeMaterial;
	nlohmann::json runtimeMaterialBaseline;
};

struct EditorReferenceData
{
	UInt32 kind = 0;
	String id;
	AssetReference asset;
	bool hasAsset = false;
	String typeId;
};

struct EditorAssetData
{
	EditorReferenceData reference;
	String path;
	String name;
	bool isSubAsset = false;
};

struct EditorServices
{
	using RectValue = B::Wire::Value_Ursa_Rect;
	static Result<UInt64> CreatePreviewUtility(B::BindingCall& call);
	static Result<void> DestroyPreviewUtility(B::BindingCall& call, UInt64 utility);
	static Result<void> BeginPreview(B::BindingCall& call, UInt64 utility, const RectValue& rect);
	static Result<void> SetPreviewCamera(B::BindingCall& call, UInt64 utility,
	                                     const B::Wire::Value_Ursa_Editor_PreviewCamera& camera);
	static Result<void> SetPreviewLighting(B::BindingCall& call, UInt64 utility,
	                                       const B::Wire::Value_Ursa_Color& ambient, Float32 intensity,
	                                       const B::Wire::Value_Ursa_Color& light, Float32 lightIntensity,
	                                       const B::Wire::Value_Ursa_Quaternion& rotation);
	static Result<void>
	PreviewDrawMesh(B::BindingCall& call, UInt64 utility, const B::Wire::Value_Ursa_NativeHandle& mesh,
	                const B::Wire::Value_Ursa_NativeHandle& material, const B::Wire::Value_Ursa_Vector3& position,
	                const B::Wire::Value_Ursa_Quaternion& rotation, const B::Wire::Value_Ursa_Vector3& scale);
	static Result<void> PreviewInstantiatePrefab(B::BindingCall& call, UInt64 utility, const B::Utf8View& guid,
	                                             UInt64 localFileId);
	static Result<void> EndPreview(B::BindingCall& call, UInt64 utility);
	static Result<void> DrawPreviewTexture(B::BindingCall& call, const RectValue& rect,
	                                       const B::Wire::Value_Ursa_NativeHandle& texture);
	static Result<void> DrawPreviewPixels(B::BindingCall& call, const RectValue& rect, UInt32 width, UInt32 height,
	                                      const B::DataView& pixels, bool linear);
	static Result<B::Wire::Value_Ursa_Editor_PreviewInput> GetPreviewInput(B::BindingCall& call);
	using Reference = B::Wire::Value_Ursa_Editor_EditorReferenceValue;
	using OptionalReference = B::Wire::Value_Ursa_Editor_EditorReferenceValue_;
	using OptionalAsset = B::Wire::Value_Ursa_NativeAssetReference_;
	using PropertyState = B::Wire::Value_Ursa_Editor_SerializedPropertyState;
	using PropertyValue = B::Wire::Value_Ursa_Editor_SerializedValue;
	using Vector2Value = B::Wire::Value_Ursa_Vector2;
	using Vector3Value = B::Wire::Value_Ursa_Vector3;
	using Vector4Value = B::Wire::Value_Ursa_Vector4;
	using QuaternionValue = B::Wire::Value_Ursa_Quaternion;
	using ColorValue = B::Wire::Value_Ursa_Color;
	static EditorInvocation& Scope(B::BindingCall& call);
	static Reference WriteReference(B::BindingCall& call, const EditorReferenceData& reference);
	static EditorReferenceData ReadReference(EditorInvocation& scope, const Reference& reference);
	static EditorReferenceData Resolve(EditorInvocation& scope, UInt32 kind, String id, AssetReference asset = {});
	static Vector<EditorAssetData> AssetChoices(EditorInvocation& scope);
	static void RequireGui(B::BindingCall& call);
	static nlohmann::json Descriptor(EditorInvocation& scope, UInt64 revision, const String& path);
	static UInt32 PropertyType(const nlohmann::json& descriptor);
	static UInt32 ReferenceKind(const nlohmann::json& descriptor);
	static PropertyValue ReadValue(B::BindingCall& call, const nlohmann::json& value, const nlohmann::json& descriptor);
	static nlohmann::json WriteValue(EditorInvocation& scope, const PropertyValue& value,
	                                 const nlohmann::json& descriptor, bool validateReference = true);
	static void DrawProperty(B::BindingCall& call, const String& path, const String& label);

	static Result<B::Wire::Value_Ursa_Editor_EditorScopeValue> GetScope(B::BindingCall& call);
	static Result<B::Wire::Value_Ursa_EditorBindingCapability> GetRuntimeCapability(B::BindingCall& call);
	static Result<B::Wire::Value_Ursa_NativeHandle> GetTargetHandle(B::BindingCall& call);
	static Result<B::DataView> GetTargetHandles(B::BindingCall& call);
	static Result<UInt32> GetTargetPrimaryIndex(B::BindingCall& call);
	static Result<B::Utf8View> GetComponentPayload(B::BindingCall& call,
	                                               const B::Wire::Value_Ursa_NativeHandle& component);
	static Result<bool> ApplyComponentFields(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& component,
	                                         const B::Utf8View& fields);
	static Result<B::Utf8View> GetTargetPayload(B::BindingCall& call);
	static Result<bool> ApplyTargetFields(B::BindingCall& call, const B::Utf8View& fields);
	static Result<B::Wire::Value_Ursa_Editor_EditorImporterInfo_> GetImporterInfo(B::BindingCall& call);
	static Result<bool> HasModifiedImporterSettings(B::BindingCall& call);
	static Result<bool> CanApplyImporterSettings(B::BindingCall& call);
	static Result<bool> ApplyImporterSettings(B::BindingCall& call);
	static Result<void> RevertImporterSettings(B::BindingCall& call);
	static Result<void> MarkImporterControlsDrawn(B::BindingCall& call);
	static Result<B::DataView> GetSelection(B::BindingCall& call);
	static Result<OptionalReference> GetTarget(B::BindingCall& call);
	static Result<B::Wire::Value_Ursa_Editor_EditorDocumentValue_> GetDocument(B::BindingCall& call);
	static Result<B::DataView> GetAssets(B::BindingCall& call);
	static Result<Reference> ResolveReference(B::BindingCall& call, UInt32 kind, const B::Utf8View& id,
	                                          const OptionalAsset& asset);
	static Result<void> MarkDirty(B::BindingCall& call);
	static Result<void> Save(B::BindingCall& call);
	static Result<void> OpenAsset(B::BindingCall& call, const B::Utf8View& path);
	static Result<void> SetActiveCamera(B::BindingCall& call);
	static Result<void> Text(B::BindingCall& call, const B::Utf8View& text);
	static Result<bool> Button(B::BindingCall& call, const B::Utf8View& label);
	static Result<bool> SmallButton(B::BindingCall& call, const B::Utf8View& label);
	static Result<void> SameLine(B::BindingCall& call);
	static Result<void> Separator(B::BindingCall& call);
	static Result<void> BeginGroup(B::BindingCall& call);
	static Result<void> EndGroup(B::BindingCall& call);
	static Result<bool> Toggle(B::BindingCall& call, const B::Utf8View& label, bool value, bool mixedValue);
	static Result<Int64> IntField(B::BindingCall& call, const B::Utf8View& label, Int64 value);
	static Result<UInt64> ULongField(B::BindingCall& call, const B::Utf8View& label, UInt64 value);
	static Result<Float64> FloatField(B::BindingCall& call, const B::Utf8View& label, Float64 value);
	static Result<Float64> SliderFloat(B::BindingCall& call, const B::Utf8View& label, Float64 value,
	                                   Float64 minimum, Float64 maximum);
	static Result<Float64> DragFloat(B::BindingCall& call, const B::Utf8View& label, Float64 value, Float64 speed,
	                                 Float64 minimum, Float64 maximum, const B::Utf8View& format, bool mixedValue);
	static Result<B::Utf8View> TextField(B::BindingCall& call, const B::Utf8View& label, const B::Utf8View& value);
	static Result<Int32> Popup(B::BindingCall& call, const B::Utf8View& label, Int32 selected,
	                           const B::DataView& options, bool mixedValue);
	static Result<Vector2Value> Vector2Field(B::BindingCall& call, const B::Utf8View& label, const Vector2Value& value);
	static Result<Vector3Value> Vector3Field(B::BindingCall& call, const B::Utf8View& label, const Vector3Value& value);
	static Result<Vector4Value> Vector4Field(B::BindingCall& call, const B::Utf8View& label, const Vector4Value& value);
	static Result<QuaternionValue> QuaternionField(B::BindingCall& call, const B::Utf8View& label,
	                                               const QuaternionValue& value);
	static Result<ColorValue> ColorField(B::BindingCall& call, const B::Utf8View& label, const ColorValue& value,
	                                     bool mixedValue, bool hdr);
	static Result<OptionalReference> ObjectField(B::BindingCall& call, const B::Utf8View& label,
	                                             const OptionalReference& value, UInt32 kind,
	                                             const B::Utf8View& expectedType);
	static Result<OptionalReference> AssetField(B::BindingCall& call, const B::Utf8View& label,
	                                            const OptionalReference& value, UInt32 kind,
	                                            const B::Utf8View& expectedType);
	static Result<PropertyState> GetProperty(B::BindingCall& call, UInt64 revision, const B::Utf8View& path);
	static Result<void> SetProperty(B::BindingCall& call, UInt64 revision, const B::Utf8View& path,
	                                const PropertyValue& value);
	static Result<void> ResizeArray(B::BindingCall& call, UInt64 revision, const B::Utf8View& path, Int32 size);
	static Result<void> MoveArrayElement(B::BindingCall& call, UInt64 revision, const B::Utf8View& path,
	                                     Int32 fromIndex, Int32 toIndex);
	static Result<UInt64> UpdateProperties(B::BindingCall& call);
	static Result<bool> ApplyProperties(B::BindingCall& call, UInt64 revision);
	static Result<bool> HasModifiedProperties(B::BindingCall& call, UInt64 revision);
	static Result<B::DataView> GetPropertyPaths(B::BindingCall& call, UInt64 revision);
	static Result<void> PropertyField(B::BindingCall& call, UInt64 revision, const B::Utf8View& path,
	                                  const B::Utf8View& label);
	static Result<bool> DrawDefaultInspector(B::BindingCall& call, UInt64 revision);
};
} // namespace Ursa::Editor::Bindings
