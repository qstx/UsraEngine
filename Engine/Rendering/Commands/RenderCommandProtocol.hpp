#pragma once

#include <Core/Result.hpp>
#include <Core/Types.hpp>

namespace Ursa::Rendering
{
inline constexpr UInt32 renderCommandProtocolVersion = 2;
inline constexpr UInt32 maxRenderCommandBytes = 64u * 1024u * 1024u;

enum class RenderCommandOpcode : UInt32
{
	SetRenderTarget = 1,
	ClearTarget,
	SetViewport,
	DrawRendererList,
	DrawMesh,
	Blit,
	CopyBuffer,
	DispatchCompute,
	SetGlobalParameters,
	GetTemporaryTarget,
	ReleaseTemporaryTarget,
	SetScissor,
	CopyTexture,
	DrawFullscreen,
	SetRenderTargets,
	SetComputeParameters,
	ResetCameraState
};

struct RenderCommandBatchHeader
{
	UInt32 version = renderCommandProtocolVersion;
	UInt32 headerSize = 16;
	UInt32 commandCount = 0;
	UInt32 byteSize = 16;
};

struct RenderCommandRecordHeader
{
	RenderCommandOpcode opcode = RenderCommandOpcode::SetRenderTarget;
	UInt32 byteSize = 8;
};

struct RenderContextToken
{
	UInt64 sessionId = 0;
	UInt64 frameId = 0;
	UInt32 index = 0;
	UInt32 generation = 0;
};

static_assert(sizeof(RenderCommandBatchHeader) == 16, "Render batch ABI changed");
static_assert(sizeof(RenderCommandRecordHeader) == 8, "Render record ABI changed");
static_assert(sizeof(RenderContextToken) == 24, "Render context ABI changed");
static_assert(offsetof(RenderContextToken, frameId) == 8 && offsetof(RenderContextToken, index) == 16 &&
                  offsetof(RenderContextToken, generation) == 20,
              "Render context field offsets changed");

Result<void> ValidateRenderCommandBatch(const void* bytes, Size byteCount);
} // namespace Ursa::Rendering
