#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/RendererComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>

namespace Ursa::Scripting
{
namespace
{
using Json = nlohmann::json;

Result<void> VisitResources(Json& value, const std::function<Result<void>(Json&)>& visit)
{
	if (value.is_object() && value.contains("$ursaReference"))
	{
		auto& reference = value["$ursaReference"];
		if (reference.is_object() && reference.contains("Kind") && reference["Kind"].is_string() &&
		    (reference["Kind"] == "Mesh" || reference["Kind"] == "Material"))
		{
			if (!reference.contains("Resource"))
			{
				return Result<void>::Failure("Script render resource has no binding");
			}
			return visit(reference);
		}
		return {};
	}
	if (value.is_object() || value.is_array())
	{
		for (auto& item : value)
		{
			auto result = VisitResources(item, visit);
			if (!result)
			{
				return result;
			}
		}
	}
	return {};
}
} // namespace

Result<void> ScriptBehaviourComponent::CollectFieldResources(DocumentResources& resources) const
{
	return fieldResources ? resources.Merge(*fieldResources) : Result<void>{};
}

Result<void> ScriptBehaviourComponent::ApplySerializedPayload(String payload, ResourceSession* resources)
{
	try
	{
		DocumentResources available;
		if (fieldResources)
		{
			auto merged = available.Merge(*fieldResources);
			if (!merged)
			{
				return merged;
			}
		}
		SerializationContext context(resources ? resources : fieldResourceSession);
		context.Provide<RenderResourceReferences>(available);
		Vector<GameObject::Ptr> pending;
		auto owner = GetGameObject();
		if (TryGetScene())
		{
			pending = TryGetScene()->RootGameObjects();
		}
		else if (owner)
		{
			while (owner->Parent())
			{
				owner = owner->Parent();
			}
			pending.push_back(owner);
		}
		while (!pending.empty())
		{
			auto object = std::move(pending.back());
			pending.pop_back();
			for (const auto& child : object->Children())
			{
				pending.push_back(child);
			}
			for (auto* component : object->Components())
			{
				ArchiveWriter writer;
				writer.SetContext(&context);
				MeshPtr mesh;
				if (auto* filter = dynamic_cast<MeshFilterComponent*>(component))
				{
					mesh = filter->SharedMesh();
				}
				if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(component))
				{
					mesh = skin->SharedMesh();
				}
				if (mesh && mesh->IsValid())
				{
					auto written = WriteMeshBinding(writer, "mesh", mesh);
					if (!written)
					{
						return written;
					}
				}
				if (auto* renderer = dynamic_cast<RendererComponent*>(component))
				{
					for (const auto& material : renderer->SharedMaterials())
					{
						if (material && material->IsValid())
						{
							auto written = WriteMaterialBinding(writer, "material", material);
							if (!written)
							{
								return written;
							}
						}
					}
				}
				if (auto* script = dynamic_cast<ScriptBehaviourComponent*>(component))
				{
					auto collected = script->CollectFieldResources(available);
					if (!collected)
					{
						return collected;
					}
				}
			}
		}
		auto document = Json::parse(ToStdString(payload));
		const auto previous = serializedPayload;
		auto previousResources = fieldResources;
		auto* previousSession = fieldResourceSession;
		auto bound = BindRenderResources(document, context);
		if (!bound)
		{
			return bound;
		}
		try
		{
			serializedPayload = ToUrsaString(document.dump());
			if (instanceToken)
			{
				auto restored = RestoreSerializedPayload();
				if (!restored)
				{
					serializedPayload = previous;
					fieldResources = std::move(previousResources);
					fieldResourceSession = previousSession;
					RestoreSerializedPayload();
					return restored;
				}
			}
		}
		catch (...)
		{
			serializedPayload = previous;
			fieldResources = std::move(previousResources);
			fieldResourceSession = previousSession;
			if (instanceToken)
			{
				RestoreSerializedPayload();
			}
			throw;
		}
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(String("Cannot apply script resource fields: ") + error.what());
	}
}

Result<void> ScriptBehaviourComponent::BindRenderResources(Json& payload, SerializationContext& context)
{
	auto candidate = MakeShared<DocumentResources>(Memory::MemoryTag::Assets);
	auto loaded = VisitResources(payload,
	                             [&](Json& reference) -> Result<void>
	                             {
		                             auto reader = ArchiveFromJson(Json{{"value", reference["Resource"]}});
		                             if (!reader)
		                             {
			                             return Result<void>::Failure(reader.error);
		                             }
		                             ArchiveWriter writer;
		                             SerializationContext bindings(context.ResourceLoader());
		                             bindings.Provide<RenderResourceReferences>(*candidate);
		                             writer.SetContext(&bindings);
		                             Result<void> result;
		                             if (reference["Kind"] == "Mesh")
		                             {
			                             auto mesh = ReadMeshBinding(reader.value, "value", context);
			                             if (!mesh)
			                             {
				                             return Result<void>::Failure(mesh.error);
			                             }
			                             result = WriteMeshBinding(writer, "value", mesh.value);
		                             }
		                             else
		                             {
			                             auto material = ReadMaterialBinding(reader.value, "value", context);
			                             if (!material)
			                             {
				                             return Result<void>::Failure(material.error);
			                             }
			                             result = WriteMaterialBinding(writer, "value", material.value);
		                             }
		                             if (!result)
		                             {
			                             return result;
		                             }
		                             auto rewritten = ArchiveToJson(writer);
		                             if (!rewritten)
		                             {
			                             return Result<void>::Failure(rewritten.error);
		                             }
		                             reference["Resource"] = std::move(rewritten.value["value"]);
		                             return {};
	                             });
	if (loaded)
	{
		fieldResources = std::move(candidate);
		fieldResourceSession = context.ResourceLoader();
	}
	return loaded;
}

Result<void> ScriptBehaviourComponent::SaveRenderResources(Json& payload, ArchiveWriter& writer) const
{
	return VisitResources(payload,
	                      [&](Json& reference) -> Result<void>
	                      {
		                      if (!writer.Context())
		                      {
			                      return Result<void>::Failure("Script resource save requires document context");
		                      }
		                      auto reader = ArchiveFromJson(Json{{"value", reference["Resource"]}});
		                      if (!reader)
		                      {
			                      return Result<void>::Failure(reader.error);
		                      }
		                      SerializationContext fields(fieldResourceSession);
		                      if (fieldResources)
		                      {
			                      fields.Provide<RenderResourceReferences>(*fieldResources);
		                      }
		                      // A live managed serializer has already registered new resources in the active document table.
		                      auto& source = instanceToken ? *writer.Context() : fields;
		                      ArchiveWriter binding;
		                      binding.SetContext(writer.Context());
		                      if (reference["Kind"] == "Mesh")
		                      {
			                      auto mesh = ReadMeshBinding(reader.value, "value", source);
			                      if (!mesh && reference["Resource"].value("kind", std::string{}) == "asset")
			                      {
				                      return {};
			                      }
			                      return mesh ? WriteMeshBinding(binding, "value", mesh.value)
			                                  : Result<void>::Failure(mesh.error);
		                      }
		                      auto material = ReadMaterialBinding(reader.value, "value", source);
		                      if (!material && reference["Resource"].value("kind", std::string{}) == "asset")
		                      {
			                      return {};
		                      }
		                      return material ? WriteMaterialBinding(binding, "value", material.value)
		                                      : Result<void>::Failure(material.error);
	                      });
}
} // namespace Ursa::Scripting
