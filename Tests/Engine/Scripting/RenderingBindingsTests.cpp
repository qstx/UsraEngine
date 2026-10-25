#include <Runtime.generated.hpp>
#include <Rendering.generated.hpp>
#include <Rendering.Commands.generated.hpp>
#include <ComputeFixtures.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;
namespace B = Bindings;
using Handle = B::Wire::Value_Ursa_NativeHandle;
using Token = B::Wire::Value_Ursa_Rendering_RenderContextToken;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <class T, class F, class... A> B::CallbackResult<T> Call(F function, void* context, A... arguments)
{
	B::CallbackResult<T> result;
	result.status = function(context, arguments..., &result.value, &result.outcome);
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.Error()));
	}
	return result;
}

template <class F, class... A> void CallVoid(F function, void* context, A... arguments)
{
	B::CallbackResult<void> result;
	result.status = function(context, arguments..., &result.outcome);
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.Error()));
	}
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		B::CompleteBindingResult(
		    files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const ResourceId shaderId = ResourceId::Generate();
		const auto path = B::TakeBindingResult(VirtualPath::Parse("prj:Fill.ursashaderasset"));
		const auto shader = B::TakeBindingResult(
		    Test::CreateComputeFixture({URSA_TEST_DXC_EXECUTABLE, URSA_TEST_SPIRV_VAL_EXECUTABLE,
		                                URSA_TEST_SPIRV_CROSS_EXECUTABLE, URSA_SHADER_STAGING_ROOT},
		                               shaderId));
		B::CompleteBindingResult(Shader::SaveShaderAssetResource(files, path, *shader));
		const auto resources = B::TakeBindingResult(ResourceSession::Open(
		    files,
		    [&](ResourceId guid, UInt64 localId)
		    {
			    return guid == shaderId && localId == 0 ? Result<VirtualPath>::Success(path)
			                                            : Result<VirtualPath>::Failure("Missing compute fixture");
		    }));
		const auto device = B::TakeBindingResult(Rhi::CreateDevice({}));
		Scene scene;
		ManagedSceneApi api(scene, resources.get());
		api.SetRenderDevice(device);
		const auto& runtime = *static_cast<const B::Runtime::Table*>(B::Runtime::NativeDescriptor().functions);
		const auto& rendering = *static_cast<const B::Rendering::Table*>(B::Rendering::NativeDescriptor().functions);
		const String guid = shaderId.ToString();
		B::Wire::Value_Ursa_NativeAssetReference asset{{reinterpret_cast<const UInt8*>(guid.data()), guid.size(), 1, 0},
		                                               0};
		String kernelName("Fill");
		B::Utf8View name{reinterpret_cast<const UInt8*>(kernelName.data()), kernelName.size(), 1, 0};
		const auto kernel = Call<Int32>(runtime.NativeResourceBindings_FindComputeKernel, api.Token(), &asset, &name);
		const auto group = Call<B::Wire::Value_Ursa_ComputeThreadGroupSize>(
		    runtime.NativeResourceBindings_GetComputeThreadGroupSize, api.Token(), &asset, kernel.value);
		Expect(kernel.value == 0 && group.value.F_X == 8 && group.value.F_Y == 8 && group.value.F_Z == 1,
		       "Generated ComputeShader metadata query lost the imported layout");
		B::Wire::Value_Ursa_Rendering_RenderTextureValue descriptor{8, 8, 0, 0, 1, 1};
		const auto texture =
		    Call<Handle>(rendering.NativeRenderingBindings_CreateRenderTexture, api.Token(), &descriptor);
		const auto buffer =
		    Call<Handle>(rendering.NativeRenderingBindings_CreateGraphicsBuffer, api.Token(), 4u, 64u, 4u);
		const auto token = Call<Token>(rendering.NativeRenderingBindings_Begin, api.Token(), UInt64{1}, 1.0f);
		{
			B::CallbackResult<B::DataView> invalid;
			invalid.status = rendering.NativeRenderingBindings_ReadRenderTexture(api.Token(), &texture.value,
			                                                                     &invalid.value, &invalid.outcome);
			Expect(!invalid, "Generated readback accepted an unfinished render frame");
		}
		namespace C = B::Rendering::Commands;
		B::BindingCommandWriter writer(C::hash);
		const auto target = [](const Handle& value)
		{
			B::Wire::Value_Ursa_Rendering_RenderTargetValue result;
			result.F_Kind = 1;
			result.F_Texture.present = 1;
			result.F_Texture.F_value = value;
			return result;
		};
		const auto text = [](const String& value)
		{ return B::Utf8View{reinterpret_cast<const UInt8*>(value.data()), value.size(), 1, 0}; };
		const String batchLabel("test render batch");
		const auto batchName = text(batchLabel);
		C::Record_RenderCommands_SetComputeTexture(writer,
		                                           {asset, kernel.value, text("_Output"), target(texture.value)});
		C::Record_RenderCommands_SetComputeBuffer(writer, {asset, kernel.value, text("_Data"), buffer.value});
		C::Record_RenderCommands_SetComputeVector(writer,
		                                          {asset, kernel.value, text("_Color"), {0.5f, 0.25f, 0.75f, 1}});
		C::Record_RenderCommands_SetComputeInt(writer, {asset, kernel.value, text("_Bias"), 31});
		C::Record_RenderCommands_DispatchCompute(writer, {asset, kernel.value, 1, 1, 1});
		const auto& bytes = writer.Bytes();
		B::DataView batch{bytes.data(), bytes.size(), 1, 0};
		CallVoid(rendering.NativeRenderingBindings_EnqueueBatch, api.Token(), &token.value, &batch, &batchName);
		writer.Clear();
		CallVoid(rendering.NativeRenderingBindings_Submit, api.Token(), &token.value);
		CallVoid(rendering.NativeRenderingBindings_Submit, api.Token(), &token.value);
		CallVoid(rendering.NativeRenderingBindings_End, api.Token(), &token.value);
		const auto pixels =
		    Call<B::DataView>(rendering.NativeRenderingBindings_ReadRenderTexture, api.Token(), &texture.value);
		const auto* rgba = static_cast<const UInt8*>(pixels.value.data);
		Expect(pixels.value.length == 8 * 8 * 4 && rgba[0] >= 126 && rgba[0] <= 129 && rgba[1] >= 62 && rgba[1] <= 65 &&
		           rgba[2] >= 190 && rgba[2] <= 193,
		       "Generated GPU readback lost compute texture output");
		const auto data = Call<B::DataView>(rendering.NativeRenderingBindings_ReadGraphicsBuffer, api.Token(),
		                                    &buffer.value, UInt64{0}, UInt64{256});
		Expect(data.value.length == 256, "Generated GPU buffer readback size differs");
		for (UInt32 index = 0; index < 64; ++index)
		{
			UInt32 word;
			std::memcpy(&word, static_cast<const UInt8*>(data.value.data) + index * 4, 4);
			Expect(word == index + 31, "Generated GPU buffer readback lost kernel parameters");
		}
		const auto failedFrame = Call<Token>(rendering.NativeRenderingBindings_Begin, api.Token(), UInt64{2}, 1.0f);
		writer.Clear();
		C::Record_RenderCommands_SetRenderTarget(writer, {target(texture.value)});
		C::Record_RenderCommands_ClearTarget(writer, {1, {1, 0, 0, 1}, 1, 0});
		auto staleBuffer = buffer.value;
		++staleBuffer.F_generation;
		C::Record_RenderCommands_SetGlobalBuffer(writer, {text("_Data"), staleBuffer});
		const auto& invalidBytes = writer.Bytes();
		B::DataView invalidBatch{invalidBytes.data(), invalidBytes.size(), 1, 0};
		const UInt64 accepted = api.AcceptedRenderBatchCount();
		B::CallbackResult<void> rejected;
		rejected.status = rendering.NativeRenderingBindings_EnqueueBatch(api.Token(), &failedFrame.value, &invalidBatch,
		                                                                 &batchName, &rejected.outcome);
		Expect(!rejected && api.AcceptedRenderBatchCount() == accepted,
		       "A late stale resource allowed a partial batch");
		auto wrongContract = invalidBytes;
		wrongContract[16] ^= 1;
		invalidBatch = {wrongContract.data(), wrongContract.size(), 1, 0};
		B::CallbackResult<void> incompatible;
		incompatible.status = rendering.NativeRenderingBindings_EnqueueBatch(
		    api.Token(), &failedFrame.value, &invalidBatch, &batchName, &incompatible.outcome);
		Expect(!incompatible && api.AcceptedRenderBatchCount() == accepted,
		       "An incompatible command contract was accepted");
		CallVoid(rendering.NativeRenderingBindings_Submit, api.Token(), &failedFrame.value);
		CallVoid(rendering.NativeRenderingBindings_End, api.Token(), &failedFrame.value);
		const auto unchanged =
		    Call<B::DataView>(rendering.NativeRenderingBindings_ReadRenderTexture, api.Token(), &texture.value);
		Expect(unchanged.value.length == pixels.value.length &&
		           std::memcmp(unchanged.value.data, pixels.value.data, Size(pixels.value.length)) == 0,
		       "A rejected command batch partially cleared its GPU target");
		writer.Clear();
		CallVoid(rendering.NativeRenderingBindings_ReleaseRenderTexture, api.Token(), &texture.value);
		CallVoid(rendering.NativeRenderingBindings_ReleaseGraphicsBuffer, api.Token(), &buffer.value);
		const auto valid = Call<UInt32>(runtime.NativeResourceBindings_IsValid, api.Token(), &texture.value);
		Expect(valid.value == 0 && rgba[0] >= 126, "Releasing the GPU resource invalidated its owned readback output");
		{
			B::CallbackResult<B::DataView> invalid;
			invalid.status = rendering.NativeRenderingBindings_ReadGraphicsBuffer(api.Token(), &buffer.value, 0, 256,
			                                                                      &invalid.value, &invalid.outcome);
			Expect(!invalid, "Released GraphicsBuffer remained readable");
		}
		descriptor = {4, 4, 1, 0, 0, 1};
		const auto hdr = Call<Handle>(rendering.NativeRenderingBindings_CreateRenderTexture, api.Token(), &descriptor);
		const auto secondColor =
		    Call<Handle>(rendering.NativeRenderingBindings_CreateRenderTexture, api.Token(), &descriptor);
		descriptor.F_ColorFormat = 2;
		const auto depth =
		    Call<Handle>(rendering.NativeRenderingBindings_CreateRenderTexture, api.Token(), &descriptor);
		const auto mrtToken = Call<Token>(rendering.NativeRenderingBindings_Begin, api.Token(), UInt64{3}, 1.0f);
		writer.Clear();
		const Array<B::Wire::Value_Ursa_Rendering_RenderTargetValue, 2> targets{target(hdr.value),
		                                                                        target(secondColor.value)};
		C::RenderCommands_SetRenderTargets attachments;
		attachments.F_colors = {targets.data(), targets.size(), 1, 0};
		attachments.F_depth.present = 1;
		attachments.F_depth.F_value = target(depth.value);
		C::Record_RenderCommands_SetRenderTargets(writer, attachments);
		C::Record_RenderCommands_ClearTarget(writer, {3, {0.5f, 0.25f, 0.75f, 1}, 0.125f, 0});
		const auto& mrtBytes = writer.Bytes();
		batch = {mrtBytes.data(), mrtBytes.size(), 1, 0};
		CallVoid(rendering.NativeRenderingBindings_EnqueueBatch, api.Token(), &mrtToken.value, &batch, &batchName);
		writer.Clear();
		CallVoid(rendering.NativeRenderingBindings_ReleaseRenderTexture, api.Token(), &secondColor.value);
		CallVoid(rendering.NativeRenderingBindings_Submit, api.Token(), &mrtToken.value);
		CallVoid(rendering.NativeRenderingBindings_End, api.Token(), &mrtToken.value);
		const auto hdrPixels =
		    Call<B::DataView>(rendering.NativeRenderingBindings_ReadRenderTexture, api.Token(), &hdr.value);
		Array<UInt16, 4> halfPixel;
		std::memcpy(halfPixel.data(), hdrPixels.value.data, sizeof(halfPixel));
		Expect(hdrPixels.value.length == 128 && halfPixel == Array<UInt16, 4>{0x3800, 0x3400, 0x3a00, 0x3c00},
		       "Generated HDR/MRT bindings lost float16 attachments or retained backing");
		const auto depthPixels =
		    Call<B::DataView>(rendering.NativeRenderingBindings_ReadRenderTexture, api.Token(), &depth.value);
		Float32 depthValue;
		std::memcpy(&depthValue, depthPixels.value.data, sizeof(depthValue));
		Expect(depthPixels.value.length == 64 && depthValue == 0.125f,
		       "Generated MRT binding lost its independent depth target");
		CallVoid(rendering.NativeRenderingBindings_ReleaseRenderTexture, api.Token(), &depth.value);
		CallVoid(rendering.NativeRenderingBindings_ReleaseRenderTexture, api.Token(), &hdr.value);
		descriptor.F_Width = device->Capabilities().maxTextureSize + 1;
		B::CallbackResult<Handle> excessive;
		excessive.status = rendering.NativeRenderingBindings_CreateRenderTexture(api.Token(), &descriptor,
		                                                                         &excessive.value, &excessive.outcome);
		Expect(!excessive, "Generated RenderTexture creation exceeded the device capacity");
		std::cout << "Generated Rendering bindings and real Compute GPU readback passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
