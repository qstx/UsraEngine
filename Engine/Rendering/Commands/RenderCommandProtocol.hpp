#pragma once

#include <Core/Types.hpp>

namespace Ursa::Rendering
{
// Internal executor operations. Wire opcodes and payloads come from RenderCommands.bindings.cs.
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

struct RenderContextToken
{
	UInt64 sessionId = 0;
	UInt64 frameId = 0;
	UInt32 index = 0;
	UInt32 generation = 0;
};

static_assert(sizeof(RenderContextToken) == 24, "Render context ABI changed");
static_assert(offsetof(RenderContextToken, frameId) == 8 && offsetof(RenderContextToken, index) == 16 &&
                  offsetof(RenderContextToken, generation) == 20,
              "Render context field offsets changed");

} // namespace Ursa::Rendering
