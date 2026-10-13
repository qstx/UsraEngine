#include <Rendering/Commands/RenderCommandProtocol.hpp>
#include <cstring>

namespace Ursa::Rendering
{
namespace
{
constexpr UInt32 AlignRecordBytes(UInt32 payloadBytes)
{
	return (sizeof(RenderCommandRecordHeader) + payloadBytes + 7u) & ~7u;
}

bool ValidRecordPayload(const UInt8* data, Size offset, const RenderCommandRecordHeader& record)
{
	const UInt32 payloadBytes = record.byteSize - sizeof(record);
	UInt32 expectedBytes = 0;
	switch (record.opcode)
	{
		case RenderCommandOpcode::SetRenderTarget:
			return record.byteSize == AlignRecordBytes(4) || record.byteSize == AlignRecordBytes(24);
		case RenderCommandOpcode::ClearTarget:
			expectedBytes = AlignRecordBytes(28);
			break;
		case RenderCommandOpcode::SetViewport:
			expectedBytes = AlignRecordBytes(24);
			break;
		case RenderCommandOpcode::DrawRendererList:
			expectedBytes = AlignRecordBytes(32);
			break;
		case RenderCommandOpcode::DrawMesh:
			expectedBytes = AlignRecordBytes(116);
			break;
		case RenderCommandOpcode::Blit:
			return record.byteSize == AlignRecordBytes(48) || record.byteSize == AlignRecordBytes(76);
		case RenderCommandOpcode::CopyBuffer:
			expectedBytes = AlignRecordBytes(72);
			break;
		case RenderCommandOpcode::SetGlobalParameters:
		{
			if (payloadBytes < 8)
			{
				return false;
			}
			UInt32 valueKind = 0, nameBytes = 0;
			std::memcpy(&valueKind, data + offset + sizeof(record), sizeof(valueKind));
			std::memcpy(&nameBytes, data + offset + sizeof(record) + sizeof(valueKind), sizeof(nameBytes));
			const UInt32 valueBytes = valueKind == 1 ? 4u : valueKind == 2 ? 16u : valueKind == 3 ? 24u : 0u;
			if (!valueBytes || !nameBytes || nameBytes > 1024 || nameBytes > payloadBytes - 8 ||
			    valueBytes > payloadBytes - 8 - nameBytes)
			{
				return false;
			}
			expectedBytes = AlignRecordBytes(8 + nameBytes + valueBytes);
			break;
		}
		case RenderCommandOpcode::GetTemporaryTarget:
			expectedBytes = AlignRecordBytes(28);
			break;
		case RenderCommandOpcode::ReleaseTemporaryTarget:
			expectedBytes = AlignRecordBytes(4);
			break;
		case RenderCommandOpcode::SetScissor:
			expectedBytes = AlignRecordBytes(16);
			break;
		case RenderCommandOpcode::CopyTexture:
			expectedBytes = AlignRecordBytes(48);
			break;
		case RenderCommandOpcode::DispatchCompute:
			return payloadBytes >= 8;
		default:
			return false;
	}
	return record.byteSize == expectedBytes;
}
} // namespace

Result<void> ValidateRenderCommandBatch(const void* bytes, Size byteCount)
{
	if (!bytes || byteCount < sizeof(RenderCommandBatchHeader) || byteCount > maxRenderCommandBytes)
	{
		return Result<void>::Failure("Render command batch has an invalid byte range");
	}
	RenderCommandBatchHeader batch;
	std::memcpy(&batch, bytes, sizeof(batch));
	if (batch.version != renderCommandProtocolVersion || batch.headerSize != sizeof(batch) ||
	    batch.byteSize != byteCount || batch.commandCount > (byteCount - sizeof(batch)) / 8)
	{
		return Result<void>::Failure("Render command batch version, size or count is invalid");
	}
	const auto* data = static_cast<const UInt8*>(bytes);
	Size offset = sizeof(batch);
	for (UInt32 index = 0; index < batch.commandCount; ++index)
	{
		if (byteCount - offset < sizeof(RenderCommandRecordHeader))
		{
			return Result<void>::Failure("Render command record is truncated");
		}
		RenderCommandRecordHeader record;
		std::memcpy(&record, data + offset, sizeof(record));
		const UInt32 opcode = static_cast<UInt32>(record.opcode);
		if (opcode < static_cast<UInt32>(RenderCommandOpcode::SetRenderTarget) ||
		    opcode > static_cast<UInt32>(RenderCommandOpcode::CopyTexture) || record.byteSize < sizeof(record) ||
		    record.byteSize % 8 != 0 || record.byteSize > byteCount - offset)
		{
			return Result<void>::Failure("Render command opcode or record size is invalid");
		}
		if (!ValidRecordPayload(data, offset, record))
		{
			return Result<void>::Failure("Render command payload size or value kind is invalid");
		}
		offset += record.byteSize;
	}
	return offset == byteCount ? Result<void>{}
	                           : Result<void>::Failure("Render command batch contains unclaimed trailing bytes");
}
} // namespace Ursa::Rendering
