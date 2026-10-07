#include <Serialization/Archive.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Core/StringInterop.hpp>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace Ursa
{
using Json = nlohmann::json;

struct ArchiveStorage
{
	Json root = Json::object();
};

struct ArchiveAccess
{
	static Json& Node(ArchiveWriter& writer)
	{
		return writer.storage->root[Json::json_pointer(ToStdString(writer.pointer))];
	}

	static const Json& Node(const ArchiveWriter& writer)
	{
		return writer.storage->root.at(Json::json_pointer(ToStdString(writer.pointer)));
	}

	static const Json& Node(const ArchiveReader& reader)
	{
		if (!reader.storage)
		{
			throw std::runtime_error("ArchiveReader has no document");
		}
		return reader.storage->root.at(Json::json_pointer(ToStdString(reader.pointer)));
	}

	static ArchiveReader Reader(const Json& document)
	{
		auto storage = std::make_shared<ArchiveStorage>();
		storage->root = document;
		return ArchiveReader(std::move(storage), {});
	}
};

namespace
{
String ChildPointer(const String& parent, const String& field)
{
	String escaped;
	for (char value : field)
	{
		escaped += value == '~' ? "~0" : value == '/' ? "~1" : String(1, value);
	}
	return parent + "/" + escaped;
}

template <class F> Result<void> Put(ArchiveWriter& writer, const String& field, F&& value)
{
	try
	{
		auto& object = ArchiveAccess::Node(writer);
		if (!object.is_object() || field.empty())
		{
			return Result<void>::Failure("Archive field requires an object and a nonempty name");
		}
		object[ToStdString(field)] = std::forward<F>(value);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}

template <class T, class F> Result<T> ReadField(const ArchiveReader& reader, const String& field, F&& convert)
{
	try
	{
		const auto& object = ArchiveAccess::Node(reader);
		if (!object.is_object() || !object.contains(ToStdString(field)))
		{
			return Result<T>::Failure("Missing archive field " + field);
		}
		return convert(object.at(ToStdString(field)));
	}
	catch (const std::exception& error)
	{
		return Result<T>::Failure("Invalid archive field " + field + ": " + error.what());
	}
}

Result<Float64> ReadNumber(const Json& value)
{
	if (!value.is_number() || !std::isfinite(value.get<Float64>()))
	{
		return Result<Float64>::Failure("Expected a finite archive number");
	}
	return Result<Float64>::Success(value.get<Float64>());
}

Result<String> ReadText(const Json& value)
{
	return value.is_string() ? Result<String>::Success(ToUrsaString(value.get<std::string>()))
	                         : Result<String>::Failure("Expected archive text");
}

Json JsonBlobs(Json value, bool decode)
{
	if (!decode && value.is_binary())
	{
		const auto& bytes = value.get_binary();
		return Json{{"$ursaBytes", std::vector<UInt8>(bytes.begin(), bytes.end())}};
	}
	if (decode && value.is_object() && value.size() == 1 && value.contains("$ursaBytes"))
	{
		const auto& input = value["$ursaBytes"];
		if (!input.is_array())
		{
			throw std::runtime_error("Invalid JSON archive byte block");
		}
		std::vector<UInt8> bytes;
		for (const auto& item : input)
		{
			if (!item.is_number_integer() || item.get<Int64>() < 0 || item.get<UInt64>() > 255)
			{
				throw std::runtime_error("Invalid JSON archive byte value");
			}
			bytes.push_back(item.get<UInt8>());
		}
		return Json::binary(std::move(bytes));
	}
	if (value.is_object() || value.is_array())
	{
		for (auto& child : value)
		{
			child = JsonBlobs(std::move(child), decode);
		}
	}
	return value;
}
} // namespace

ArchiveWriter::ArchiveWriter() : storage(std::make_shared<ArchiveStorage>())
{
}

ArchiveWriter::ArchiveWriter(std::shared_ptr<ArchiveStorage> storage, String pointer, SerializationContext* context)
    : storage(std::move(storage)), pointer(std::move(pointer)), context(context)
{
}

ArchiveReader::ArchiveReader(std::shared_ptr<ArchiveStorage> storage, String pointer)
    : storage(std::move(storage)), pointer(std::move(pointer))
{
}

Result<void> ArchiveWriter::Write(const String& field, bool value)
{
	return Put(*this, field, value);
}

Result<void> ArchiveWriter::Null(const String& field)
{
	return Put(*this, field, nullptr);
}

Result<void> ArchiveWriter::Write(const String& field, Int64 value)
{
	return Put(*this, field, value);
}

Result<void> ArchiveWriter::Write(const String& field, UInt64 value)
{
	return Put(*this, field, value);
}

Result<void> ArchiveWriter::Write(const String& field, Float64 value)
{
	return std::isfinite(value) ? Put(*this, field, value) : Result<void>::Failure("Archive number is not finite");
}

Result<void> ArchiveWriter::Write(const String& field, const String& value)
{
	return Put(*this, field, ToStdString(value));
}

Result<void> ArchiveWriter::WriteBytes(const String& field, const Vector<UInt8>& value)
{
	return Put(*this, field, Json::binary(std::vector<UInt8>(value.begin(), value.end())));
}

Result<ArchiveWriter> ArchiveWriter::Object(const String& field)
{
	auto result = Put(*this, field, Json::object());
	return result ? Result<ArchiveWriter>::Success(ArchiveWriter(storage, ChildPointer(pointer, field), context))
	              : Result<ArchiveWriter>::Failure(result.error);
}

Result<ArchiveWriter> ArchiveWriter::Array(const String& field)
{
	auto result = Put(*this, field, Json::array());
	return result ? Result<ArchiveWriter>::Success(ArchiveWriter(storage, ChildPointer(pointer, field), context))
	              : Result<ArchiveWriter>::Failure(result.error);
}

Result<ArchiveWriter> ArchiveWriter::AddObject()
{
	auto& array = ArchiveAccess::Node(*this);
	if (!array.is_array())
	{
		return Result<ArchiveWriter>::Failure("Archive AddObject requires an array");
	}
	const Size index = array.size();
	array.push_back(Json::object());
	return Result<ArchiveWriter>::Success(
	    ArchiveWriter(storage, pointer + "/" + ToUrsaString(std::to_string(index)), context));
}

Result<void> ArchiveWriter::Append(Float64 value)
{
	auto& array = ArchiveAccess::Node(*this);
	if (!array.is_array() || !std::isfinite(value))
	{
		return Result<void>::Failure("Archive Append requires an array and finite value");
	}
	array.push_back(value);
	return {};
}

Result<void> ArchiveWriter::Append(const String& value)
{
	auto& array = ArchiveAccess::Node(*this);
	if (!array.is_array())
	{
		return Result<void>::Failure("Archive Append requires an array");
	}
	array.push_back(ToStdString(value));
	return {};
}

Result<bool> ArchiveReader::Boolean(const String& field) const
{
	return ReadField<bool>(*this, field,
	                       [](const Json& value)
	                       {
		                       return value.is_boolean() ? Result<bool>::Success(value.get<bool>())
		                                                 : Result<bool>::Failure("Expected archive boolean");
	                       });
}

Result<Int64> ArchiveReader::Integer(const String& field) const
{
	return ReadField<Int64>(*this, field,
	                        [](const Json& value)
	                        {
		                        if (!value.is_number_integer() ||
		                            (value.is_number_unsigned() &&
		                             value.get<UInt64>() > static_cast<UInt64>((std::numeric_limits<Int64>::max)())))
		                        {
			                        return Result<Int64>::Failure("Expected a bounded archive integer");
		                        }
		                        return Result<Int64>::Success(value.get<Int64>());
	                        });
}

Result<UInt64> ArchiveReader::Unsigned(const String& field) const
{
	return ReadField<UInt64>(*this, field,
	                         [](const Json& value)
	                         {
		                         if (!value.is_number_integer() ||
		                             (!value.is_number_unsigned() && value.get<Int64>() < 0))
		                         {
			                         return Result<UInt64>::Failure("Expected an unsigned archive integer");
		                         }
		                         return Result<UInt64>::Success(value.get<UInt64>());
	                         });
}

Result<Float64> ArchiveReader::Number(const String& field) const
{
	return ReadField<Float64>(*this, field, ReadNumber);
}

Result<String> ArchiveReader::Text(const String& field) const
{
	return ReadField<String>(*this, field, ReadText);
}

Result<Vector<UInt8>> ArchiveReader::Bytes(const String& field) const
{
	return ReadField<Vector<UInt8>>(*this, field,
	                                [](const Json& value)
	                                {
		                                if (!value.is_binary())
		                                {
			                                return Result<Vector<UInt8>>::Failure("Expected archive binary block");
		                                }
		                                const auto& bytes = value.get_binary();
		                                return Result<Vector<UInt8>>::Success(
		                                    Vector<UInt8>(bytes.data(), bytes.data() + bytes.size()));
	                                });
}

Result<ArchiveReader> ArchiveReader::Object(const String& field) const
{
	return ReadField<ArchiveReader>(*this, field,
	                                [&](const Json& value)
	                                {
		                                return value.is_object()
		                                           ? Result<ArchiveReader>::Success(
		                                                 ArchiveReader(storage, ChildPointer(pointer, field)))
		                                           : Result<ArchiveReader>::Failure("Expected archive object");
	                                });
}

Result<Vector<ArchiveReader>> ArchiveReader::Array(const String& field) const
{
	return ReadField<Vector<ArchiveReader>>(
	    *this, field,
	    [&](const Json& value)
	    {
		    if (!value.is_array())
		    {
			    return Result<Vector<ArchiveReader>>::Failure("Expected archive array");
		    }
		    Vector<ArchiveReader> values;
		    for (Size index = 0; index < value.size(); ++index)
		    {
			    values.push_back(
			        ArchiveReader(storage, ChildPointer(pointer, field) + "/" + ToUrsaString(std::to_string(index))));
		    }
		    return Result<Vector<ArchiveReader>>::Success(std::move(values));
	    });
}

Result<Float64> ArchiveReader::NumberValue() const
{
	try
	{
		return ReadNumber(ArchiveAccess::Node(*this));
	}
	catch (const std::exception& error)
	{
		return Result<Float64>::Failure(error.what());
	}
}

Result<String> ArchiveReader::TextValue() const
{
	try
	{
		return ReadText(ArchiveAccess::Node(*this));
	}
	catch (const std::exception& error)
	{
		return Result<String>::Failure(error.what());
	}
}

bool ArchiveReader::Contains(const String& field) const
{
	return storage && ArchiveAccess::Node(*this).is_object() && ArchiveAccess::Node(*this).contains(ToStdString(field));
}

bool ArchiveReader::IsNull(const String& field) const
{
	return Contains(field) && ArchiveAccess::Node(*this).at(ToStdString(field)).is_null();
}

Result<nlohmann::json> ArchiveToJson(const ArchiveWriter& writer)
{
	try
	{
		return Result<Json>::Success(JsonBlobs(ArchiveAccess::Node(writer), false));
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure(error.what());
	}
}

Result<Vector<String>> ArchiveReader::Fields() const
{
	try
	{
		const auto& object = ArchiveAccess::Node(*this);
		if (!object.is_object())
		{
			return Result<Vector<String>>::Failure("Archive Fields requires an object");
		}
		Vector<String> fields;
		for (auto field = object.begin(); field != object.end(); ++field)
		{
			fields.push_back(ToUrsaString(field.key()));
		}
		return Result<Vector<String>>::Success(std::move(fields));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<String>>::Failure(error.what());
	}
}

Result<ArchiveReader> ArchiveFromJson(const nlohmann::json& document)
{
	try
	{
		return Result<ArchiveReader>::Success(ArchiveAccess::Reader(JsonBlobs(document, true)));
	}
	catch (const std::exception& error)
	{
		return Result<ArchiveReader>::Failure(error.what());
	}
}

Result<nlohmann::json> ArchiveToJson(const ArchiveReader& reader)
{
	try
	{
		return Result<Json>::Success(JsonBlobs(ArchiveAccess::Node(reader), false));
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure(error.what());
	}
}

Result<ArchiveReader> ArchiveSnapshot(const ArchiveWriter& writer)
{
	try
	{
		return Result<ArchiveReader>::Success(ArchiveAccess::Reader(ArchiveAccess::Node(writer)));
	}
	catch (const std::exception& error)
	{
		return Result<ArchiveReader>::Failure(error.what());
	}
}

Result<void> ArchiveWriteJson(ArchiveWriter& writer, const nlohmann::json& document)
{
	try
	{
		ArchiveAccess::Node(writer) = JsonBlobs(document, true);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}

Result<Vector<UInt8>> JsonArchive::Encode(const ArchiveWriter& writer)
{
	auto document = ArchiveToJson(writer);
	if (!document)
	{
		return Result<Vector<UInt8>>::Failure(document.error);
	}
	try
	{
		const std::string text = document.value.dump(2);
		return Result<Vector<UInt8>>::Success(Vector<UInt8>(reinterpret_cast<const UInt8*>(text.data()),
		                                                    reinterpret_cast<const UInt8*>(text.data()) + text.size()));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<UInt8>>::Failure(error.what());
	}
}

Result<ArchiveReader> JsonArchive::Decode(const Vector<UInt8>& bytes)
{
	try
	{
		if (bytes.size() > 256ull * 1024 * 1024)
		{
			return Result<ArchiveReader>::Failure("Archive exceeds size limit");
		}
		const auto document = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
		if (document.is_discarded() || !document.is_object())
		{
			return Result<ArchiveReader>::Failure("Invalid JSON archive document");
		}
		return ArchiveFromJson(document);
	}
	catch (const std::exception& error)
	{
		return Result<ArchiveReader>::Failure(error.what());
	}
}

Result<Vector<UInt8>> BinaryArchive::Encode(const ArchiveWriter& writer)
{
	try
	{
		const auto payload = Json::to_cbor(ArchiveAccess::Node(writer));
		Vector<UInt8> bytes{'U', 'R', 'S', 'A', 'A', 'R', 'C', 1};
		bytes.insert(bytes.end(), payload.data(), payload.data() + payload.size());
		return Result<Vector<UInt8>>::Success(std::move(bytes));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<UInt8>>::Failure(error.what());
	}
}

Result<ArchiveReader> BinaryArchive::Decode(const Vector<UInt8>& bytes)
{
	try
	{
		const UInt8 magic[8]{'U', 'R', 'S', 'A', 'A', 'R', 'C', 1};
		if (bytes.size() < 9 || bytes.size() > 256ull * 1024 * 1024 ||
		    !std::equal(std::begin(magic), std::end(magic), bytes.begin()))
		{
			return Result<ArchiveReader>::Failure("Invalid binary archive header or version");
		}
		const auto document = Json::from_cbor(bytes.begin() + 8, bytes.end(), true, false);
		if (document.is_discarded() || !document.is_object())
		{
			return Result<ArchiveReader>::Failure("Invalid binary archive document");
		}
		return Result<ArchiveReader>::Success(ArchiveAccess::Reader(document));
	}
	catch (const std::exception& error)
	{
		return Result<ArchiveReader>::Failure(error.what());
	}
}
} // namespace Ursa
