#include <Serialization/Archive.hpp>
#include <Serialization/SerializableRegistry.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace Ursa;

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

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

class Counter final : public Serializable
{
public:
	Int64 count = 0;

	Result<void> Serialize(ArchiveWriter& writer) const override
	{
		return writer.Write("count", count);
	}

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext&) override
	{
		auto value = reader.Integer("count");
		if (!value)
		{
			return Result<void>::Failure(value.error);
		}
		count = value.value;
		return {};
	}
};
} // namespace

int main()
{
	try
	{
		ArchiveWriter writer;
		Done(writer.Write("title", String("测试资源")));
		Done(writer.Write("signed", Int64{-12}));
		Done(writer.Write("unsigned", (std::numeric_limits<UInt64>::max)()));
		Done(writer.Write("active", true));
		Vector<UInt8> pixels(80000, 123);
		Done(writer.WriteBytes("pixels", pixels));
		auto object = Take(writer.Object("child"));
		Done(object.Write("value", Float64{0.25}));
		auto array = Take(writer.Array("items"));
		for (int index = 0; index < 3; ++index)
		{
			auto item = Take(array.AddObject());
			Done(item.Write("index", static_cast<Int64>(index)));
		}
		for (int format = 0; format < 2; ++format)
		{
			auto bytes = format == 0 ? Take(JsonArchive::Encode(writer)) : Take(BinaryArchive::Encode(writer));
			auto reader = format == 0 ? Take(JsonArchive::Decode(bytes)) : Take(BinaryArchive::Decode(bytes));
			Expect(Take(reader.Text("title")) == "测试资源" && Take(reader.Integer("signed")) == -12 &&
			           Take(reader.Unsigned("unsigned")) == (std::numeric_limits<UInt64>::max)() &&
			           Take(reader.Bytes("pixels")) == pixels && Take(reader.Boolean("active")),
			       "Archive round trip failed");
			Expect(Take(Take(reader.Object("child")).Number("value")) == 0.25 &&
			           Take(Take(reader.Array("items"))[2].Integer("index")) == 2,
			       "Nested archive fields changed");
			Expect(!reader.Text("active") && !reader.Unsigned("signed") && !reader.Integer("missing"),
			       "Malformed archive fields were accepted");
		}
		Expect(!writer.Write("bad", (std::numeric_limits<Float64>::infinity)()), "Archive accepted infinity");
		Expect(!BinaryArchive::Decode({1, 2, 3}) && !JsonArchive::Decode({'[', ']'}), "Malformed archive accepted");
		SerializationContext context;
		SerializableRegistry registry;
		Done(RegisterBuiltinSerializableTypes(registry));
		for (const char* type : {"ursa.GameObject", "ursa.Scene", "ursa.PrefabAsset", "ursa.Material", "ursa.Texture"})
		{
			auto object = Take(registry.Create(type, 1, context));
			Expect(object->SerializationType() == type && object->SerializationVersion() == 1,
			       "Persistent model factory returned the wrong type/version");
		}
		Done(
		    registry.Register("test.Counter", 1, [](SerializationContext&)
		                      { return Result<std::shared_ptr<Serializable>>::Success(std::make_shared<Counter>()); }));
		Expect(!registry.Create("missing", 1, context) && !registry.Create("test.Counter", 2, context) &&
		           !context.Service<int>(),
		       "Missing type, version or service was accepted");
		auto instance = Take(registry.Create("test.Counter", 1, context));
		const auto id = ResourceId::Generate();
		bool bound = false;
		context.Defer(
		    [&]() -> Result<void>
		    {
			    auto object = context.Object<Counter>(id);
			    if (!object)
			    {
				    return Result<void>::Failure(object.error);
			    }
			    bound = true;
			    return {};
		    });
		Done(context.RegisterObject(id, *instance));
		Done(context.ResolveReferences());
		Expect(bound, "Deferred reference was not resolved after construction");
		context.Defer([&]() -> Result<void>
		              { return Result<void>::Failure(context.Object<Counter>(ResourceId::Generate()).error); });
		Expect(!context.ResolveReferences(), "Unresolved reference was accepted");
		std::cout << "Archive and Serializable registry tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
