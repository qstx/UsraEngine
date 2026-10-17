#include <Rendering.Commands.generated.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
namespace B = Ursa::Scripting::Bindings;
namespace C = B::Rendering::Commands;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

bool Valid(const void* bytes, Size size)
{
	try
	{
		B::BindingArena arena;
		C::Decode(bytes, size, arena);
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

void WriteUInt(Vector<UInt8>& bytes, Size offset, UInt32 value)
{
	std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
} // namespace

int main()
{
	try
	{
		B::BindingCommandWriter writer(C::hash);
		const auto empty = writer.Bytes();
		Expect(Valid(empty.data(), empty.size()), "Empty generated batch rejected");
		C::RenderCommands_SetRenderTarget target;
		target.F_target.F_Id = 5;
		C::Record_RenderCommands_SetRenderTarget(writer, target);
		const auto valid = writer.Bytes();
		Expect(Valid(valid.data(), valid.size()), "Generated target command rejected");
		for (Size size : {Size{0}, Size{47}, Size{48}, valid.size() - 1})
		{
			Expect(!Valid(valid.data(), size), "Truncated generated batch accepted");
		}
		Expect(!Valid(nullptr, valid.size()), "Null command range accepted");
		for (Size offset : {Size{0}, Size{8}, Size{12}, Size{16}})
		{
			auto bytes = valid;
			bytes[offset] ^= 0xff;
			Expect(!Valid(bytes.data(), bytes.size()), "Invalid version/count/size/contract identity accepted");
		}
		for (UInt32 size : {0u, 7u, 9u, 8u, 0xfffffff8u})
		{
			auto bytes = valid;
			WriteUInt(bytes, B::bindingCommandHeaderSize + 4, size);
			Expect(!Valid(bytes.data(), bytes.size()), "Invalid command record size accepted");
		}
		for (UInt32 opcode : {0u, 0xffffffffu})
		{
			auto bytes = valid;
			WriteUInt(bytes, B::bindingCommandHeaderSize, opcode);
			Expect(!Valid(bytes.data(), bytes.size()), "Unknown command identity accepted");
		}
		{
			auto bytes = valid;
			bytes.back() = 1;
			Expect(!Valid(bytes.data(), bytes.size()), "Nonzero record padding accepted");
		}
		writer.Clear();
		C::RenderCommands_CopyTexture copy;
		copy.F_source.F_Id = 1;
		copy.F_destination.F_Id = 2;
		C::Record_RenderCommands_CopyTexture(writer, copy);
		const auto copied = writer.Bytes();
		Expect(Valid(copied.data(), copied.size()), "Generated copy command rejected");
		writer.Clear();
		C::Record_RenderCommands_ResetCameraState(writer, {});
		const auto reset = writer.Bytes();
		Expect(!Valid(reset.data(), reset.size()), "Native-only camera reset was accepted from a project batch");
		B::BindingArena arena;
		Expect(C::Decode(reset.data(), reset.size(), arena, true).size() == 1, "Native camera reset codec failed");
		std::cout << "Generated render command framing and native-only isolation passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
