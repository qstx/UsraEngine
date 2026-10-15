#include <Rendering/Commands/RenderCommandProtocol.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace Ursa;
using namespace Ursa::Rendering;

namespace
{
void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		std::array<UInt8, 72> bytes{};
		auto batch = [&](UInt32 version, UInt32 count, UInt32 size)
		{
			RenderCommandBatchHeader header{version, 16, count, size};
			std::memcpy(bytes.data(), &header, sizeof(header));
		};
		auto record = [&](UInt32 opcode, UInt32 size)
		{
			RenderCommandRecordHeader header{static_cast<RenderCommandOpcode>(opcode), size};
			std::memcpy(bytes.data() + 16, &header, sizeof(header));
		};
		batch(renderCommandProtocolVersion, 0, 16);
		Expect(bool(ValidateRenderCommandBatch(bytes.data(), 16)), "Empty batch rejected");
		batch(renderCommandProtocolVersion, 1, 32);
		record(1, 16);
		Expect(bool(ValidateRenderCommandBatch(bytes.data(), 32)), "Valid batch rejected");
		batch(renderCommandProtocolVersion, 1, 72);
		record(13, 56);
		const auto copyResult = ValidateRenderCommandBatch(bytes.data(), 72);
		Expect(bool(copyResult), copyResult.error.c_str());
		Expect(!ValidateRenderCommandBatch(nullptr, 32) && !ValidateRenderCommandBatch(bytes.data(), 15),
		       "Invalid range accepted");
		batch(renderCommandProtocolVersion + 1, 1, 32);
		Expect(!ValidateRenderCommandBatch(bytes.data(), 32), "Unknown version accepted");
		batch(renderCommandProtocolVersion, 2, 32);
		Expect(!ValidateRenderCommandBatch(bytes.data(), 32), "Truncated command accepted");
		batch(renderCommandProtocolVersion, 1, 32);
		for (UInt32 size : {0u, 7u, 9u, 24u, 0xfffffff8u})
		{
			record(1, size);
			Expect(!ValidateRenderCommandBatch(bytes.data(), 32), "Invalid record size accepted");
		}
		for (UInt32 opcode : {0u, 17u, 0xffffffffu})
		{
			record(opcode, 16);
			Expect(!ValidateRenderCommandBatch(bytes.data(), 32), "Unknown opcode accepted");
		}
		record(1, 8);
		Expect(!ValidateRenderCommandBatch(bytes.data(), 32), "Trailing bytes accepted");
		std::cout << "Render command framing tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
