#include <Scripting/ManagedSceneApi.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;
using Json = nlohmann::json;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

Json Response(NativeApiV5& api, const Json& request, bool succeeds = true)
{
	const std::string text = request.dump();
	UInt64 size = 0;
	Expect(api.resourceRequest(api.sceneContext, reinterpret_cast<const UInt8*>(text.data()), text.size(), nullptr, 0,
	                           &size) == 4,
	       "Resource sizing did not return a response");
	Vector<UInt8> bytes(Size(size), 0);
	Expect(api.resourceRequest(api.sceneContext, nullptr, 0, bytes.data(), size, &size) == 0,
	       "Response retrieval failed");
	auto response = Json::parse(bytes.begin(), bytes.end());
	Expect(response.contains("value") == succeeds, "Unexpected resource request result");
	return response.contains("value") ? response["value"] : response["error"];
}

void Run()
{
	Scene scene;
	ManagedSceneApi bridge(scene);
	NativeApiV5 table;
	table.size = sizeof(table);
	table = bridge.Bind(table);
	auto mesh = Response(table, {{"operation", "mesh.create"}});
	Json geometry{
	    {"positions", {{{"x", 0}, {"y", 0}, {"z", 0}}, {{"x", 1}, {"y", 0}, {"z", 0}}, {{"x", 0}, {"y", 1}, {"z", 0}}}},
	    {"normals", Json::array()},
	    {"uv0", Json::array()},
	    {"colors", Json::array()},
	    {"tangents", Json::array()},
	    {"indices", {0, 1, 2}},
	    {"subMeshes", Json::array()}};
	Response(table, {{"operation", "mesh.set"}, {"handle", mesh}, {"data", geometry}});
	const auto version = Response(table, {{"operation", "mesh.version"}, {"handle", mesh}});
	geometry["indices"][2] = 1234;
	Response(table, {{"operation", "mesh.set"}, {"handle", mesh}, {"data", geometry}}, false);
	Expect(Response(table, {{"operation", "mesh.version"}, {"handle", mesh}}) == version,
	       "Invalid managed Mesh data changed version");
	auto clone = Response(table, {{"operation", "mesh.clone"}, {"handle", mesh}});
	Expect(mesh != clone && Response(table, {{"operation", "mesh.get"}, {"handle", mesh}})["positions"].size() == 3,
	       "Mesh clone lost geometry or identity");
	Response(table, {{"operation", "mesh.destroy"}, {"handle", mesh}});
	Expect(Response(table, {{"operation", "resource.valid"}, {"handle", mesh}}) == false,
	       "Destroyed Mesh handle remained valid");
	auto reused = Response(table, {{"operation", "mesh.create"}});
	Expect(reused != mesh && reused["generation"] != mesh["generation"], "Freed slot did not advance generation");
	Response(table, {{"operation", "mesh.get"}, {"handle", mesh}}, false);
	Scene anotherScene;
	ManagedSceneApi anotherBridge(anotherScene);
	auto anotherTable = anotherBridge.Bind(NativeApiV5{});
	Response(anotherTable, {{"operation", "mesh.get"}, {"handle", clone}}, false);
	auto material = Response(table, {{"operation", "material.create"}});
	Response(table, {{"operation", "mesh.get"}, {"handle", material}}, false);
	Response(table, {{"operation", "material.color"},
	                 {"handle", material},
	                 {"data", {{"r", 0.3}, {"g", 0.8}, {"b", 0.1}, {"a", 1}}}});
	const auto previous = Response(table, {{"operation", "material.get"}, {"handle", material}});
	Response(table, {{"operation", "material.roughness"}, {"handle", material}, {"data", -1}}, false);
	Expect(Response(table, {{"operation", "material.get"}, {"handle", material}}) == previous,
	       "Invalid material mutation changed values");
	Response(table, {{"operation", "material.destroy"}, {"handle", material}});
	Response(table, {{"operation", "material.get"}, {"handle", material}}, false);
	const auto object = GameObject::Create("Object");
	Expect(bool(scene.AttachGameObject(object)), "Object attach failed");
	auto filter = object->AddComponent<MeshFilterComponent>().value;
	auto renderer = object->AddComponent<MeshRendererComponent>().value;
	const auto filterHandle = bridge.RegisterComponent(filter).value;
	const Json filterJson{{"sessionId", filterHandle.sessionId},
	                      {"index", filterHandle.index},
	                      {"generation", filterHandle.generation},
	                      {"kind", UInt32(filterHandle.kind)},
	                      {"reserved", 0}};
	Response(table, {{"operation", "binding.mesh.set"}, {"handle", filterJson}, {"data", clone}});
	auto instance = Response(table, {{"operation", "binding.mesh.get"}, {"handle", filterJson}, {"instance", true}});
	Expect(instance != clone &&
	           Response(table, {{"operation", "binding.mesh.get"}, {"handle", filterJson}, {"instance", true}}) ==
	               instance,
	       "Managed getter cloned Mesh more than once");
	Expect(Response(table, {{"operation", "binding.mesh.get"}, {"handle", filterJson}}) == instance,
	       "Shared binding did not follow independent Mesh");
	Expect(!object->AddComponent<MeshFilterComponent>() && !object->AddComponent<MeshRendererComponent>(),
	       "Duplicate component accepted");
	Expect(bool(renderer->SetEnabled(false)), "Renderer enabled setter failed");
	Expect(!filter->SetEnabled(false), "MeshFilter enabled setter succeeded");
}
} // namespace

int main()
{
	try
	{
		Run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
