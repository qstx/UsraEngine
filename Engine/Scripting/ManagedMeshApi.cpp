#include <Scripting/ManagedSceneApi.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Mesh/Primitive.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/RenderResourceReferences.hpp>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Ursa::Scripting
{
namespace
{
using Json = nlohmann::json;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

Json HandleJson(ManagedHandle handle)
{
	if (!handle.sessionId)
	{
		return nullptr;
	}
	return {{"sessionId", handle.sessionId},
	        {"index", handle.index},
	        {"generation", handle.generation},
	        {"kind", UInt32(handle.kind)},
	        {"reserved", handle.reserved}};
}

UInt64 Unsigned(const Json& value);

UInt32 Index(const Json& value);

ManagedHandle ReadHandle(const Json& value)
{
	if (value.is_null())
	{
		return {};
	}
	const auto session = Unsigned(value.at("sessionId"));
	if (!session)
	{
		throw std::runtime_error("Null resource references must use an explicit null value");
	}
	return {session, Index(value.at("index")), Index(value.at("generation")),
	        ManagedHandleKind(Index(value.at("kind"))), Index(value.at("reserved"))};
}

UInt64 Unsigned(const Json& value)
{
	if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<Int64>() >= 0))
	{
		throw std::runtime_error("Resource count/index requires an unsigned integer");
	}
	return value.get<UInt64>();
}

UInt32 Index(const Json& value)
{
	const auto number = Unsigned(value);
	if (number > std::numeric_limits<UInt32>::max())
	{
		throw std::runtime_error("Resource index exceeds 32-bit capacity");
	}
	return UInt32(number);
}

Int32 BaseVertex(const Json& value)
{
	if (!value.is_number_integer())
	{
		throw std::runtime_error("Base vertex requires an integer");
	}
	const auto number = value.get<Int64>();
	if (number < std::numeric_limits<Int32>::min() || number > std::numeric_limits<Int32>::max())
	{
		throw std::runtime_error("Base vertex exceeds 32-bit capacity");
	}
	return Int32(number);
}

Float32 Number(const Json& value)
{
	const auto number = value.get<Float64>();
	if (!value.is_number() || !std::isfinite(number) || std::abs(number) > std::numeric_limits<Float32>::max())
	{
		throw std::runtime_error("Resource value must be a finite 32-bit number");
	}
	return Float32(number);
}

template <class T, class Read> Vector<T> Channel(const Json& value, Read read)
{
	if (!value.is_array() || value.size() > Mesh::maxDataBytes / sizeof(T))
	{
		throw std::runtime_error("Mesh channel exceeds capacity or has an invalid shape");
	}
	Vector<T> values;
	values.reserve(value.size());
	for (const auto& item : value)
	{
		values.push_back(read(item));
	}
	return values;
}

Vec3 ReadVector3(const Json& v)
{
	return {Number(v.at("x")), Number(v.at("y")), Number(v.at("z"))};
}

Vec2 ReadVector2(const Json& v)
{
	return {Number(v.at("x")), Number(v.at("y"))};
}

Vec4 ReadVector4(const Json& v)
{
	return {Number(v.at("x")), Number(v.at("y")), Number(v.at("z")), Number(v.at("w"))};
}

Color ReadColor(const Json& v)
{
	return {Number(v.at("r")), Number(v.at("g")), Number(v.at("b")), Number(v.at("a"))};
}

SubMesh ReadSubMesh(const Json& v)
{
	return {Index(v.at("firstIndex")), Index(v.at("indexCount")), BaseVertex(v.at("baseVertex"))};
}

template <class T, class Write> Json ArrayJson(const Vector<T>& values, Write write)
{
	Json result = Json::array();
	for (const auto& value : values)
	{
		result.push_back(write(value));
	}
	return result;
}

Json GeometryJson(const MeshData& data)
{
	return {
	    {"positions", ArrayJson(data.positions, [](Vec3 v) { return Json{{"x", v.x}, {"y", v.y}, {"z", v.z}}; })},
	    {"normals", ArrayJson(data.normals, [](Vec3 v) { return Json{{"x", v.x}, {"y", v.y}, {"z", v.z}}; })},
	    {"uv0", ArrayJson(data.uv0, [](Vec2 v) { return Json{{"x", v.x}, {"y", v.y}}; })},
	    {"colors", ArrayJson(data.colors, [](Color v)
	                         { return Json{{"r", v.red}, {"g", v.green}, {"b", v.blue}, {"a", v.alpha}}; })},
	    {"tangents",
	     ArrayJson(data.tangents, [](Vec4 v) { return Json{{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}}; })},
	    {"indices", ArrayJson(data.indices, [](UInt32 v) { return Json(v); })},
	    {"subMeshes", ArrayJson(data.subMeshes,
	                            [](SubMesh v)
	                            {
		                            return Json{{"firstIndex", v.firstIndex},
		                                        {"indexCount", v.indexCount},
		                                        {"baseVertex", v.baseVertex}};
	                            })},
	    {"bounds",
	     {{"empty", data.bounds.empty},
	      {"minimum", {{"x", data.bounds.minimum.x}, {"y", data.bounds.minimum.y}, {"z", data.bounds.minimum.z}}},
	      {"maximum", {{"x", data.bounds.maximum.x}, {"y", data.bounds.maximum.y}, {"z", data.bounds.maximum.z}}}}}};
}

MeshData ReadGeometry(const Json& value, const MeshData* previous = nullptr)
{
	MeshData data;
	if (previous)
	{
		data.skeleton = previous->skeleton;
		data.skeletonReference = previous->skeletonReference;
		data.skinInfluences = previous->skinInfluences;
	}
	data.positions = Channel<Vec3>(value.at("positions"), ReadVector3);
	data.normals = Channel<Vec3>(value.at("normals"), ReadVector3);
	data.uv0 = Channel<Vec2>(value.at("uv0"), ReadVector2);
	data.colors = Channel<Color>(value.at("colors"), ReadColor);
	data.tangents = Channel<Vec4>(value.at("tangents"), ReadVector4);
	data.indices = Channel<UInt32>(value.at("indices"), [](const Json& v) { return Index(v); });
	data.subMeshes = Channel<SubMesh>(value.at("subMeshes"), ReadSubMesh);
	if (value.contains("bounds"))
	{
		const auto& bounds = value["bounds"];
		data.bounds = {ReadVector3(bounds.at("minimum")), ReadVector3(bounds.at("maximum")),
		               bounds.at("empty").get<bool>()};
	}
	return data;
}
} // namespace

Result<ManagedHandle> ManagedSceneApi::RegisterMesh(const MeshPtr& mesh)
{
	if (!mesh)
	{
		return Result<ManagedHandle>::Success({});
	}
	if (!mesh->IsValid())
	{
		return Result<ManagedHandle>::Failure("Mesh has been destroyed");
	}
	auto handle = handles->Register(mesh.get(), ManagedHandleKind::Mesh);
	if (handle)
	{
		ownedMeshes[handle.value.index] = mesh;
	}
	return handle;
}

Result<ManagedHandle> ManagedSceneApi::RegisterMaterial(const std::shared_ptr<MaterialInstance>& material)
{
	if (!material)
	{
		return Result<ManagedHandle>::Success({});
	}
	if (!material->IsValid())
	{
		return Result<ManagedHandle>::Failure("Material has been destroyed");
	}
	auto handle = handles->Register(material.get(), ManagedHandleKind::Material);
	if (handle)
	{
		ownedMaterials[handle.value.index] = material;
	}
	return handle;
}

Result<MeshPtr> ManagedSceneApi::ResolveMesh(const ManagedHandle& handle) const
{
	if (!handle.sessionId)
	{
		return Result<MeshPtr>::Success(nullptr);
	}
	auto valid = handles->Resolve(handle, ManagedHandleKind::Mesh);
	auto found = ownedMeshes.find(handle.index);
	return valid && found != ownedMeshes.end() && found->second->IsValid()
	           ? Result<MeshPtr>::Success(found->second)
	           : Result<MeshPtr>::Failure(valid ? "Mesh has been destroyed" : valid.error);
}

Result<std::shared_ptr<MaterialInstance>> ManagedSceneApi::ResolveMaterial(const ManagedHandle& handle) const
{
	if (!handle.sessionId)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(nullptr);
	}
	auto valid = handles->Resolve(handle, ManagedHandleKind::Material);
	auto found = ownedMaterials.find(handle.index);
	return valid && found != ownedMaterials.end() && found->second->IsValid()
	           ? Result<std::shared_ptr<MaterialInstance>>::Success(found->second)
	           : Result<std::shared_ptr<MaterialInstance>>::Failure(valid ? "Material has been destroyed"
	                                                                      : valid.error);
}

Int32 ManagedSceneApi::ResourceRequest(void* context, const UInt8* input, UInt64 length, UInt8* output, UInt64 capacity,
                                       UInt64* required)
{
	auto* api = FromContext(context);
	if (!api || !required || (!input && length) || (!output && capacity) || length > maxMeshResourceBytes)
	{
		return 1;
	}
	if (input)
	{
		try
		{
			auto request = Json::parse(input, input + length);
			api->resourceResponse = ToUrsaString(Json{{"value", api->DispatchResourceRequest(request)}}.dump());
		}
		catch (const std::exception& error)
		{
			api->resourceResponse = ToUrsaString(Json{{"error", error.what()}}.dump());
		}
	}
	// A sizing retry retrieves this exact result; it never repeats a mutation.
	*required = api->resourceResponse.size();
	if (capacity < *required)
	{
		return 4;
	}
	if (*required)
	{
		std::memcpy(output, api->resourceResponse.data(), Size(*required));
	}
	api->resourceResponse.clear();
	return 0;
}

nlohmann::json ManagedSceneApi::DispatchResourceRequest(const Json& request)
{
	const auto operation = request.at("operation").get<std::string>();
	const auto handle = ReadHandle(request.value("handle", Json(nullptr)));
	if (operation == "gameObject.primitive")
	{
		if (!resources)
		{
			throw std::runtime_error("Primitive creation requires a ResourceSession");
		}
		if (request.value("type", std::string{}) != "Cube")
		{
			throw std::runtime_error("Unsupported primitive type");
		}
		auto object = Take(CreatePrimitive(scene, *resources, PrimitiveType::Cube));
		auto registered = Register(object);
		if (!registered)
		{
			scene.DestroyGameObject(object);
			throw std::runtime_error(ToStdString(registered.error));
		}
		return HandleJson(registered.value);
	}
	if (operation == "mesh.create")
	{
		return HandleJson(Take(RegisterMesh(Take(Mesh::Create()))));
	}
	if (operation == "mesh.load")
	{
		if (!resources)
		{
			throw std::runtime_error("Mesh loading requires a ResourceSession");
		}
		auto guid = Take(ResourceId::Parse(ToUrsaString(request.at("guid").get<std::string>())));
		return HandleJson(Take(RegisterMesh(Take(resources->LoadMesh(guid, request.at("localFileId").get<UInt64>())))));
	}
	if (operation == "material.create")
	{
		const auto type = request.value("type", std::string("Unlit"));
		if (type != "Unlit" && type != "PbrMetallicRoughness")
		{
			throw std::runtime_error("Unknown Material type");
		}
		auto material = MakeShared<MaterialInstance>(
		    Memory::MemoryTag::Assets, type == "Unlit" ? MaterialType::Unlit : MaterialType::PbrMetallicRoughness);
		const auto shaderId = type == "Unlit" ? Shader::BuiltinUnlitShaderId() : Shader::BuiltinPbrShaderId();
		Done(material->MigrateLegacyToShader(shaderId));
		auto shader =
		    resources ? Take(resources->LoadShader(shaderId))
		              : MakeShared<const Shader::ShaderAsset>(Memory::MemoryTag::Assets,
		                                                      type == "Unlit" ? Shader::CreateBuiltinUnlitShaderAsset()
		                                                                      : Shader::CreateBuiltinPbrShaderAsset());
		Done(material->SetShaderAsset(std::move(shader)));
		return HandleJson(Take(RegisterMaterial(material)));
	}
	if (operation == "material.load")
	{
		if (!resources)
		{
			throw std::runtime_error("Material loading requires a ResourceSession");
		}
		auto guid = Take(ResourceId::Parse(ToUrsaString(request.at("guid").get<std::string>())));
		return HandleJson(
		    Take(RegisterMaterial(Take(resources->LoadMaterial(guid, request.at("localFileId").get<UInt64>())))));
	}
	if (operation == "resource.valid")
	{
		if (!handle.sessionId)
		{
			return false;
		}
		return handle.kind == ManagedHandleKind::Mesh
		           ? bool(ResolveMesh(handle))
		           : handle.kind == ManagedHandleKind::Material && bool(ResolveMaterial(handle));
	}
	if (operation == "resource.reference")
	{
		ArchiveWriter writer;
		writer.SetContext(SerializationContext::Active());
		if (handle.kind == ManagedHandleKind::Mesh)
		{
			auto mesh = Take(ResolveMesh(handle));
			if (mesh && !mesh->AssetIdentity().guid && !writer.Context())
			{
				return {{"kind", "local"}, {"id", ToStdString(mesh->LocalIdentity().ToString())}};
			}
			Done(WriteMeshBinding(writer, "value", mesh));
		}
		else if (handle.kind == ManagedHandleKind::Material)
		{
			auto material = Take(ResolveMaterial(handle));
			if (material && !material->AssetIdentity().guid && !writer.Context())
			{
				return {{"kind", "local"}, {"id", ToStdString(material->LocalIdentity().ToString())}};
			}
			Done(WriteMaterialBinding(writer, "value", material));
		}
		else
		{
			throw std::runtime_error("Invalid render resource reference kind");
		}
		return Take(ArchiveToJson(writer))["value"];
	}
	if (operation == "resource.restore")
	{
		auto reader = Take(ArchiveFromJson(Json{{"value", request.at("data")}}));
		SerializationContext fallback(resources);
		auto* active = SerializationContext::Active();
		if (active && !active->ResourceLoader())
		{
			auto service = active->Service<RenderResourceReferences>();
			if (service)
			{
				fallback.Provide<RenderResourceReferences>(*service.value);
			}
		}
		auto& context = active && active->ResourceLoader() ? *active : fallback;
		const auto type = request.at("type").get<std::string>();
		if (type == "Mesh")
		{
			return HandleJson(Take(RegisterMesh(Take(ReadMeshBinding(reader, "value", context)))));
		}
		if (type == "Material")
		{
			return HandleJson(Take(RegisterMaterial(Take(ReadMaterialBinding(reader, "value", context)))));
		}
		throw std::runtime_error("Unsupported render resource restore kind");
	}
	if (operation.rfind("mesh.", 0) == 0)
	{
		auto mesh = Take(ResolveMesh(handle));
		if (!mesh)
		{
			throw std::runtime_error("Mesh handle cannot be null");
		}
		if (operation == "mesh.clone")
		{
			return HandleJson(Take(RegisterMesh(Take(mesh->Clone()))));
		}
		if (operation == "mesh.destroy")
		{
			Done(mesh->Destroy());
			Done(handles->Invalidate(handle));
			ownedMeshes.erase(handle.index);
			return nullptr;
		}
		if (operation == "mesh.get")
		{
			return GeometryJson(Take(mesh->GetData()));
		}
		if (operation == "mesh.version")
		{
			return mesh->Version();
		}
		if (operation == "mesh.set")
		{
			auto previous = Take(mesh->GetData());
			Done(mesh->SetData(ReadGeometry(request.at("data"), &previous)));
		}
		else if (operation == "mesh.vertices")
		{
			Done(mesh->SetVertices(Channel<Vec3>(request.at("data"), ReadVector3)));
		}
		else if (operation == "mesh.normals")
		{
			Done(mesh->SetNormals(Channel<Vec3>(request.at("data"), ReadVector3)));
		}
		else if (operation == "mesh.uv0")
		{
			Done(mesh->SetUv0(Channel<Vec2>(request.at("data"), ReadVector2)));
		}
		else if (operation == "mesh.colors")
		{
			Done(mesh->SetColors(Channel<Color>(request.at("data"), ReadColor)));
		}
		else if (operation == "mesh.tangents")
		{
			Done(mesh->SetTangents(Channel<Vec4>(request.at("data"), ReadVector4)));
		}
		else if (operation == "mesh.indices")
		{
			Done(mesh->SetIndices(Channel<UInt32>(request.at("data"), [](const Json& v) { return Index(v); })));
		}
		else if (operation == "mesh.subMeshCount")
		{
			Done(mesh->SetSubMeshCount(Size(Unsigned(request.at("data")))));
		}
		else if (operation == "mesh.subMesh")
		{
			Done(mesh->SetSubMesh(Size(Unsigned(request.at("index"))), ReadSubMesh(request.at("data"))));
		}
		else if (operation == "mesh.triangles.get")
		{
			return ArrayJson(Take(mesh->GetTriangles(Size(Unsigned(request.at("index"))))),
			                 [](UInt32 v) { return Json(v); });
		}
		else if (operation == "mesh.triangles.set")
		{
			Done(mesh->SetTriangles(Size(Unsigned(request.at("index"))),
			                        Channel<UInt32>(request.at("data"), [](const Json& v) { return Index(v); })));
		}
		else if (operation == "mesh.clear")
		{
			Done(mesh->Clear());
		}
		else if (operation == "mesh.recalculateNormals")
		{
			Done(mesh->RecalculateNormals());
		}
		else if (operation == "mesh.recalculateBounds")
		{
			Done(mesh->RecalculateBounds());
		}
		else
		{
			throw std::runtime_error("Unsupported Mesh operation");
		}
		return nullptr;
	}
	if (operation.rfind("binding.", 0) == 0)
	{
		auto* component = Take(ResolveComponent(handle));
		if (operation == "binding.mesh.get")
		{
			MeshPtr mesh;
			if (auto* filter = dynamic_cast<MeshFilterComponent*>(component))
			{
				mesh = request.value("instance", false) ? Take(filter->Mesh()) : filter->SharedMesh();
			}
			else if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(component))
			{
				mesh = skin->SharedMesh();
			}
			else
			{
				throw std::runtime_error("Component has no Mesh binding");
			}
			return HandleJson(Take(RegisterMesh(mesh)));
		}
		if (operation == "binding.mesh.set")
		{
			auto mesh = Take(ResolveMesh(ReadHandle(request.at("data"))));
			if (auto* filter = dynamic_cast<MeshFilterComponent*>(component))
			{
				Done(request.value("instance", false) ? filter->SetMesh(std::move(mesh))
				                                      : filter->SetSharedMesh(std::move(mesh)));
			}
			else if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(component))
			{
				Done(skin->SetSharedMesh(std::move(mesh)));
			}
			else
			{
				throw std::runtime_error("Component has no Mesh binding");
			}
			return nullptr;
		}
		auto* renderer = dynamic_cast<RendererComponent*>(component);
		if (!renderer)
		{
			throw std::runtime_error("Component is not a Renderer");
		}
		if (operation == "binding.materials.get")
		{
			const bool instance = request.value("instance", false);
			const bool first = request.value("first", false);
			Vector<std::shared_ptr<MaterialInstance>> values;
			if (first)
			{
				values = {instance ? Take(renderer->Material()) : renderer->SharedMaterial()};
			}
			else
			{
				values = instance ? Take(renderer->Materials()) : renderer->SharedMaterials();
			}
			Json result = Json::array();
			for (const auto& value : values)
			{
				result.push_back(HandleJson(Take(RegisterMaterial(value))));
			}
			return result;
		}
		if (operation == "binding.materials.set")
		{
			const auto& data = request.at("data");
			if (!data.is_array() || data.size() > Mesh::maxSubMeshes)
			{
				throw std::runtime_error("Renderer slot capacity exceeded");
			}
			Vector<std::shared_ptr<MaterialInstance>> values;
			for (const auto& item : data)
			{
				values.push_back(Take(ResolveMaterial(ReadHandle(item))));
			}
			const bool shared = request.value("shared", true);
			if (request.value("first", false))
			{
				if (values.size() != 1)
				{
					throw std::runtime_error("Single material setter needs one value");
				}
				Done(shared ? renderer->SetSharedMaterial(values[0]) : renderer->SetMaterial(values[0]));
			}
			else
			{
				Done(shared ? renderer->SetSharedMaterials(std::move(values))
				            : renderer->SetMaterials(std::move(values)));
			}
			return nullptr;
		}
	}
	if (operation.rfind("material.", 0) == 0)
	{
		auto material = Take(ResolveMaterial(handle));
		if (!material)
		{
			throw std::runtime_error("Material handle cannot be null");
		}
		if (operation == "material.clone")
		{
			return HandleJson(Take(RegisterMaterial(material->Clone())));
		}
		if (operation == "material.destroy")
		{
			Done(material->Destroy());
			Done(handles->Invalidate(handle));
			ownedMaterials.erase(handle.index);
			return nullptr;
		}
		if (operation == "material.get")
		{
			const auto& p = material->Parameters();
			return {
			    {"baseColor",
			     {{"r", p.baseColor.red}, {"g", p.baseColor.green}, {"b", p.baseColor.blue}, {"a", p.baseColor.alpha}}},
			    {"metallic", p.metallic},
			    {"roughness", p.roughness},
			    {"normalScale", p.normalScale},
			    {"occlusionStrength", p.occlusionStrength},
			    {"alphaCutoff", p.alphaCutoff},
			    {"alphaMode", UInt32(p.alphaMode)},
			    {"emissive",
			     {{"r", p.emissive.red}, {"g", p.emissive.green}, {"b", p.emissive.blue}, {"a", p.emissive.alpha}}}};
		}
		if (operation == "material.texture.get")
		{
			const auto reference = material->TextureReference(MaterialTextureSlot(Index(request.at("slot"))));
			return reference.guid ? AssetReferenceToJson(reference) : Json(nullptr);
		}
		if (operation == "material.shader.get")
		{
			return material->ShaderId() ? AssetReferenceToJson({material->ShaderId(), 0}) : Json(nullptr);
		}
		if (operation == "material.keywords")
		{
			return ArrayJson(material->Keywords(), [](const String& v) { return Json(ToStdString(v)); });
		}
		if (operation == "material.sampler.get")
		{
			const auto sampler = material->Parameters().sampler;
			return {{"linear", sampler.linear}, {"repeat", sampler.repeat}};
		}
		if (operation == "material.property.get" || operation == "material.property.set" ||
		    operation == "material.property.remove")
		{
			const String name = ToUrsaString(request.at("name").get<std::string>());
			auto shader = material->Shader();
			if (!shader && resources)
			{
				shader = Take(resources->LoadShader(material->ShaderId()));
			}
			if (!shader)
			{
				throw std::runtime_error("Material property access requires its Shader metadata");
			}
			auto current = Take(material->PropertyValue(name, *shader));
			if (operation == "material.property.remove")
			{
				Done(material->RemovePropertyOverride(name));
				return nullptr;
			}
			if (operation == "material.property.get")
			{
				if (current.type == Shader::PropertyType::Float || current.type == Shader::PropertyType::Range)
				{
					return current.floatValue;
				}
				if (current.type == Shader::PropertyType::Int)
				{
					return current.intValue;
				}
				if (current.type == Shader::PropertyType::Texture2D)
				{
					return current.textureId ? AssetReferenceToJson({current.textureId, current.textureLocalFileId})
					                         : Json(nullptr);
				}
				if (current.type == Shader::PropertyType::Color)
				{
					return {{"r", current.vectorValue[0]},
					        {"g", current.vectorValue[1]},
					        {"b", current.vectorValue[2]},
					        {"a", current.vectorValue[3]}};
				}
				return {{"x", current.vectorValue[0]},
				        {"y", current.vectorValue[1]},
				        {"z", current.vectorValue[2]},
				        {"w", current.vectorValue[3]}};
			}
			const auto type = request.at("type").get<std::string>();
			const auto& data = request.at("data");
			MaterialPropertyValue value;
			value.type = current.type;
			if (type == "Float" &&
			    (current.type == Shader::PropertyType::Float || current.type == Shader::PropertyType::Range))
			{
				value.floatValue = Number(data);
			}
			else if (type == "Int" && current.type == Shader::PropertyType::Int)
			{
				value.intValue = BaseVertex(data);
			}
			else if (type == "Color" && current.type == Shader::PropertyType::Color)
			{
				const auto c = ReadColor(data);
				value.vectorValue = {c.red, c.green, c.blue, c.alpha};
			}
			else if (type == "Vector" && current.type == Shader::PropertyType::Vector)
			{
				const auto v = ReadVector4(data);
				value.vectorValue = {v.x, v.y, v.z, v.w};
			}
			else if (type == "Texture2D" && current.type == Shader::PropertyType::Texture2D)
			{
				if (!data.is_null())
				{
					if (!resources)
					{
						throw std::runtime_error("Texture property binding requires a ResourceSession");
					}
					const auto reference = Take(AssetReferenceFromJson(data, "Texture property"));
					value.textureId = reference.guid;
					value.textureLocalFileId = reference.localFileId;
					value.texture = Take(resources->LoadTexture(reference.guid, reference.localFileId));
				}
				else
				{
					value.textureDefault = "white";
				}
			}
			else
			{
				throw std::runtime_error("Shader property type does not match the requested setter");
			}
			Done(material->SetPropertyValue(name, *shader, std::move(value)));
			return nullptr;
		}
		const auto& value = request.at("data");
		if (operation == "material.shader.set")
		{
			if (!resources)
			{
				throw std::runtime_error("Shader assignment requires a ResourceSession");
			}
			const auto reference = Take(AssetReferenceFromJson(value, "Material Shader"));
			if (reference.localFileId)
			{
				throw std::runtime_error("Shader sub-assets are not supported");
			}
			Done(material->SetShaderAsset(Take(resources->LoadShader(reference.guid))));
		}
		else if (operation == "material.texture.set")
		{
			AssetReference reference;
			MaterialTexturePtr texture;
			if (!value.is_null())
			{
				if (!resources)
				{
					throw std::runtime_error("Texture assignment requires a ResourceSession");
				}
				reference = Take(AssetReferenceFromJson(value, "Material Texture"));
				texture = Take(resources->LoadTexture(reference.guid, reference.localFileId));
			}
			Done(material->SetTextureReference(MaterialTextureSlot(Index(request.at("slot"))), reference,
			                                   std::move(texture)));
		}
		else if (operation == "material.sampler.set")
		{
			Done(material->SetSampler({value.at("linear").get<bool>(), value.at("repeat").get<bool>()}));
		}
		else if (operation == "material.color")
		{
			Done(material->SetBaseColor(ReadColor(value)));
		}
		else if (operation == "material.metallic")
		{
			Done(material->SetMetallic(Number(value)));
		}
		else if (operation == "material.roughness")
		{
			Done(material->SetRoughness(Number(value)));
		}
		else if (operation == "material.normalScale")
		{
			Done(material->SetNormalScale(Number(value)));
		}
		else if (operation == "material.occlusionStrength")
		{
			Done(material->SetOcclusionStrength(Number(value)));
		}
		else if (operation == "material.emissive")
		{
			Done(material->SetEmissive(ReadColor(value)));
		}
		else if (operation == "material.alphaMode")
		{
			Done(material->SetAlphaMode(AlphaMode(value.get<UInt32>()), material->Parameters().alphaCutoff));
		}
		else if (operation == "material.alphaCutoff")
		{
			Done(material->SetAlphaMode(material->Parameters().alphaMode, Number(value)));
		}
		else if (operation == "material.keyword")
		{
			Done(material->SetKeywordEnabled(ToUrsaString(request.at("name").get<std::string>()), value.get<bool>()));
		}
		else
		{
			throw std::runtime_error("Unsupported Material operation");
		}
		return nullptr;
	}
	throw std::runtime_error("Unsupported resource request " + operation);
}
} // namespace Ursa::Scripting
