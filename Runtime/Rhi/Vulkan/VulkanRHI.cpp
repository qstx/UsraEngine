#include <Core/Types.hpp>
#include <Core/Profiler.hpp>
#include <Rhi/RHI.hpp>
#include <Core/StringInterop.hpp>
#include <Rhi/Vulkan/ValidationLog.hpp>
#include <Vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>
#include <tuple>
#include <vk_mem_alloc.h>
#if defined(URSA_ENABLE_TRACY)
#include <tracy/TracyVulkan.hpp>
#endif

namespace Ursa::Rhi
{
namespace
{
struct SamplerImpl;
struct TextureImpl;
struct BufferImpl;

struct TrackedResourceStates
{
	std::weak_ptr<void> owner;
	Vector<ResourceState> states;
};

void Require(bool condition, const String& message)
{
	if (!condition)
	{
		throw std::runtime_error(ToStdString(message));
	}
}

void Check(VkResult result, const char* operation)
{
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
	}
}

template <class T, class F> Result<T> Guarded(F&& fn)
{
	try
	{
		return Result<T>::Success(fn());
	}
	catch (const std::exception& e)
	{
		return Result<T>::Failure(e.what());
	}
}

template <class F> Result<void> GuardedVoid(F&& fn)
{
	try
	{
		fn();
		return Result<void>::Success();
	}
	catch (const std::exception& e)
	{
		return Result<void>::Failure(e.what());
	}
}

VkFormat ToVkFormat(Format format)
{
	switch (format)
	{
		case Format::RgbA8Unorm:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case Format::RgbA8Srgb:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case Format::BgrA8Unorm:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case Format::R32Uint:
			return VK_FORMAT_R32_UINT;
		case Format::D32Float:
			return VK_FORMAT_D32_SFLOAT;
		case Format::D32FloatS8Uint:
			return VK_FORMAT_D32_SFLOAT_S8_UINT;
		case Format::RgbA16Float:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
	}
	throw std::runtime_error("Unknown texture format");
}

Format FromVkFormat(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_R8G8B8A8_UNORM:
			return Format::RgbA8Unorm;
		case VK_FORMAT_R8G8B8A8_SRGB:
			return Format::RgbA8Srgb;
		case VK_FORMAT_B8G8R8A8_UNORM:
			return Format::BgrA8Unorm;
		case VK_FORMAT_R32_UINT:
			return Format::R32Uint;
		case VK_FORMAT_D32_SFLOAT:
			return Format::D32Float;
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return Format::D32FloatS8Uint;
		case VK_FORMAT_R16G16B16A16_SFLOAT:
			return Format::RgbA16Float;
		default:
			throw std::runtime_error("Unsupported surface format");
	}
}

bool IsDepthFormat(Format format)
{
	return format == Format::D32Float || format == Format::D32FloatS8Uint;
}

VkImageAspectFlags ImageAspect(Format format)
{
	if (format == Format::D32FloatS8Uint)
	{
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	return format == Format::D32Float ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkCompareOp ToVkCompare(Compare compare)
{
	switch (compare)
	{
		case Compare::Never:
			return VK_COMPARE_OP_NEVER;
		case Compare::Less:
			return VK_COMPARE_OP_LESS;
		case Compare::LessEqual:
			return VK_COMPARE_OP_LESS_OR_EQUAL;
		case Compare::Equal:
			return VK_COMPARE_OP_EQUAL;
		case Compare::NotEqual:
			return VK_COMPARE_OP_NOT_EQUAL;
		case Compare::Greater:
			return VK_COMPARE_OP_GREATER;
		case Compare::GreaterEqual:
			return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case Compare::Always:
			return VK_COMPARE_OP_ALWAYS;
	}
	throw std::runtime_error("Unknown comparison operation");
}

VkBlendFactor ToVkBlendFactor(BlendFactor factor)
{
	switch (factor)
	{
		case BlendFactor::Zero:
			return VK_BLEND_FACTOR_ZERO;
		case BlendFactor::One:
			return VK_BLEND_FACTOR_ONE;
		case BlendFactor::SourceColor:
			return VK_BLEND_FACTOR_SRC_COLOR;
		case BlendFactor::OneMinusSourceColor:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case BlendFactor::DestinationColor:
			return VK_BLEND_FACTOR_DST_COLOR;
		case BlendFactor::OneMinusDestinationColor:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case BlendFactor::SourceAlpha:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		case BlendFactor::OneMinusSourceAlpha:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case BlendFactor::DestinationAlpha:
			return VK_BLEND_FACTOR_DST_ALPHA;
		case BlendFactor::OneMinusDestinationAlpha:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	}
	throw std::runtime_error("Unknown blend factor");
}

VkBlendOp ToVkBlendOperation(BlendOperation operation)
{
	switch (operation)
	{
		case BlendOperation::Add:
			return VK_BLEND_OP_ADD;
		case BlendOperation::Subtract:
			return VK_BLEND_OP_SUBTRACT;
		case BlendOperation::ReverseSubtract:
			return VK_BLEND_OP_REVERSE_SUBTRACT;
		case BlendOperation::Minimum:
			return VK_BLEND_OP_MIN;
		case BlendOperation::Maximum:
			return VK_BLEND_OP_MAX;
	}
	throw std::runtime_error("Unknown blend operation");
}

VkStencilOp ToVkStencilOperation(StencilOperation operation)
{
	switch (operation)
	{
		case StencilOperation::Keep:
			return VK_STENCIL_OP_KEEP;
		case StencilOperation::Zero:
			return VK_STENCIL_OP_ZERO;
		case StencilOperation::Replace:
			return VK_STENCIL_OP_REPLACE;
		case StencilOperation::IncrementClamp:
			return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case StencilOperation::DecrementClamp:
			return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case StencilOperation::Invert:
			return VK_STENCIL_OP_INVERT;
		case StencilOperation::IncrementWrap:
			return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case StencilOperation::DecrementWrap:
			return VK_STENCIL_OP_DECREMENT_AND_WRAP;
	}
	throw std::runtime_error("Unknown stencil operation");
}

VkColorComponentFlags ToVkColorWriteMask(ColorWriteMask mask)
{
	VkColorComponentFlags flags = 0;
	if (Has(mask, ColorWriteMask::Red))
	{
		flags |= VK_COLOR_COMPONENT_R_BIT;
	}
	if (Has(mask, ColorWriteMask::Green))
	{
		flags |= VK_COLOR_COMPONENT_G_BIT;
	}
	if (Has(mask, ColorWriteMask::Blue))
	{
		flags |= VK_COLOR_COMPONENT_B_BIT;
	}
	if (Has(mask, ColorWriteMask::Alpha))
	{
		flags |= VK_COLOR_COMPONENT_A_BIT;
	}
	return flags;
}

Ursa::UInt32 PixelSize(Format f)
{
	return f == Format::D32FloatS8Uint || f == Format::RgbA16Float ? 8 : 4;
}

VkBufferUsageFlags VkBufferUsage(BufferUsage usage)
{
	VkBufferUsageFlags flags = 0;
	if (Has(usage, BufferUsage::TransferSource))
	{
		flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}
	if (Has(usage, BufferUsage::TransferDestination))
	{
		flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	if (Has(usage, BufferUsage::Vertex))
	{
		flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if (Has(usage, BufferUsage::Index))
	{
		flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if (Has(usage, BufferUsage::Uniform))
	{
		flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	if (Has(usage, BufferUsage::Storage))
	{
		flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	return flags;
}

VkImageUsageFlags VkImageUsage(TextureUsage usage)
{
	VkImageUsageFlags flags = 0;
	if (Has(usage, TextureUsage::TransferSource))
	{
		flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (Has(usage, TextureUsage::TransferDestination))
	{
		flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if (Has(usage, TextureUsage::Sampled))
	{
		flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if (Has(usage, TextureUsage::Storage))
	{
		flags |= VK_IMAGE_USAGE_STORAGE_BIT;
	}
	if (Has(usage, TextureUsage::ColorAttachment))
	{
		flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if (Has(usage, TextureUsage::DepthAttachment))
	{
		flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	return flags;
}

VkShaderStageFlags VkStages(Ursa::UInt32 mask)
{
	VkShaderStageFlags flags = 0;
	if (mask & 1)
	{
		flags |= VK_SHADER_STAGE_VERTEX_BIT;
	}
	if (mask & 2)
	{
		flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	if (mask & 4)
	{
		flags |= VK_SHADER_STAGE_COMPUTE_BIT;
	}
	return flags;
}

VkShaderStageFlagBits VkStage(ShaderStage stage)
{
	switch (stage)
	{
		case ShaderStage::Vertex:
			return VK_SHADER_STAGE_VERTEX_BIT;
		case ShaderStage::Fragment:
			return VK_SHADER_STAGE_FRAGMENT_BIT;
		case ShaderStage::Compute:
			return VK_SHADER_STAGE_COMPUTE_BIT;
	}
	throw std::runtime_error("Unknown shader stage");
}

VkDescriptorType VkDescriptor(BindingType type)
{
	switch (type)
	{
		case BindingType::UniformBuffer:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case BindingType::StorageBuffer:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case BindingType::SampledTexture:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case BindingType::StorageTexture:
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case BindingType::Sampler:
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		case BindingType::CombinedImageSampler:
			return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		case BindingType::DynamicUniformBuffer:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		case BindingType::DynamicStorageBuffer:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	}
	throw std::runtime_error("Unknown descriptor type");
}

struct Access
{
	VkPipelineStageFlags stage;
	VkAccessFlags access;
	VkImageLayout layout;
};

Access Access(ResourceState state)
{
	switch (state)
	{
		case ResourceState::Undefined:
			return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::Present:
			return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
		case ResourceState::TransferSource:
			return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
		case ResourceState::TransferDestination:
			return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
		case ResourceState::ShaderRead:
			return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			        VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		case ResourceState::ShaderWrite:
			return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			        VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
		case ResourceState::ColorAttachment:
			return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		case ResourceState::DepthAttachment:
			return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		case ResourceState::VertexBuffer:
			return {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::IndexBuffer:
			return {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::UniformRead:
			return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			        VK_ACCESS_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::StorageReadWrite:
			return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
	}
	throw std::runtime_error("Unknown resource state");
}

#if defined(URSA_ENABLE_TRACY_MEMORY) || defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
struct DeviceMemoryProfiling
{
	std::array<char, 80> pool{};
	std::array<char, 80> reserved{};
	std::array<char, 80> used{};
	std::mutex sampling;
	std::chrono::steady_clock::time_point nextSample{};
};

DeviceMemoryProfiling* RegisterDeviceMemoryProfiling()
{
	// Tracy can query names after the owning device has closed.
	static std::atomic<UInt64> nextDevice{0};
	void* storage = std::malloc(sizeof(DeviceMemoryProfiling));
	Require(storage != nullptr, "Cannot allocate Vulkan profiling metadata");
	auto* profile = new (storage) DeviceMemoryProfiling;
	const auto identity = static_cast<unsigned long long>(nextDevice.fetch_add(1, std::memory_order_relaxed));
	std::snprintf(profile->pool.data(), profile->pool.size(), "Ursa.Gpu.DeviceMemory.%llu", identity);
	std::snprintf(profile->reserved.data(), profile->reserved.size(), "Ursa.Gpu.%llu.Reserved", identity);
	std::snprintf(profile->used.data(), profile->used.size(), "Ursa.Gpu.%llu.Used", identity);
	return profile;
}
#endif

#if defined(URSA_ENABLE_TRACY_MEMORY)
const void* DeviceMemoryIdentity(VkDeviceMemory memory) noexcept
{
#if VK_USE_64_BIT_PTR_DEFINES
	return reinterpret_cast<const void*>(memory);
#else
	return reinterpret_cast<const void*>(static_cast<UIntPtr>(memory));
#endif
}

void VKAPI_PTR DeviceMemoryAllocated(VmaAllocator, UInt32, VkDeviceMemory memory, VkDeviceSize size,
                                     void* user) noexcept
{
	const auto* profile = static_cast<DeviceMemoryProfiling*>(user);
	URSA_PROFILE_MEMORY_ALLOC(DeviceMemoryIdentity(memory), static_cast<Size>(size), profile->pool.data());
}

void VKAPI_PTR DeviceMemoryReleased(VmaAllocator, UInt32, VkDeviceMemory memory, VkDeviceSize, void* user) noexcept
{
	const auto* profile = static_cast<DeviceMemoryProfiling*>(user);
	URSA_PROFILE_MEMORY_FREE(DeviceMemoryIdentity(memory), profile->pool.data());
}
#endif

struct Context
{
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
	PFN_vkCmdBeginDebugUtilsLabelEXT beginDebugLabel = nullptr;
	PFN_vkCmdEndDebugUtilsLabelEXT endDebugLabel = nullptr;
	bool debugUtilsEnabled = false;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	Ursa::UInt32 family = UINT32_MAX;
	bool supportsTimestamps = false;
	VmaAllocator allocator = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties properties{};
	VkPhysicalDeviceFeatures features{};
	std::atomic<Ursa::UInt32> validationErrors{0};
	std::shared_ptr<Window> window;
	std::mutex queueMutex;
	std::map<const void*, TrackedResourceStates> textureStates;
	std::map<const void*, TrackedResourceStates> bufferStates;
	std::mutex samplerCacheMutex;
	std::map<std::tuple<bool, bool, Float32>, std::weak_ptr<SamplerImpl>> samplers;
	std::mutex renderPassCacheMutex;
	std::map<std::string, VkRenderPass> renderPasses;
#if defined(URSA_ENABLE_TRACY_MEMORY) || defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	DeviceMemoryProfiling* memoryProfiling = nullptr;
#endif
#if defined(URSA_ENABLE_TRACY_MEMORY)
	VmaDeviceMemoryCallbacks memoryCallbacks{};
#endif
#if defined(URSA_ENABLE_TRACY)
	tracy::VkCtx* tracyGpu = nullptr;
#endif

	void SampleMemory() noexcept
	{
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
		if (!memoryProfiling || !allocator || !Profiling::IsConnected())
		{
			return;
		}
		std::unique_lock lock(memoryProfiling->sampling, std::try_to_lock);
		const auto now = std::chrono::steady_clock::now();
		if (!lock.owns_lock() || now < memoryProfiling->nextSample)
		{
			return;
		}
		memoryProfiling->nextSample = now + std::chrono::milliseconds(250);
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
		vmaGetHeapBudgets(allocator, budgets);
		UInt64 reserved = 0;
		UInt64 used = 0;
		VkPhysicalDeviceMemoryProperties memory{};
		vkGetPhysicalDeviceMemoryProperties(physical, &memory);
		for (UInt32 index = 0; index < memory.memoryHeapCount; ++index)
		{
			reserved += budgets[index].statistics.blockBytes;
			used += budgets[index].statistics.allocationBytes;
		}
		URSA_PROFILE_PLOT(memoryProfiling->reserved.data(), static_cast<Float64>(reserved), Profiling::PlotUnit::Bytes);
		URSA_PROFILE_PLOT(memoryProfiling->used.data(), static_cast<Float64>(used), Profiling::PlotUnit::Bytes);
#endif
	}

	~Context()
	{
		if (device)
		{
			vkDeviceWaitIdle(device);
		}
#if defined(URSA_ENABLE_TRACY)
		if (tracyGpu)
		{
			TracyVkDestroy(tracyGpu);
			tracyGpu = nullptr;
		}
#endif
		if (descriptorPool)
		{
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}
		for (const auto& [key, renderPass] : renderPasses)
		{
			vkDestroyRenderPass(device, renderPass, nullptr);
		}
		renderPasses.clear();
		if (allocator)
		{
			vmaDestroyAllocator(allocator);
		}
		if (device)
		{
			vkDestroyDevice(device, nullptr);
		}
		if (surface)
		{
			vkDestroySurfaceKHR(instance, surface, nullptr);
		}
		if (debug)
		{
			auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
			if (destroy)
			{
				destroy(instance, debug, nullptr);
			}
		}
		if (instance)
		{
			vkDestroyInstance(instance, nullptr);
		}
	}
};

struct SurfaceState
{
	std::shared_ptr<Context> ctx;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	bool ownsSurface = false;

	~SurfaceState()
	{
		if (ownsSurface && surface && ctx && ctx->instance)
		{
			if (ctx->device)
			{
				vkDeviceWaitIdle(ctx->device);
			}
			vkDestroySurfaceKHR(ctx->instance, surface, nullptr);
		}
	}
};

VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                  VkDebugUtilsMessageTypeFlagsEXT,
                                                  const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT && user)
	{
		static_cast<Context*>(user)->validationErrors.fetch_add(1, std::memory_order_relaxed);
	}
	VulkanDetail::ReportValidationMessage(severity, data ? data->pMessage : nullptr);
	return VK_FALSE;
}

std::shared_ptr<Context> CreateContext(const DeviceDesc& desc)
{
	Require(desc.backend == Backend::Vulkan, "Requested RHI backend is not implemented");
	auto ctx = std::make_shared<Context>();
	ctx->window = desc.window;
	Vector<const char*> extensions;
	if (ctx->window)
	{
		Require(glfwVulkanSupported() == GLFW_TRUE, "GLFW reports no Vulkan loader or driver");
		Ursa::UInt32 count = 0;
		const char** required = glfwGetRequiredInstanceExtensions(&count);
		Require(required && count, "GLFW cannot provide Vulkan surface extensions");
		extensions.assign(required, required + count);
	}
	Vector<const char*> layers;
	Ursa::UInt32 debugUtilsExtensionCount = 0;
	Check(vkEnumerateInstanceExtensionProperties(nullptr, &debugUtilsExtensionCount, nullptr),
	      "Enumerating Vulkan instance extensions");
	Vector<VkExtensionProperties> availableExtensions(debugUtilsExtensionCount);
	Check(vkEnumerateInstanceExtensionProperties(nullptr, &debugUtilsExtensionCount, availableExtensions.data()),
	      "Enumerating Vulkan instance extensions");
	const bool debugUtilsAvailable =
	    std::any_of(availableExtensions.begin(), availableExtensions.end(), [](const auto& extension)
	                { return std::strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0; });
	if (debugUtilsAvailable && std::none_of(extensions.begin(), extensions.end(), [](const char* extension)
	                                        { return std::strcmp(extension, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0; }))
	{
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
	if (desc.validation)
	{
		Ursa::UInt32 count = 0;
		Check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerating instance layers");
		Vector<VkLayerProperties> available(count);
		Check(vkEnumerateInstanceLayerProperties(&count, available.data()), "Enumerating instance layers");
		bool found = std::any_of(available.begin(), available.end(), [](const auto& layer)
		                         { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
		Require(found, "VK_LAYER_KHRONOS_validation is required in Debug mode");
		layers.push_back("VK_LAYER_KHRONOS_validation");
		Require(debugUtilsAvailable, "VK_EXT_debug_utils is required for Vulkan validation in Debug mode");
	}
	VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	app.pApplicationName = "UrsaEngine";
	app.apiVersion = VK_API_VERSION_1_2;
	VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.pApplicationInfo = &app;
	instanceInfo.enabledExtensionCount = static_cast<Ursa::UInt32>(extensions.size());
	instanceInfo.ppEnabledExtensionNames = extensions.data();
	instanceInfo.enabledLayerCount = static_cast<Ursa::UInt32>(layers.size());
	instanceInfo.ppEnabledLayerNames = layers.data();
	Check(vkCreateInstance(&instanceInfo, nullptr, &ctx->instance), "Creating Vulkan instance");
	ctx->debugUtilsEnabled = debugUtilsAvailable;
	if (ctx->debugUtilsEnabled)
	{
		ctx->beginDebugLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
		    vkGetInstanceProcAddr(ctx->instance, "vkCmdBeginDebugUtilsLabelEXT"));
		ctx->endDebugLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
		    vkGetInstanceProcAddr(ctx->instance, "vkCmdEndDebugUtilsLabelEXT"));
		if (!ctx->beginDebugLabel || !ctx->endDebugLabel)
		{
			ctx->beginDebugLabel = nullptr;
			ctx->endDebugLabel = nullptr;
			ctx->debugUtilsEnabled = false;
		}
	}
	if (desc.validation)
	{
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
		debugInfo.messageSeverity =
		    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugInfo.pfnUserCallback = ValidationCallback;
		debugInfo.pUserData = ctx.get();
		auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		    vkGetInstanceProcAddr(ctx->instance, "vkCreateDebugUtilsMessengerEXT"));
		Require(create, "Vulkan debug messenger extension unavailable");
		Check(create(ctx->instance, &debugInfo, nullptr, &ctx->debug), "Creating Vulkan debug messenger");
	}
	if (ctx->window)
	{
		Check(glfwCreateWindowSurface(ctx->instance,
		                              static_cast<GLFWwindow*>(Detail::WindowAccess::Native(*ctx->window)), nullptr,
		                              &ctx->surface),
		      "Creating window surface");
	}
	Ursa::UInt32 physicalCount = 0;
	Check(vkEnumeratePhysicalDevices(ctx->instance, &physicalCount, nullptr), "Enumerating Vulkan devices");
	Require(physicalCount, "No Vulkan physical device found");
	Vector<VkPhysicalDevice> devices(physicalCount);
	Check(vkEnumeratePhysicalDevices(ctx->instance, &physicalCount, devices.data()), "Enumerating Vulkan devices");
	for (auto physical : devices)
	{
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(physical, &props);
		if (VK_VERSION_MAJOR(props.apiVersion) < 1 ||
		    (VK_VERSION_MAJOR(props.apiVersion) == 1 && VK_VERSION_MINOR(props.apiVersion) < 2))
		{
			continue;
		}
		Ursa::UInt32 familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
		Vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
		for (Ursa::UInt32 i = 0; i < familyCount; ++i)
		{
			if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
			    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
			{
				continue;
			}
			VkBool32 present = VK_TRUE;
			if (ctx->surface)
			{
				Check(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, ctx->surface, &present),
				      "Checking presentation support");
			}
			if (!present)
			{
				continue;
			}
			Ursa::UInt32 extensionCount = 0;
			Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount, nullptr),
			      "Enumerating device extensions");
			Vector<VkExtensionProperties> available(extensionCount);
			Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount, available.data()),
			      "Enumerating device extensions");
			const bool swapchainSupported =
			    std::any_of(available.begin(), available.end(), [](const auto& e)
			                { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; });
			if (ctx->surface && !swapchainSupported)
			{
				continue;
			}
			if (!ctx->physical || (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
			                       ctx->properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
			{
				ctx->physical = physical;
				ctx->family = i;
				ctx->properties = props;
				ctx->supportsTimestamps =
				    families[i].timestampValidBits > 0 && props.limits.timestampComputeAndGraphics;
			}
		}
	}
	Require(ctx->physical != VK_NULL_HANDLE, "No Vulkan 1.2 graphics/compute device supports the requested surface");
	Ursa::Float32 priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	queueInfo.queueFamilyIndex = ctx->family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	VkPhysicalDeviceFeatures features{};
	vkGetPhysicalDeviceFeatures(ctx->physical, &ctx->features);
	features.shaderStorageImageExtendedFormats = ctx->features.shaderStorageImageExtendedFormats;
	VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.pEnabledFeatures = &features;
	const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if (ctx->surface)
	{
		deviceInfo.enabledExtensionCount = 1;
		deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
	}
	Check(vkCreateDevice(ctx->physical, &deviceInfo, nullptr, &ctx->device), "Creating Vulkan device");
	vkGetDeviceQueue(ctx->device, ctx->family, 0, &ctx->queue);
	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
	allocatorInfo.instance = ctx->instance;
	allocatorInfo.physicalDevice = ctx->physical;
	allocatorInfo.device = ctx->device;
#if defined(URSA_ENABLE_TRACY_MEMORY) || defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	ctx->memoryProfiling = RegisterDeviceMemoryProfiling();
#endif
#if defined(URSA_ENABLE_TRACY_MEMORY)
	ctx->memoryCallbacks.pfnAllocate = DeviceMemoryAllocated;
	ctx->memoryCallbacks.pfnFree = DeviceMemoryReleased;
	ctx->memoryCallbacks.pUserData = ctx->memoryProfiling;
	allocatorInfo.pDeviceMemoryCallbacks = &ctx->memoryCallbacks;
#endif
	Check(vmaCreateAllocator(&allocatorInfo, &ctx->allocator), "Creating Vulkan memory allocator");
	Array<VkDescriptorPoolSize, 8> poolSizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
	                                          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
	                                          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
	                                          {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
	                                          {VK_DESCRIPTOR_TYPE_SAMPLER, 1024},
	                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
	                                          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024},
	                                          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1024}}};
	VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1024;
	poolInfo.poolSizeCount = static_cast<Ursa::UInt32>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	Check(vkCreateDescriptorPool(ctx->device, &poolInfo, nullptr, &ctx->descriptorPool), "Creating descriptor pool");
#if defined(URSA_ENABLE_TRACY)
	if (ctx->supportsTimestamps)
	{
		VkCommandPoolCreateInfo profilingPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		profilingPoolInfo.flags =
		    VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		profilingPoolInfo.queueFamilyIndex = ctx->family;
		VkCommandPool profilingPool = VK_NULL_HANDLE;
		Check(vkCreateCommandPool(ctx->device, &profilingPoolInfo, nullptr, &profilingPool),
		      "Creating Tracy Vulkan command pool");
		VkCommandBufferAllocateInfo profilingBufferInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		profilingBufferInfo.commandPool = profilingPool;
		profilingBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		profilingBufferInfo.commandBufferCount = 1;
		VkCommandBuffer profilingBuffer = VK_NULL_HANDLE;
		Check(vkAllocateCommandBuffers(ctx->device, &profilingBufferInfo, &profilingBuffer),
		      "Allocating Tracy Vulkan command buffer");
		ctx->tracyGpu = TracyVkContext(ctx->instance, ctx->physical, ctx->device, ctx->queue, profilingBuffer,
		                               vkGetInstanceProcAddr, vkGetDeviceProcAddr);
		TracyVkContextName(ctx->tracyGpu, "Ursa Vulkan GPU", sizeof("Ursa Vulkan GPU") - 1);
		vkDestroyCommandPool(ctx->device, profilingPool, nullptr);
	}
#endif
	return ctx;
}

struct BufferImpl final : Buffer
{
	std::shared_ptr<Context> ctx;
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	BufferImpl(std::shared_ptr<Context> c, BufferDesc d) : Buffer(d), ctx(std::move(c))
	{
	}

	~BufferImpl() override
	{
		if (buffer)
		{
			vmaDestroyBuffer(ctx->allocator, buffer, allocation);
		}
	}

	Result<void> Write(Ursa::Size offset, const void* data, Ursa::Size size) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(desc.memory == Memory::Upload, "Buffer is not CPU writable");
			    Require(data && offset <= desc.size && size <= desc.size - offset, "Buffer write range invalid");
			    void* mapped = nullptr;
			    Check(vmaMapMemory(ctx->allocator, allocation, &mapped), "Mapping upload buffer");
			    struct Unmap
			    {
				    VmaAllocator allocator;
				    VmaAllocation allocation;
				    ~Unmap()
				    {
					    vmaUnmapMemory(allocator, allocation);
				    }
			    } unmap{ctx->allocator, allocation};
			    std::memcpy(static_cast<Ursa::UInt8*>(mapped) + offset, data, size);
			    Check(vmaFlushAllocation(ctx->allocator, allocation, offset, size), "Flushing upload buffer");
		    });
	}

	Result<void> Read(Ursa::Size offset, void* data, Ursa::Size size) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(desc.memory == Memory::Readback, "Buffer is not CPU readable");
			    Require(data && offset <= desc.size && size <= desc.size - offset, "Buffer read range invalid");
			    void* mapped = nullptr;
			    Check(vmaMapMemory(ctx->allocator, allocation, &mapped), "Mapping readback buffer");
			    struct Unmap
			    {
				    VmaAllocator allocator;
				    VmaAllocation allocation;
				    ~Unmap()
				    {
					    vmaUnmapMemory(allocator, allocation);
				    }
			    } unmap{ctx->allocator, allocation};
			    Check(vmaInvalidateAllocation(ctx->allocator, allocation, offset, size),
			          "Invalidating readback buffer");
			    std::memcpy(data, static_cast<Ursa::UInt8*>(mapped) + offset, size);
		    });
	}
};

struct SwapchainState
{
	std::shared_ptr<Context> ctx;
	std::shared_ptr<SurfaceState> surface;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	Vector<VkSemaphore> imageAvailable, renderComplete;
	Vector<VkImage> images;
	Vector<bool> presented;
	VkExtent2D extent{};
	Format format = Format::BgrA8Unorm;

	~SwapchainState()
	{
		if (!ctx || !ctx->device)
		{
			return;
		}
		vkDeviceWaitIdle(ctx->device);
		for (auto semaphore : imageAvailable)
		{
			vkDestroySemaphore(ctx->device, semaphore, nullptr);
		}
		for (auto semaphore : renderComplete)
		{
			vkDestroySemaphore(ctx->device, semaphore, nullptr);
		}
		if (swapchain)
		{
			vkDestroySwapchainKHR(ctx->device, swapchain, nullptr);
		}
	}
};

struct TextureImpl final : Texture
{
	std::shared_ptr<Context> ctx;
	std::shared_ptr<SwapchainState> swapchainOwner;
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkImageView attachmentView = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	TextureImpl(std::shared_ptr<Context> c, TextureDesc d) : Texture(d), ctx(std::move(c))
	{
	}

	~TextureImpl() override
	{
		if (attachmentView)
		{
			vkDestroyImageView(ctx->device, attachmentView, nullptr);
		}
		if (view)
		{
			vkDestroyImageView(ctx->device, view, nullptr);
		}
		if (allocation)
		{
			vmaDestroyImage(ctx->allocator, image, allocation);
		}
	}
};

struct FramebufferCacheEntry
{
	std::shared_ptr<Context> ctx;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	Vector<std::shared_ptr<TextureImpl>> attachments;

	~FramebufferCacheEntry()
	{
		if (framebuffer)
		{
			vkDestroyFramebuffer(ctx->device, framebuffer, nullptr);
		}
	}
};

struct FramebufferCache
{
	std::mutex mutex;
	std::map<std::string, std::shared_ptr<FramebufferCacheEntry>> entries;

	std::shared_ptr<FramebufferCacheEntry> Acquire(const std::shared_ptr<Context>& ctx, const std::string& key,
	                                               VkRenderPass pass, const Vector<VkImageView>& views,
	                                               VkExtent2D extent,
	                                               const Vector<std::shared_ptr<TextureImpl>>& attachments)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (const auto found = entries.find(key); found != entries.end())
		{
			return found->second;
		}
		for (auto it = entries.begin(); entries.size() >= 64 && it != entries.end();)
		{
			it = it->second.use_count() == 1 ? entries.erase(it) : std::next(it);
		}
		auto entry = std::make_shared<FramebufferCacheEntry>();
		entry->ctx = ctx;
		entry->attachments = attachments;
		VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		info.renderPass = pass;
		info.attachmentCount = UInt32(views.size());
		info.pAttachments = views.data();
		info.width = extent.width;
		info.height = extent.height;
		info.layers = 1;
		Check(vkCreateFramebuffer(ctx->device, &info, nullptr, &entry->framebuffer), "Creating framebuffer");
		if (entries.size() < 64)
		{
			entries.emplace(key, entry);
		}
		return entry;
	}
};

struct SamplerImpl final : Sampler
{
	std::shared_ptr<Context> ctx;
	VkSampler sampler = VK_NULL_HANDLE;

	~SamplerImpl() override
	{
		if (sampler)
		{
			vkDestroySampler(ctx->device, sampler, nullptr);
		}
	}
};

struct ShaderImpl final : Shader
{
	std::shared_ptr<Context> ctx;
	VkShaderModule module = VK_NULL_HANDLE;
	ShaderStage stage{};
	String entry;

	~ShaderImpl() override
	{
		if (module)
		{
			vkDestroyShaderModule(ctx->device, module, nullptr);
		}
	}
};

struct LayoutImpl final : BindGroupLayout
{
	std::shared_ptr<Context> ctx;
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	Vector<LayoutBinding> bindings;

	~LayoutImpl() override
	{
		if (layout)
		{
			vkDestroyDescriptorSetLayout(ctx->device, layout, nullptr);
		}
	}
};

struct DescriptorPoolState
{
	std::shared_ptr<Context> ctx;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	UInt64 generation = 1;
	std::weak_ptr<CommandList> owner;
	UInt32 liveSets = 0;
	UInt64 totalSets = 0;

	~DescriptorPoolState()
	{
		if (pool)
		{
			vkDestroyDescriptorPool(ctx->device, pool, nullptr);
		}
	}
};

std::shared_ptr<DescriptorPoolState> CreateTemporaryDescriptorPool(const std::shared_ptr<Context>& ctx)
{
	auto pool = std::make_shared<DescriptorPoolState>();
	pool->ctx = ctx;
	Array<VkDescriptorPoolSize, 8> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
	                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
	                                      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
	                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
	                                      {VK_DESCRIPTOR_TYPE_SAMPLER, 1024},
	                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
	                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024},
	                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1024}}};
	VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	info.maxSets = 4096;
	info.poolSizeCount = UInt32(sizes.size());
	info.pPoolSizes = sizes.data();
	Check(vkCreateDescriptorPool(ctx->device, &info, nullptr, &pool->pool), "Creating frame descriptor pool");
	return pool;
}

struct GroupImpl final : BindGroup
{
	struct DynamicBuffer
	{
		std::shared_ptr<BufferImpl> buffer;
		Size offset = 0;
		Size range = 0;
		Size alignment = 1;
	};

	std::shared_ptr<Context> ctx;
	std::shared_ptr<LayoutImpl> layout;
	VkDescriptorSet set = VK_NULL_HANDLE;
	Vector<std::shared_ptr<void>> keepAlive;
	Vector<TextureImpl*> sampledTextures;
	Vector<DynamicBuffer> dynamicBuffers;
	std::shared_ptr<DescriptorPoolState> temporaryPool;
	UInt64 poolGeneration = 0;

	~GroupImpl() override
	{
		if (set && ctx->descriptorPool)
		{
			if (!temporaryPool || temporaryPool->generation == poolGeneration)
			{
				vkFreeDescriptorSets(ctx->device, temporaryPool ? temporaryPool->pool : ctx->descriptorPool, 1, &set);
				if (temporaryPool)
				{
					--temporaryPool->liveSets;
				}
			}
		}
	}
};

struct PipelineImpl final : Pipeline
{
	std::shared_ptr<Context> ctx;
	Vector<std::shared_ptr<LayoutImpl>> groupLayouts;
	ShaderPtr shaderA, shaderB;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	bool compute = false;
	bool renderPassCached = false;
	Format colorFormat{}, depthFormat{};
	Vector<Format> colorFormats;
	bool hasDepth = false;

	Vector<VkDescriptorSetLayout> InitializeLayouts(const LayoutPtr& legacy, const Vector<LayoutPtr>& selected)
	{
		Require(!legacy || selected.empty(), "Choose either legacy descriptor layout or explicit descriptor layouts");
		Vector<LayoutPtr> layouts = selected;
		if (legacy)
		{
			layouts.push_back(legacy);
		}
		Require(layouts.size() <= ctx->properties.limits.maxBoundDescriptorSets,
		        "Descriptor group count exceeds adapter limit");
		Vector<VkDescriptorSetLayout> result;
		for (const auto& input : layouts)
		{
			auto group = std::dynamic_pointer_cast<LayoutImpl>(input);
			Require(group && group->ctx == ctx, "Invalid or foreign descriptor layout");
			groupLayouts.push_back(group);
			result.push_back(group->layout);
		}
		return result;
	}

	~PipelineImpl() override
	{
		if (pipeline)
		{
			vkDestroyPipeline(ctx->device, pipeline, nullptr);
		}
		if (renderPass && !renderPassCached)
		{
			vkDestroyRenderPass(ctx->device, renderPass, nullptr);
		}
		if (layout)
		{
			vkDestroyPipelineLayout(ctx->device, layout, nullptr);
		}
	}
};

template <class T, class B>
std::shared_ptr<T> checked(const std::shared_ptr<B>& base, const std::shared_ptr<Context>& ctx, const char* kind)
{
	auto object = std::dynamic_pointer_cast<T>(base);
	Require(object && object->ctx == ctx, String("Invalid or foreign ") + kind);
	return object;
}

BindGroupPtr CreateDescriptorGroup(const std::shared_ptr<Context>& ctx, const BindGroupDesc& desc,
                                   const std::shared_ptr<DescriptorPoolState>& temporaryPool)
{
	auto layout = checked<LayoutImpl>(desc.layout, ctx, "descriptor layout");
	Require(desc.resources.size() == layout->bindings.size(), "Descriptor group resource count does not match layout");
	auto group = std::make_shared<GroupImpl>();
	group->ctx = ctx;
	group->layout = layout;
	group->temporaryPool = temporaryPool;
	group->poolGeneration = temporaryPool ? temporaryPool->generation : 0;
	VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	alloc.descriptorPool = temporaryPool ? temporaryPool->pool : ctx->descriptorPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &layout->layout;
	Check(vkAllocateDescriptorSets(ctx->device, &alloc, &group->set), "Allocating descriptor group");
	if (temporaryPool)
	{
		++temporaryPool->liveSets;
		++temporaryPool->totalSets;
	}
	Vector<VkDescriptorBufferInfo> bufferInfos;
	bufferInfos.reserve(desc.resources.size());
	Vector<VkDescriptorImageInfo> imageInfos;
	imageInfos.reserve(desc.resources.size());
	Vector<VkWriteDescriptorSet> writes;
	writes.reserve(desc.resources.size());
	for (const auto& binding : layout->bindings)
	{
		auto it = std::find_if(desc.resources.begin(), desc.resources.end(),
		                       [&](const auto& r) { return r.binding == binding.binding; });
		Require(it != desc.resources.end(),
		        String("Missing descriptor binding ") + ToUrsaString(std::to_string(binding.binding)));
		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = group->set;
		write.dstBinding = binding.binding;
		write.descriptorCount = 1;
		write.descriptorType = VkDescriptor(binding.type);
		if (binding.type == BindingType::UniformBuffer || binding.type == BindingType::StorageBuffer ||
		    binding.type == BindingType::DynamicUniformBuffer || binding.type == BindingType::DynamicStorageBuffer)
		{
			auto b = checked<BufferImpl>(it->buffer, ctx, "descriptor buffer");
			const bool uniform =
			    binding.type == BindingType::UniformBuffer || binding.type == BindingType::DynamicUniformBuffer;
			const bool dynamic =
			    binding.type == BindingType::DynamicUniformBuffer || binding.type == BindingType::DynamicStorageBuffer;
			Require(Has(b->desc.usage, uniform ? BufferUsage::Uniform : BufferUsage::Storage),
			        "Buffer lacks descriptor usage");
			const Size alignment = Size(uniform ? ctx->properties.limits.minUniformBufferOffsetAlignment
			                                    : ctx->properties.limits.minStorageBufferOffsetAlignment);
			Require(it->offset <= b->desc.size && it->offset % alignment == 0,
			        "Descriptor buffer offset is out of range or not aligned");
			const Size range = it->size ? it->size : b->desc.size - it->offset;
			Require(range && range <= b->desc.size - it->offset &&
			            range <= (uniform ? ctx->properties.limits.maxUniformBufferRange
			                              : ctx->properties.limits.maxStorageBufferRange),
			        "Descriptor buffer range exceeds its buffer or adapter limit");
			bufferInfos.push_back({b->buffer, it->offset, range});
			if (dynamic)
			{
				group->dynamicBuffers.push_back({b, it->offset, range, alignment});
			}
			write.pBufferInfo = &bufferInfos.back();
			group->keepAlive.push_back(b);
		}
		else
		{
			VkDescriptorImageInfo image{};
			if (binding.type != BindingType::Sampler)
			{
				auto t = checked<TextureImpl>(it->texture, ctx, "descriptor texture");
				Require(Has(t->desc.usage, binding.type == BindingType::StorageTexture ? TextureUsage::Storage
				                                                                       : TextureUsage::Sampled),
				        "Texture lacks descriptor usage");
				image.imageView = t->view;
				image.imageLayout = binding.type == BindingType::StorageTexture
				                        ? VK_IMAGE_LAYOUT_GENERAL
				                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				group->keepAlive.push_back(t);
				if (binding.type == BindingType::SampledTexture || binding.type == BindingType::CombinedImageSampler)
				{
					group->sampledTextures.push_back(t.get());
				}
			}
			if (binding.type == BindingType::Sampler || binding.type == BindingType::CombinedImageSampler)
			{
				auto s = checked<SamplerImpl>(it->sampler, ctx, "descriptor sampler");
				image.sampler = s->sampler;
				group->keepAlive.push_back(s);
			}
			imageInfos.push_back(image);
			write.pImageInfo = &imageInfos.back();
		}
		writes.push_back(write);
	}
	vkUpdateDescriptorSets(ctx->device, static_cast<Ursa::UInt32>(writes.size()), writes.data(), 0, nullptr);
	return std::static_pointer_cast<BindGroup>(group);
}

struct PassAttachment
{
	Format format = Format::RgbA8Unorm;
	AttachmentLoadOp load = AttachmentLoadOp::Clear;
	AttachmentStoreOp store = AttachmentStoreOp::Store;
	AttachmentLoadOp stencilLoad = AttachmentLoadOp::DontCare;
	AttachmentStoreOp stencilStore = AttachmentStoreOp::DontCare;
};

VkAttachmentLoadOp ToVkLoadOp(AttachmentLoadOp value)
{
	switch (value)
	{
		case AttachmentLoadOp::Load:
			return VK_ATTACHMENT_LOAD_OP_LOAD;
		case AttachmentLoadOp::Clear:
			return VK_ATTACHMENT_LOAD_OP_CLEAR;
		case AttachmentLoadOp::DontCare:
			return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}
	throw std::runtime_error("Unknown attachment load operation");
}

VkAttachmentStoreOp ToVkStoreOp(AttachmentStoreOp value)
{
	switch (value)
	{
		case AttachmentStoreOp::Store:
			return VK_ATTACHMENT_STORE_OP_STORE;
		case AttachmentStoreOp::DontCare:
			return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	throw std::runtime_error("Unknown attachment store operation");
}

VkRenderPass MakeRenderPass(VkDevice device, const Vector<PassAttachment>& colors, bool depth,
                            PassAttachment depthDescription)
{
	Vector<VkAttachmentDescription> attachments(colors.size() + (depth ? 1 : 0));
	Vector<VkAttachmentReference> colorReferences;
	for (Size index = 0; index < colors.size(); ++index)
	{
		auto& attachment = attachments[index];
		attachment = {};
		attachment.format = ToVkFormat(colors[index].format);
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = ToVkLoadOp(colors[index].load);
		attachment.storeOp = ToVkStoreOp(colors[index].store);
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorReferences.push_back({UInt32(index), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
	}
	VkAttachmentReference depthRef{UInt32(colors.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	if (depth)
	{
		auto& attachment = attachments.back();
		attachment = {};
		attachment.format = ToVkFormat(depthDescription.format);
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = ToVkLoadOp(depthDescription.load);
		attachment.storeOp = ToVkStoreOp(depthDescription.store);
		const bool stencil = depthDescription.format == Format::D32FloatS8Uint;
		attachment.stencilLoadOp = stencil ? ToVkLoadOp(depthDescription.stencilLoad) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp =
		    stencil ? ToVkStoreOp(depthDescription.stencilStore) : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = UInt32(colors.size());
	subpass.pColorAttachments = colorReferences.data();
	subpass.pDepthStencilAttachment = depth ? &depthRef : nullptr;
	Array<VkSubpassDependency, 2> deps{};
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	deps[0].dstStageMask =
	    (colors.empty() ? 0 : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) |
	    (depth ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : 0);
	deps[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	deps[0].dstAccessMask =
	    (colors.empty() ? 0 : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) |
	    (depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = deps[0].dstStageMask;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	deps[1].srcAccessMask = deps[0].dstAccessMask;
	deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	info.attachmentCount = UInt32(attachments.size());
	info.pAttachments = attachments.data();
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 2;
	info.pDependencies = deps.data();
	VkRenderPass pass = VK_NULL_HANDLE;
	Check(vkCreateRenderPass(device, &info, nullptr, &pass), "Creating render pass");
	return pass;
}

struct CachedRenderPass
{
	VkRenderPass pass = VK_NULL_HANDLE;
	bool cached = false;
};

CachedRenderPass GetRenderPass(const std::shared_ptr<Context>& ctx, const Vector<PassAttachment>& colors, bool depth,
                               PassAttachment depthDescription)
{
	std::string key;
	const auto append = [&](UInt32 value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
	append(UInt32(colors.size()));
	append(depth ? 1u : 0u);
	for (const auto& color : colors)
	{
		append(UInt32(color.format));
		append(UInt32(color.load));
		append(UInt32(color.store));
	}
	if (depth)
	{
		append(UInt32(depthDescription.format));
		append(UInt32(depthDescription.load));
		append(UInt32(depthDescription.store));
		append(UInt32(depthDescription.stencilLoad));
		append(UInt32(depthDescription.stencilStore));
	}
	std::lock_guard<std::mutex> lock(ctx->renderPassCacheMutex);
	if (const auto found = ctx->renderPasses.find(key); found != ctx->renderPasses.end())
	{
		return {found->second, true};
	}
	VkRenderPass pass = MakeRenderPass(ctx->device, colors, depth, depthDescription);
	if (ctx->renderPasses.size() < 64)
	{
		ctx->renderPasses.emplace(std::move(key), pass);
		return {pass, true};
	}
	return {pass, false};
}

struct CommandImpl final : CommandList, std::enable_shared_from_this<CommandImpl>
{
	std::shared_ptr<Context> ctx;
	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandBuffer command = VK_NULL_HANDLE;
	Vector<VkRenderPass> transientPasses;
	std::shared_ptr<FramebufferCache> framebufferCache;
	Vector<std::shared_ptr<void>> keepAlive;
	std::map<TextureImpl*, Vector<ResourceState>> textureStates;
	std::map<BufferImpl*, ResourceState> bufferStates;
	std::map<UInt32, std::shared_ptr<GroupImpl>> boundGroups;
	Vector<TextureImpl*> activeAttachments;

	struct TextureTransition
	{
		std::shared_ptr<TextureImpl> texture;
		TextureSubresourceRange range;
		ResourceState before;
		ResourceState after;
	};

	struct BufferTransition
	{
		std::shared_ptr<BufferImpl> buffer;
		ResourceState before;
		ResourceState after;
	};

	Vector<TextureTransition> textureTransitions;
	Vector<BufferTransition> bufferTransitions;
	std::shared_ptr<PipelineImpl> boundPipeline;
	std::shared_ptr<BufferImpl> boundIndexBuffer;
	std::weak_ptr<Fence> completion;
	std::shared_ptr<DescriptorPoolState> temporaryPool;
	UInt64 resets = 0;
	VkExtent2D extent{};
	Format colorFormat{};
	Vector<Format> colorFormats;
	Format depthFormat{};
	bool hasDepth = false;
	bool rendering = false;
	Ursa::UInt32 debugRegionDepth = 0;
	int state = 0;

	~CommandImpl() override
	{
		if (!ctx)
		{
			return;
		}
		for (auto pass : transientPasses)
		{
			vkDestroyRenderPass(ctx->device, pass, nullptr);
		}
		if (pool)
		{
			vkDestroyCommandPool(ctx->device, pool, nullptr);
		}
	}

	void Recording(bool outside = false) const
	{
		Require(state == 1 && (!outside || !rendering), "Command list must be recording outside a render pass");
	}

	Result<void> Begin() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 0, "Command list can only be recorded once");
			    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
			    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			    Check(vkBeginCommandBuffer(command, &info), "Beginning command list");
			    debugRegionDepth = 0;
			    state = 1;
		    });
	}

	Result<void> End() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    Require(debugRegionDepth == 0, "End all debug regions before ending the command list");
#if defined(URSA_ENABLE_TRACY)
			    if (ctx->tracyGpu)
			    {
				    TracyVkCollect(ctx->tracyGpu, command);
			    }
#endif
			    Check(vkEndCommandBuffer(command), "Ending command list");
			    state = 2;
		    });
	}

	Result<void> Reset() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(!rendering, "End rendering before resetting a command list");
			    if (state == 3)
			    {
				    auto fence = completion.lock();
				    Require(!fence || fence->Completed(), "Command list is still in use by the GPU");
			    }
			    Check(vkResetCommandPool(ctx->device, pool, 0), "Resetting completed command pool");
			    debugRegionDepth = 0;
			    if (temporaryPool)
			    {
				    Require(temporaryPool->generation != (std::numeric_limits<UInt64>::max)(),
				            "Frame descriptor generation exhausted");
				    Check(vkResetDescriptorPool(ctx->device, temporaryPool->pool, 0),
				          "Resetting completed descriptor pool");
				    ++temporaryPool->generation;
				    temporaryPool->liveSets = 0;
			    }
			    for (auto pass : transientPasses)
			    {
				    vkDestroyRenderPass(ctx->device, pass, nullptr);
			    }
			    transientPasses.clear();
			    keepAlive.clear();
			    textureStates.clear();
			    bufferStates.clear();
			    textureTransitions.clear();
			    bufferTransitions.clear();
			    boundGroups.clear();
			    activeAttachments.clear();
			    boundPipeline.reset();
			    boundIndexBuffer.reset();
			    completion.reset();
			    colorFormats.clear();
			    state = 0;
			    ++resets;
		    });
	}

	Result<void> BeginDebugRegion(const String& name) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording();
			    Require(!name.empty() && name.size() <= 1024 && name.find('\0') == String::npos,
			            "Debug region name must be nonempty, contain no null bytes, and fit within 1024 bytes");
			    if (debugRegionDepth == (std::numeric_limits<Ursa::UInt32>::max)())
			    {
				    throw std::runtime_error("Debug region nesting limit exceeded");
			    }
			    if (ctx->debugUtilsEnabled)
			    {
				    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
				    label.pLabelName = name.c_str();
				    ctx->beginDebugLabel(command, &label);
			    }
			    ++debugRegionDepth;
		    });
	}

	Result<void> EndDebugRegion() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording();
			    Require(debugRegionDepth > 0, "No active debug region");
			    if (ctx->debugUtilsEnabled)
			    {
				    ctx->endDebugLabel(command);
			    }
			    --debugRegionDepth;
		    });
	}

	Result<BindGroupPtr> CreateTransientBindGroup(const BindGroupDesc& desc) override
	{
		return Guarded<BindGroupPtr>(
		    [&]
		    {
			    Recording();
			    if (!temporaryPool)
			    {
				    temporaryPool = CreateTemporaryDescriptorPool(ctx);
				    temporaryPool->owner = shared_from_this();
			    }
			    return CreateDescriptorGroup(ctx, desc, temporaryPool);
		    });
	}

	CommandListStatistics Statistics() const override
	{
		return {resets, temporaryPool ? 1u : 0u, temporaryPool ? temporaryPool->totalSets : 0,
		        temporaryPool ? temporaryPool->liveSets : 0};
	}

	Result<void> Barrier(const TexturePtr& texture, ResourceState before, ResourceState after) override
	{
		return Barrier(texture, TextureSubresourceRange{}, before, after);
	}

	Result<void> Barrier(const TexturePtr& texture, TextureSubresourceRange range, ResourceState before,
	                     ResourceState after) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto t = checked<TextureImpl>(texture, ctx, "texture");
			    Require(range.baseMipLevel < t->desc.mipLevels, "Texture barrier base mip is out of range");
			    const UInt32 mipCount =
			        range.mipLevelCount ? range.mipLevelCount : t->desc.mipLevels - range.baseMipLevel;
			    Require(mipCount && mipCount <= t->desc.mipLevels - range.baseMipLevel,
			            "Texture barrier mip range is out of range");
			    Require(before != ResourceState::VertexBuffer && before != ResourceState::IndexBuffer &&
			                before != ResourceState::UniformRead && before != ResourceState::StorageReadWrite,
			            "Invalid texture state");
			    Require(after != ResourceState::VertexBuffer && after != ResourceState::IndexBuffer &&
			                after != ResourceState::UniformRead && after != ResourceState::StorageReadWrite,
			            "Invalid texture state");
			    auto [tracked, inserted] =
			        textureStates.try_emplace(t.get(), Vector<ResourceState>(t->desc.mipLevels, before));
			    for (UInt32 mip = range.baseMipLevel; mip < range.baseMipLevel + mipCount; ++mip)
			    {
				    Require(tracked->second[mip] == before,
				            "Texture barrier before-state does not match the preceding transition");
			    }
			    const auto src = Access(before), dst = Access(after);
			    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			    b.srcAccessMask = src.access;
			    b.dstAccessMask = dst.access;
			    b.oldLayout = src.layout;
			    b.newLayout = dst.layout;
			    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			    b.image = t->image;
			    b.subresourceRange.aspectMask = ImageAspect(t->desc.format);
			    b.subresourceRange.baseMipLevel = range.baseMipLevel;
			    b.subresourceRange.levelCount = mipCount;
			    b.subresourceRange.layerCount = 1;
			    vkCmdPipelineBarrier(command, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1, &b);
			    for (UInt32 mip = range.baseMipLevel; mip < range.baseMipLevel + mipCount; ++mip)
			    {
				    tracked->second[mip] = after;
			    }
			    textureTransitions.push_back({t, range, before, after});
			    keepAlive.push_back(t);
		    });
	}

	Result<void> Barrier(const BufferPtr& buffer, ResourceState before, ResourceState after) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto b = checked<BufferImpl>(buffer, ctx, "buffer");
			    const auto tracked = bufferStates.find(b.get());
			    Require(tracked == bufferStates.end() || tracked->second == before,
			            "Buffer barrier before-state does not match the preceding transition");
			    const auto src = Access(before), dst = Access(after);
			    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
			    barrier.srcAccessMask = src.access;
			    barrier.dstAccessMask = dst.access;
			    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			    barrier.buffer = b->buffer;
			    barrier.offset = 0;
			    barrier.size = VK_WHOLE_SIZE;
			    vkCmdPipelineBarrier(command, src.stage, dst.stage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
			    bufferStates[b.get()] = after;
			    bufferTransitions.push_back({b, before, after});
			    keepAlive.push_back(b);
		    });
	}

	Result<void> CopyBuffer(const BufferPtr& source, const BufferPtr& destination, Ursa::Size size,
	                        Ursa::Size sourceOffset, Ursa::Size destinationOffset) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto src = checked<BufferImpl>(source, ctx, "source buffer");
			    auto dst = checked<BufferImpl>(destination, ctx, "destination buffer");
			    Require(Has(src->desc.usage, BufferUsage::TransferSource) &&
			                Has(dst->desc.usage, BufferUsage::TransferDestination),
			            "Buffer transfer usage missing");
			    Require(size && sourceOffset <= src->desc.size && size <= src->desc.size - sourceOffset &&
			                destinationOffset <= dst->desc.size && size <= dst->desc.size - destinationOffset,
			            "Buffer copy range invalid");
			    VkBufferCopy region{sourceOffset, destinationOffset, size};
			    vkCmdCopyBuffer(command, src->buffer, dst->buffer, 1, &region);
			    keepAlive.push_back(src);
			    keepAlive.push_back(dst);
		    });
	}

	Result<void> CopyBufferToTexture(const BufferPtr& source, const TexturePtr& destination, Ursa::UInt32 mipLevel,
	                                 Ursa::Size sourceOffset) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto src = checked<BufferImpl>(source, ctx, "source buffer");
			    auto dst = checked<TextureImpl>(destination, ctx, "destination texture");
			    Require(!IsDepthFormat(dst->desc.format), "Buffer-to-texture copies do not support depth formats");
			    Require(Has(src->desc.usage, BufferUsage::TransferSource) &&
			                Has(dst->desc.usage, TextureUsage::TransferDestination),
			            "Buffer-to-texture transfer usage missing");
			    Require(mipLevel < dst->desc.mipLevels, "Buffer-to-texture mip level is out of range");
			    const Ursa::UInt32 width = (std::max)(1u, dst->desc.width >> mipLevel);
			    const Ursa::UInt32 height = (std::max)(1u, dst->desc.height >> mipLevel);
			    const Ursa::Size uploadSize = Ursa::Size(width) * height * PixelSize(dst->desc.format);
			    Require(sourceOffset <= src->desc.size && uploadSize <= src->desc.size - sourceOffset,
			            "Upload buffer too small");
			    VkBufferImageCopy region{};
			    region.bufferOffset = sourceOffset;
			    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			    region.imageSubresource.mipLevel = mipLevel;
			    region.imageSubresource.layerCount = 1;
			    region.imageExtent = {width, height, 1};
			    vkCmdCopyBufferToImage(command, src->buffer, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			                           &region);
			    keepAlive.push_back(src);
			    keepAlive.push_back(dst);
		    });
	}

	Result<void> CopyTextureToBuffer(const TexturePtr& source, const BufferPtr& destination,
	                                 Ursa::UInt32 mipLevel) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto src = checked<TextureImpl>(source, ctx, "source texture");
			    auto dst = checked<BufferImpl>(destination, ctx, "destination buffer");
			    Require(Has(src->desc.usage, TextureUsage::TransferSource) &&
			                Has(dst->desc.usage, BufferUsage::TransferDestination),
			            "Texture-to-buffer transfer usage missing");
			    Require(mipLevel < src->desc.mipLevels, "Texture-to-buffer mip level is out of range");
			    const Ursa::UInt32 width = (std::max)(1u, src->desc.width >> mipLevel);
			    const Ursa::UInt32 height = (std::max)(1u, src->desc.height >> mipLevel);
			    Require(dst->desc.size >= Ursa::Size(width) * height *
			                                  (IsDepthFormat(src->desc.format) ? 4 : PixelSize(src->desc.format)),
			            "Readback buffer too small");
			    VkBufferImageCopy region{};
			    region.imageSubresource.aspectMask =
			        IsDepthFormat(src->desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			    region.imageSubresource.mipLevel = mipLevel;
			    region.imageSubresource.layerCount = 1;
			    region.imageExtent = {width, height, 1};
			    vkCmdCopyImageToBuffer(command, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->buffer, 1,
			                           &region);
			    keepAlive.push_back(src);
			    keepAlive.push_back(dst);
		    });
	}

	Result<void> CopyTexture(const TexturePtr& source, const TexturePtr& destination) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto src = checked<TextureImpl>(source, ctx, "source texture");
			    auto dst = checked<TextureImpl>(destination, ctx, "destination texture");
			    Require(src != dst, "Texture copies cannot read and write the same texture");
			    Require(Has(src->desc.usage, TextureUsage::TransferSource) &&
			                Has(dst->desc.usage, TextureUsage::TransferDestination),
			            "Texture copy transfer usage missing");
			    Require(src->desc.width == dst->desc.width && src->desc.height == dst->desc.height &&
			                src->desc.format == dst->desc.format && src->desc.mipLevels == dst->desc.mipLevels,
			            "Texture copy dimensions, format and mip count must match");
			    Vector<VkImageCopy> regions;
			    for (UInt32 mip = 0; mip < src->desc.mipLevels; ++mip)
			    {
				    VkImageCopy region{};
				    region.srcSubresource.aspectMask =
				        IsDepthFormat(src->desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
				    region.srcSubresource.mipLevel = mip;
				    region.srcSubresource.layerCount = 1;
				    region.dstSubresource = region.srcSubresource;
				    region.extent = {(std::max)(1u, src->desc.width >> mip), (std::max)(1u, src->desc.height >> mip),
				                     1};
				    regions.push_back(region);
				    if (src->desc.format == Format::D32FloatS8Uint)
				    {
					    region.srcSubresource.aspectMask = region.dstSubresource.aspectMask =
					        VK_IMAGE_ASPECT_STENCIL_BIT;
					    regions.push_back(region);
				    }
			    }
			    vkCmdCopyImage(command, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image,
			                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, UInt32(regions.size()), regions.data());
			    keepAlive.push_back(src);
			    keepAlive.push_back(dst);
		    });
	}

	Result<void> BeginRendering(const RenderTarget& target) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    Vector<ColorAttachment> colors = target.colors;
			    if (target.color)
			    {
				    Require(colors.empty(), "Choose either legacy color target or explicit color attachments");
				    ColorAttachment attachment;
				    attachment.texture = target.color;
				    attachment.load = target.clearColor ? AttachmentLoadOp::Clear : AttachmentLoadOp::Load;
				    std::copy(target.colorValue, target.colorValue + 4, attachment.clearValue.begin());
				    colors.push_back(std::move(attachment));
			    }
			    DepthAttachment depthDescription = target.depthAttachment;
			    if (target.depth)
			    {
				    Require(!depthDescription.texture,
				            "Choose either legacy depth target or explicit depth attachment");
				    depthDescription.texture = target.depth;
				    depthDescription.load = target.clearDepth ? AttachmentLoadOp::Clear : AttachmentLoadOp::Load;
				    depthDescription.stencilLoad =
				        target.clearStencil ? AttachmentLoadOp::Clear : AttachmentLoadOp::Load;
				    depthDescription.clearDepth = target.depthValue;
				    depthDescription.clearStencil = target.stencilValue;
			    }
			    Require(!colors.empty() || depthDescription.texture, "Rendering requires at least one attachment");
			    Require(colors.size() <= ctx->properties.limits.maxColorAttachments,
			            "Color attachment count exceeds adapter limit");
			    Vector<std::shared_ptr<TextureImpl>> attachments;
			    Vector<VkImageView> views;
			    Vector<VkClearValue> clears;
			    Vector<PassAttachment> passColors;
			    Vector<Format> formats;
			    VkExtent2D selectedExtent{};
			    const auto add = [&](const std::shared_ptr<TextureImpl>& texture)
			    {
				    if (attachments.empty())
				    {
					    selectedExtent = {texture->desc.width, texture->desc.height};
				    }
				    Require(texture->desc.width == selectedExtent.width &&
				                texture->desc.height == selectedExtent.height,
				            "Attachment sizes differ");
				    for (const auto& existing : attachments)
				    {
					    Require(existing != texture, "Duplicate render attachment");
				    }
				    attachments.push_back(texture);
				    views.push_back(texture->attachmentView ? texture->attachmentView : texture->view);
			    };
			    for (const auto& color : colors)
			    {
				    auto texture = checked<TextureImpl>(color.texture, ctx, "color target");
				    Require(!IsDepthFormat(texture->desc.format) &&
				                Has(texture->desc.usage, TextureUsage::ColorAttachment),
				            "Invalid color attachment format or usage");
				    add(texture);
				    formats.push_back(texture->desc.format);
				    passColors.push_back({texture->desc.format, color.load, color.store});
				    VkClearValue value{};
				    for (Size channel = 0; channel < 4; ++channel)
				    {
					    if (texture->desc.format == Format::R32Uint)
					    {
						    value.color.uint32[channel] = color.clearUint[channel];
					    }
					    else
					    {
						    Require(std::isfinite(color.clearValue[channel]), "Color clear value must be finite");
						    value.color.float32[channel] = color.clearValue[channel];
					    }
				    }
				    clears.push_back(value);
			    }
			    PassAttachment passDepth;
			    if (depthDescription.texture)
			    {
				    auto texture = checked<TextureImpl>(depthDescription.texture, ctx, "depth target");
				    Require(IsDepthFormat(texture->desc.format) &&
				                Has(texture->desc.usage, TextureUsage::DepthAttachment),
				            "Invalid depth attachment format or usage");
				    Require(std::isfinite(depthDescription.clearDepth) && depthDescription.clearDepth >= 0 &&
				                depthDescription.clearDepth <= 1,
				            "Depth clear value must be in [0,1]");
				    add(texture);
				    passDepth = {texture->desc.format, depthDescription.load, depthDescription.store,
				                 depthDescription.stencilLoad, depthDescription.stencilStore};
				    VkClearValue value{};
				    value.depthStencil = {depthDescription.clearDepth, depthDescription.clearStencil};
				    clears.push_back(value);
			    }
			    // A new graphics pass has no descriptor bindings until its pipeline/groups are bound.
			    boundGroups.clear();
			    transientPasses.reserve(transientPasses.size() + 1);
			    keepAlive.reserve(keepAlive.size() + attachments.size());
			    const auto [pass, passCached] =
			        GetRenderPass(ctx, passColors, bool(depthDescription.texture), passDepth);
			    if (!passCached)
			    {
				    transientPasses.push_back(pass);
			    }
			    std::string framebufferKey;
			    const auto appendFramebufferKey = [&](UInt64 value)
			    { framebufferKey.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
			    appendFramebufferKey(reinterpret_cast<UIntPtr>(pass));
			    appendFramebufferKey(selectedExtent.width);
			    appendFramebufferKey(selectedExtent.height);
			    for (const auto view : views)
			    {
				    appendFramebufferKey(reinterpret_cast<UIntPtr>(view));
			    }
			    const auto framebufferResource =
			        framebufferCache->Acquire(ctx, framebufferKey, pass, views, selectedExtent, attachments);
			    const VkFramebuffer framebuffer = framebufferResource->framebuffer;
			    keepAlive.push_back(framebufferResource);
			    VkRenderPassBeginInfo beginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
			    beginInfo.renderPass = pass;
			    beginInfo.framebuffer = framebuffer;
			    beginInfo.renderArea.extent = selectedExtent;
			    if (target.renderArea)
			    {
				    const auto& area = *target.renderArea;
				    Require(area.x >= 0 && area.y >= 0 && area.width && area.height &&
				                UInt64(area.x) + area.width <= selectedExtent.width &&
				                UInt64(area.y) + area.height <= selectedExtent.height,
				            "Render area exceeds attachment bounds");
				    beginInfo.renderArea = {{area.x, area.y}, {area.width, area.height}};
			    }
			    beginInfo.clearValueCount = UInt32(clears.size());
			    beginInfo.pClearValues = clears.data();
			    vkCmdBeginRenderPass(command, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
			    extent = selectedExtent;
			    colorFormats = std::move(formats);
			    hasDepth = bool(depthDescription.texture);
			    depthFormat = passDepth.format;
			    rendering = true;
			    activeAttachments.clear();
			    activeAttachments.reserve(attachments.size());
			    for (const auto& attachment : attachments)
			    {
				    activeAttachments.push_back(attachment.get());
			    }
			    VkViewport viewport{0.0f, Float32(extent.height), Float32(extent.width), -Float32(extent.height), 0.0f,
			                        1.0f};
			    VkRect2D scissor{{0, 0}, extent};
			    vkCmdSetViewport(command, 0, 1, &viewport);
			    vkCmdSetScissor(command, 0, 1, &scissor);
			    boundPipeline.reset();
			    for (const auto& attachment : attachments)
			    {
				    keepAlive.push_back(attachment);
			    }
		    });
	}

	Result<void> EndRendering() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering, "No active render pass");
			    vkCmdEndRenderPass(command);
			    rendering = false;
			    activeAttachments.clear();
			    boundPipeline.reset();
			    boundGroups.clear();
		    });
	}

	Result<void> SetViewport(const ViewportRect& rect) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering, "Viewport requires an active render pass");
			    Require(std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
			                std::isfinite(rect.height) && std::isfinite(rect.minimumDepth) &&
			                std::isfinite(rect.maximumDepth) && rect.x >= 0 && rect.y >= 0 && rect.width > 0 &&
			                rect.height > 0 && rect.x + rect.width <= Float32(extent.width) &&
			                rect.y + rect.height <= Float32(extent.height) && rect.minimumDepth >= 0 &&
			                rect.maximumDepth <= 1 && rect.minimumDepth <= rect.maximumDepth,
			            "Viewport must be a finite positive rectangle inside the target, with depth in [0,1]");
			    VkViewport viewport{rect.x,       rect.y + rect.height, rect.width,
			                        -rect.height, rect.minimumDepth,    rect.maximumDepth};
			    vkCmdSetViewport(command, 0, 1, &viewport);
		    });
	}

	Result<void> BindPipeline(const PipelinePtr& pipeline) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording();
			    auto p = checked<PipelineImpl>(pipeline, ctx, "pipeline");
			    Require(p->compute != rendering, "Pipeline type does not match active render pass");
			    if (rendering)
			    {
				    Require(p->colorFormats == colorFormats && p->hasDepth == hasDepth &&
				                (!hasDepth || p->depthFormat == depthFormat),
				            "Pipeline and render target formats differ");
			    }
			    vkCmdBindPipeline(command,
			                      p->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
			                      p->pipeline);
			    boundPipeline = p;
			    boundGroups.clear();
			    keepAlive.push_back(p);
		    });
	}

	Result<void> BindGroup(const BindGroupPtr& group) override
	{
		return BindGroup(0, group, {});
	}

	Result<void> BindGroup(UInt32 index, const BindGroupPtr& group, const Vector<UInt32>& dynamicOffsets) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording();
			    Require(bool(boundPipeline), "Bind a pipeline before its descriptor group");
			    auto g = checked<GroupImpl>(group, ctx, "descriptor group");
			    Require(!g->temporaryPool || (g->poolGeneration == g->temporaryPool->generation &&
			                                  g->temporaryPool->owner.lock().get() == this),
			            "Transient descriptor group belongs to an expired frame");
			    Require(index < boundPipeline->groupLayouts.size() && boundPipeline->groupLayouts[index] == g->layout,
			            "Descriptor group index/layout does not match pipeline");
			    Require(dynamicOffsets.size() == g->dynamicBuffers.size(),
			            "Dynamic offset count does not match descriptor layout");
			    for (Size slot = 0; slot < dynamicOffsets.size(); ++slot)
			    {
				    const auto& binding = g->dynamicBuffers[slot];
				    const Size offset = dynamicOffsets[slot];
				    Require(offset % binding.alignment == 0, "Dynamic buffer offset is not aligned");
				    Require(binding.offset <= binding.buffer->desc.size &&
				                offset <= binding.buffer->desc.size - binding.offset &&
				                binding.range <= binding.buffer->desc.size - binding.offset - offset,
				            "Dynamic buffer offset/range exceeds its buffer");
			    }
			    if (rendering)
			    {
				    for (auto* sampled : g->sampledTextures)
				    {
					    Require(std::find(activeAttachments.begin(), activeAttachments.end(), sampled) ==
					                activeAttachments.end(),
					            "A render attachment cannot also be sampled in the same pass");
				    }
			    }
			    vkCmdBindDescriptorSets(
			        command, boundPipeline->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
			        boundPipeline->layout, index, 1, &g->set, UInt32(dynamicOffsets.size()), dynamicOffsets.data());
			    boundGroups[index] = g;
			    keepAlive.push_back(g);
		    });
	}

	Result<void> BindVertexBuffer(const BufferPtr& buffer) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering, "Vertex buffer requires an active render pass");
			    auto b = checked<BufferImpl>(buffer, ctx, "vertex buffer");
			    Require(Has(b->desc.usage, BufferUsage::Vertex), "Buffer lacks vertex usage");
			    VkDeviceSize offset = 0;
			    vkCmdBindVertexBuffers(command, 0, 1, &b->buffer, &offset);
			    keepAlive.push_back(b);
		    });
	}

	Result<void> BindIndexBuffer(const BufferPtr& buffer) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering, "Index buffer requires an active render pass");
			    auto b = checked<BufferImpl>(buffer, ctx, "index buffer");
			    Require(Has(b->desc.usage, BufferUsage::Index), "Buffer lacks index usage");
			    vkCmdBindIndexBuffer(command, b->buffer, 0, VK_INDEX_TYPE_UINT32);
			    boundIndexBuffer = b;
			    keepAlive.push_back(b);
		    });
	}

	Result<void> SetScissor(const ScissorRect& rect) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering, "Scissor requires an active render pass");
			    Require(rect.x >= 0 && rect.y >= 0 && rect.width && rect.height,
			            "Scissor position and size must be positive");
			    Require(static_cast<Ursa::UInt64>(rect.x) + rect.width <= extent.width &&
			                static_cast<Ursa::UInt64>(rect.y) + rect.height <= extent.height,
			            "Scissor exceeds render target");
			    VkRect2D scissor{{rect.x, rect.y}, {rect.width, rect.height}};
			    vkCmdSetScissor(command, 0, 1, &scissor);
		    });
	}

	Result<void> Draw(Ursa::UInt32 vertices, Ursa::UInt32 instances) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering && boundPipeline && !boundPipeline->compute,
			            "Draw requires a graphics pipeline and render pass");
			    Require(vertices && instances, "Draw count must be positive");
#if defined(URSA_ENABLE_TRACY)
			    if (ctx->tracyGpu)
			    {
				    TracyVkZone(ctx->tracyGpu, command, "Vulkan.Draw");
				    vkCmdDraw(command, vertices, instances, 0, 0);
			    }
			    else
			    {
				    vkCmdDraw(command, vertices, instances, 0, 0);
			    }
#else
			    vkCmdDraw(command, vertices, instances, 0, 0);
#endif
		    });
	}

	Result<void> DrawIndexed(Ursa::UInt32 indices, Ursa::UInt32 instances, Ursa::UInt32 firstInstance,
	                         Ursa::UInt32 firstIndex, Ursa::Int32 vertexOffset) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering && boundPipeline && !boundPipeline->compute,
			            "Indexed draw requires a graphics pipeline and render pass");
			    Require(indices && instances, "Draw count must be positive");
			    Require(bool(boundIndexBuffer), "Indexed draw requires an index buffer");
			    Require(static_cast<Ursa::UInt64>(firstIndex) + indices <=
			                boundIndexBuffer->desc.size / sizeof(Ursa::UInt32),
			            "Indexed draw exceeds index buffer");
#if defined(URSA_ENABLE_TRACY)
			    if (ctx->tracyGpu)
			    {
				    TracyVkZone(ctx->tracyGpu, command, "Vulkan.DrawIndexed");
				    vkCmdDrawIndexed(command, indices, instances, firstIndex, vertexOffset, firstInstance);
			    }
			    else
			    {
				    vkCmdDrawIndexed(command, indices, instances, firstIndex, vertexOffset, firstInstance);
			    }
#else
			    vkCmdDrawIndexed(command, indices, instances, firstIndex, vertexOffset, firstInstance);
#endif
		    });
	}

	Result<void> Dispatch(Ursa::UInt32 x, Ursa::UInt32 y, Ursa::UInt32 z) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    Require(boundPipeline && boundPipeline->compute, "Dispatch requires a compute pipeline");
			    Require(x && y && z, "Dispatch dimensions must be positive");
			    Require(x <= ctx->properties.limits.maxComputeWorkGroupCount[0] &&
			                y <= ctx->properties.limits.maxComputeWorkGroupCount[1] &&
			                z <= ctx->properties.limits.maxComputeWorkGroupCount[2],
			            "Dispatch dimensions exceed device limits");
#if defined(URSA_ENABLE_TRACY)
			    if (ctx->tracyGpu)
			    {
				    TracyVkZone(ctx->tracyGpu, command, "Vulkan.Dispatch");
				    vkCmdDispatch(command, x, y, z);
			    }
			    else
			    {
				    vkCmdDispatch(command, x, y, z);
			    }
#else
			    vkCmdDispatch(command, x, y, z);
#endif
		    });
	}
};

struct FenceImpl final : Fence
{
	std::shared_ptr<Context> ctx;
	std::shared_ptr<CommandImpl> command;
	std::shared_ptr<SwapchainState> swapchainState;
	VkFence fence = VK_NULL_HANDLE;
	bool submitted = false;

	~FenceImpl() override
	{
		if (fence)
		{
			if (submitted)
			{
				vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX);
			}
			vkDestroyFence(ctx->device, fence, nullptr);
		}
	}

	Result<void> Wait(Ursa::UInt64 timeout) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(fence, "Invalid fence");
			    const VkResult result = vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, timeout);
			    if (result == VK_TIMEOUT)
			    {
				    throw std::runtime_error("Fence wait timed out");
			    }
			    Check(result, "Waiting for GPU fence");
		    });
	}

	bool Completed() const override
	{
		return fence && vkGetFenceStatus(ctx->device, fence) == VK_SUCCESS;
	}
};

struct SwapchainImpl final : Swapchain
{
	std::shared_ptr<Context> ctx;
	std::shared_ptr<Window> window;
	std::shared_ptr<SurfaceState> surface;
	SwapchainDesc desc;
	std::shared_ptr<SwapchainState> state;
	Vector<TexturePtr> textures;
	Array<FencePtr, 2> slotFences;
	Ursa::UInt32 slot = 0, imageIndex = 0;
	bool acquired = false, submitted = false, recreate = false;
	SwapchainStatistics statistics;

	SwapchainStatistics Statistics() const override
	{
		return statistics;
	}

	Ursa::Rhi::Format Format() const override
	{
		return state ? state->format : Format::BgrA8Unorm;
	}

	void Rebuild()
	{
		Require(window && surface && surface->surface, "Swapchain requires a window surface");
		const Ursa::UInt32 width = window->FramebufferWidth(), height = window->FramebufferHeight();
		Require(width && height, "Cannot rebuild swapchain for zero-sized window");
		Check(vkDeviceWaitIdle(ctx->device), "Waiting before swapchain rebuild");
		auto previous = state;
		slotFences = {};
		textures.clear();
		state.reset();
		auto next = std::make_shared<SwapchainState>();
		next->ctx = ctx;
		next->surface = surface;
		VkSurfaceCapabilitiesKHR caps{};
		Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical, surface->surface, &caps),
		      "Querying surface capabilities");
		Ursa::UInt32 formatCount = 0;
		Check(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical, surface->surface, &formatCount, nullptr),
		      "Querying surface formats");
		Require(formatCount, "Surface has no formats");
		Vector<VkSurfaceFormatKHR> formats(formatCount);
		Check(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical, surface->surface, &formatCount, formats.data()),
		      "Querying surface formats");
		VkSurfaceFormatKHR chosen = formats.front();
		for (const auto& f : formats)
		{
			if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
			    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				chosen = f;
				break;
			}
		}
		Require(chosen.format == VK_FORMAT_B8G8R8A8_UNORM || chosen.format == VK_FORMAT_R8G8B8A8_UNORM,
		        "Surface lacks an RGBA8/BGRA8 UNORM format");
		next->format = FromVkFormat(chosen.format);
		Ursa::UInt32 modeCount = 0;
		Check(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical, surface->surface, &modeCount, nullptr),
		      "Querying present modes");
		Vector<VkPresentModeKHR> modes(modeCount);
		if (modeCount)
		{
			Check(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical, surface->surface, &modeCount, modes.data()),
			      "Querying present modes");
		}
		VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
		if (!desc.vsync)
		{
			if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
			{
				mode = VK_PRESENT_MODE_MAILBOX_KHR;
			}
			else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
			{
				mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}
		}
		next->extent = caps.currentExtent.width == UINT32_MAX
		                   ? VkExtent2D{std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
		                                std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)}
		                   : caps.currentExtent;
		Ursa::UInt32 imageCount = std::max(2u, caps.minImageCount + 1);
		if (caps.maxImageCount)
		{
			imageCount = std::min(imageCount, caps.maxImageCount);
		}
		VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
		info.surface = surface->surface;
		info.minImageCount = imageCount;
		info.imageFormat = chosen.format;
		info.imageColorSpace = chosen.colorSpace;
		info.imageExtent = next->extent;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		const bool readable = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
		if (readable)
		{
			info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = caps.currentTransform;
		info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
		                          ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
		                          : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha &
		                                                                     (~caps.supportedCompositeAlpha + 1));
		info.presentMode = mode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = previous ? previous->swapchain : VK_NULL_HANDLE;
		Check(vkCreateSwapchainKHR(ctx->device, &info, nullptr, &next->swapchain), "Creating swapchain");
		Ursa::UInt32 actualCount = 0;
		Check(vkGetSwapchainImagesKHR(ctx->device, next->swapchain, &actualCount, nullptr), "Getting swapchain images");
		next->images.resize(actualCount);
		Check(vkGetSwapchainImagesKHR(ctx->device, next->swapchain, &actualCount, next->images.data()),
		      "Getting swapchain images");
		next->presented.resize(actualCount, false);
		VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		for (Ursa::UInt32 i = 0; i < 2; ++i)
		{
			VkSemaphore s = VK_NULL_HANDLE;
			Check(vkCreateSemaphore(ctx->device, &semaphoreInfo, nullptr, &s), "Creating acquire semaphore");
			next->imageAvailable.push_back(s);
		}
		for (Ursa::UInt32 i = 0; i < actualCount; ++i)
		{
			VkSemaphore s = VK_NULL_HANDLE;
			Check(vkCreateSemaphore(ctx->device, &semaphoreInfo, nullptr, &s), "Creating present semaphore");
			next->renderComplete.push_back(s);
		}
		Vector<TexturePtr> newTextures;
		for (auto image : next->images)
		{
			TextureDesc textureDesc{next->extent.width, next->extent.height, next->format,
			                        readable ? TextureUsage::ColorAttachment | TextureUsage::TransferSource
			                                 : TextureUsage::ColorAttachment};
			auto texture = std::make_shared<TextureImpl>(ctx, textureDesc);
			texture->swapchainOwner = next;
			texture->image = image;
			VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = chosen.format;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.levelCount = viewInfo.subresourceRange.layerCount = 1;
			Check(vkCreateImageView(ctx->device, &viewInfo, nullptr, &texture->view), "Creating swapchain image view");
			newTextures.push_back(texture);
		}
		state = next;
		textures = std::move(newTextures);
		slot = 0;
		acquired = false;
		submitted = false;
		recreate = false;
		++statistics.rebuilds;
	}

	Result<AcquiredImage> Acquire() override
	{
		return Guarded<AcquiredImage>(
		    [&]
		    {
			    Require(!acquired, "Previous swapchain image must be presented first");
			    if (!window->FramebufferWidth() || !window->FramebufferHeight())
			    {
				    return AcquiredImage{};
			    }
			    if (recreate || !state || state->extent.width != window->FramebufferWidth() ||
			        state->extent.height != window->FramebufferHeight())
			    {
				    Rebuild();
			    }
			    if (slotFences[slot])
			    {
				    if (!slotFences[slot]->Completed())
				    {
					    auto done = slotFences[slot]->Wait();
					    Require(bool(done), done.error);
				    }
				    slotFences[slot].reset();
			    }
			    VkResult result = vkAcquireNextImageKHR(ctx->device, state->swapchain, UINT64_MAX,
			                                            state->imageAvailable[slot], VK_NULL_HANDLE, &imageIndex);
			    if (result == VK_ERROR_OUT_OF_DATE_KHR)
			    {
				    Rebuild();
				    result = vkAcquireNextImageKHR(ctx->device, state->swapchain, UINT64_MAX,
				                                   state->imageAvailable[slot], VK_NULL_HANDLE, &imageIndex);
			    }
			    Require(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR,
			            String("Acquiring swapchain image failed (VkResult ") + ToUrsaString(std::to_string(result)) +
			                ")");
			    acquired = true;
			    submitted = false;
			    ++statistics.acquisitions;
			    return AcquiredImage{textures[imageIndex],
			                         state->presented[imageIndex] ? ResourceState::Present : ResourceState::Undefined};
		    });
	}

	Result<void> Present() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(acquired && submitted, "Present requires an acquired and submitted image");
			    VkSemaphore wait = state->renderComplete[imageIndex];
			    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
			    info.waitSemaphoreCount = 1;
			    info.pWaitSemaphores = &wait;
			    info.swapchainCount = 1;
			    info.pSwapchains = &state->swapchain;
			    info.pImageIndices = &imageIndex;
			    VkResult result;
			    {
				    std::lock_guard<std::mutex> lock(ctx->queueMutex);
				    result = vkQueuePresentKHR(ctx->queue, &info);
			    }
			    Require(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR,
			            String("Present failed (VkResult ") + ToUrsaString(std::to_string(result)) + ")");
			    state->presented[imageIndex] = true;
			    recreate = result != VK_SUCCESS;
			    acquired = submitted = false;
			    slot = (slot + 1) % 2;
			    ++statistics.presentations;
		    });
	}
};

struct DeviceImpl final : Device
{
	std::shared_ptr<Context> ctx;
	std::shared_ptr<FramebufferCache> framebufferCache;

	explicit DeviceImpl(std::shared_ptr<Context> c)
	    : ctx(std::move(c)), framebufferCache(std::make_shared<FramebufferCache>())
	{
	}

	Ursa::Rhi::AdapterInfo AdapterInfo() const override
	{
		return {ctx->properties.deviceName, ctx->properties.vendorID, ctx->properties.deviceID,
		        ctx->properties.apiVersion};
	}

	Ursa::Rhi::Capabilities Capabilities() const override
	{
		return {true,
		        true,
		        true,
		        true,
		        true,
		        ctx->properties.limits.maxImageDimension2D,
		        ctx->properties.limits.maxPushConstantsSize,
		        ctx->properties.limits.maxStorageBufferRange,
		        ctx->properties.limits.maxColorAttachments,
		        ctx->properties.limits.maxBoundDescriptorSets,
		        ctx->properties.limits.maxUniformBufferRange,
		        ctx->properties.limits.minUniformBufferOffsetAlignment,
		        ctx->properties.limits.minStorageBufferOffsetAlignment,
		        {ctx->properties.limits.maxComputeWorkGroupCount[0], ctx->properties.limits.maxComputeWorkGroupCount[1],
		         ctx->properties.limits.maxComputeWorkGroupCount[2]},
		        {ctx->properties.limits.maxComputeWorkGroupSize[0], ctx->properties.limits.maxComputeWorkGroupSize[1],
		         ctx->properties.limits.maxComputeWorkGroupSize[2]},
		        ctx->properties.limits.maxComputeWorkGroupInvocations};
	}

	Result<FormatCapabilities> QueryFormat(Format format) const override
	{
		return Guarded<FormatCapabilities>(
		    [&]
		    {
			    VkFormatProperties properties{};
			    vkGetPhysicalDeviceFormatProperties(ctx->physical, ToVkFormat(format), &properties);
			    const auto flags = properties.optimalTilingFeatures;
			    return FormatCapabilities{
			        bool(flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
			        bool(flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) &&
			            (format != Format::RgbA16Float || bool(ctx->features.shaderStorageImageExtendedFormats)),
			        bool(flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
			        bool(flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
			        bool(flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT),
			        bool(flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT),
			        bool(flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)};
		    });
	}

	Result<BufferPtr> CreateBuffer(const BufferDesc& desc) override
	{
		return Guarded<BufferPtr>(
		    [&]
		    {
			    Require(desc.size && desc.usage != BufferUsage::None, "Buffer size and usage must be set");
			    auto resource = std::make_shared<BufferImpl>(ctx, desc);
			    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			    info.size = desc.size;
			    info.usage = VkBufferUsage(desc.usage);
			    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			    VmaAllocationCreateInfo alloc{};
			    alloc.usage = VMA_MEMORY_USAGE_AUTO;
			    if (desc.memory == Memory::Upload)
			    {
				    alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			    }
			    else if (desc.memory == Memory::Readback)
			    {
				    alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			    }
			    Check(vmaCreateBuffer(ctx->allocator, &info, &alloc, &resource->buffer, &resource->allocation, nullptr),
			          "Creating buffer");
			    return std::static_pointer_cast<Buffer>(resource);
		    });
	}

	Result<TexturePtr> CreateTexture(const TextureDesc& desc) override
	{
		return Guarded<TexturePtr>(
		    [&]
		    {
			    Require(desc.width && desc.height && desc.mipLevels && desc.usage != TextureUsage::None,
			            "Texture dimensions and usage must be set");
			    Require(desc.width <= ctx->properties.limits.maxImageDimension2D &&
			                desc.height <= ctx->properties.limits.maxImageDimension2D,
			            "Texture exceeds adapter limit");
			    Ursa::UInt32 maximumMipLevels = 1;
			    for (Ursa::UInt32 dimension = (std::max)(desc.width, desc.height); dimension > 1; dimension >>= 1)
			    {
				    ++maximumMipLevels;
			    }
			    Require(desc.mipLevels <= maximumMipLevels, "Texture mip count exceeds its dimensions");
			    Require(!IsDepthFormat(desc.format) || !Has(desc.usage, TextureUsage::ColorAttachment),
			            "Depth texture cannot be a color attachment");
			    Require(desc.format != Format::RgbA8Srgb || !Has(desc.usage, TextureUsage::Storage),
			            "sRGB texture cannot be a storage image");
			    const auto support = QueryFormat(desc.format);
			    Require(bool(support), support.error);
			    Require((!Has(desc.usage, TextureUsage::Sampled) || support.value.sampled) &&
			                (!Has(desc.usage, TextureUsage::Storage) || support.value.storage) &&
			                (!Has(desc.usage, TextureUsage::ColorAttachment) || support.value.colorAttachment) &&
			                (!Has(desc.usage, TextureUsage::DepthAttachment) || support.value.depthAttachment) &&
			                (!Has(desc.usage, TextureUsage::TransferSource) || support.value.transferSource) &&
			                (!Has(desc.usage, TextureUsage::TransferDestination) || support.value.transferDestination),
			            "Texture format does not support its requested usage");
			    auto texture = std::make_shared<TextureImpl>(ctx, desc);
			    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			    info.imageType = VK_IMAGE_TYPE_2D;
			    info.format = ToVkFormat(desc.format);
			    info.extent = {desc.width, desc.height, 1};
			    info.mipLevels = desc.mipLevels;
			    info.arrayLayers = 1;
			    info.samples = VK_SAMPLE_COUNT_1_BIT;
			    info.tiling = VK_IMAGE_TILING_OPTIMAL;
			    info.usage = VkImageUsage(desc.usage);
			    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			    VmaAllocationCreateInfo alloc{};
			    alloc.usage = VMA_MEMORY_USAGE_AUTO;
			    Check(vmaCreateImage(ctx->allocator, &info, &alloc, &texture->image, &texture->allocation, nullptr),
			          "Creating texture");
			    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			    view.image = texture->image;
			    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			    view.format = info.format;
			    view.subresourceRange.aspectMask = IsDepthFormat(desc.format) && Has(desc.usage, TextureUsage::Sampled)
			                                           ? VK_IMAGE_ASPECT_DEPTH_BIT
			                                           : ImageAspect(desc.format);
			    view.subresourceRange.levelCount = desc.mipLevels;
			    view.subresourceRange.layerCount = 1;
			    Check(vkCreateImageView(ctx->device, &view, nullptr, &texture->view), "Creating texture view");
			    if ((Has(desc.usage, TextureUsage::ColorAttachment) ||
			         Has(desc.usage, TextureUsage::DepthAttachment)) &&
			        (desc.mipLevels > 1 ||
			         (desc.format == Format::D32FloatS8Uint && Has(desc.usage, TextureUsage::Sampled))))
			    {
				    view.subresourceRange.aspectMask = ImageAspect(desc.format);
				    view.subresourceRange.levelCount = 1;
				    Check(vkCreateImageView(ctx->device, &view, nullptr, &texture->attachmentView),
				          "Creating attachment view");
			    }
			    return std::static_pointer_cast<Texture>(texture);
		    });
	}

	Result<SamplerPtr> CreateSampler(const SamplerDesc& desc) override
	{
		return Guarded<SamplerPtr>(
		    [&]
		    {
			    Require(std::isfinite(desc.maxLod) && desc.maxLod >= 0,
			            "Sampler max LOD must be finite and nonnegative");
			    const auto key = std::make_tuple(desc.linear, desc.repeat, desc.maxLod);
			    std::lock_guard<std::mutex> lock(ctx->samplerCacheMutex);
			    auto cached = ctx->samplers.find(key);
			    if (cached != ctx->samplers.end())
			    {
				    if (auto sampler = cached->second.lock())
				    {
					    return std::static_pointer_cast<Sampler>(std::move(sampler));
				    }
			    }
			    for (auto entry = ctx->samplers.begin(); entry != ctx->samplers.end();)
			    {
				    entry = entry->second.expired() ? ctx->samplers.erase(entry) : std::next(entry);
			    }
			    auto sampler = std::make_shared<SamplerImpl>();
			    sampler->ctx = ctx;
			    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			    info.magFilter = info.minFilter = desc.linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			    info.mipmapMode = desc.linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			    info.addressModeU = info.addressModeV = info.addressModeW =
			        desc.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			    info.maxLod = desc.maxLod;
			    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			    Check(vkCreateSampler(ctx->device, &info, nullptr, &sampler->sampler), "Creating sampler");
			    ctx->samplers[key] = sampler;
			    return std::static_pointer_cast<Sampler>(sampler);
		    });
	}

	Result<ShaderPtr> CreateShader(const ShaderDesc& desc) override
	{
		return Guarded<ShaderPtr>(
		    [&]
		    {
			    Require(desc.format == ShaderFormat::Spirv, "Vulkan backend requires SPIR-V shader data");
			    Require(desc.code.size() >= 4 && desc.code.size() % 4 == 0,
			            "SPIR-V bytecode size must be a nonzero multiple of four");
			    Require(!desc.entry.empty(), "Shader entry point is empty");
			    Vector<Ursa::UInt32> aligned(desc.code.size() / 4);
			    std::memcpy(aligned.data(), desc.code.data(), desc.code.size());
			    Require(aligned[0] == 0x07230203, "Shader data lacks SPIR-V magic");
			    auto shader = std::make_shared<ShaderImpl>();
			    shader->ctx = ctx;
			    shader->stage = desc.stage;
			    shader->entry = desc.entry;
			    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			    info.codeSize = desc.code.size();
			    info.pCode = aligned.data();
			    Check(vkCreateShaderModule(ctx->device, &info, nullptr, &shader->module), "Creating shader module");
			    return std::static_pointer_cast<Shader>(shader);
		    });
	}

	Result<LayoutPtr> CreateBindGroupLayout(const Vector<LayoutBinding>& bindings) override
	{
		return Guarded<LayoutPtr>(
		    [&]
		    {
			    auto layout = std::make_shared<LayoutImpl>();
			    layout->ctx = ctx;
			    layout->bindings = bindings;
			    std::sort(layout->bindings.begin(), layout->bindings.end(),
			              [](const LayoutBinding& a, const LayoutBinding& b) { return a.binding < b.binding; });
			    Vector<VkDescriptorSetLayoutBinding> vkBindings;
			    for (const auto& binding : layout->bindings)
			    {
				    Require(binding.count == 1 && VkStages(binding.stages) != 0 && (binding.stages & ~7u) == 0,
				            "Descriptor binding requires one resource and at least one shader stage");
				    Require(std::none_of(vkBindings.begin(), vkBindings.end(),
				                         [&](const auto& b) { return b.binding == binding.binding; }),
				            "Duplicate descriptor binding");
				    VkDescriptorSetLayoutBinding b{};
				    b.binding = binding.binding;
				    b.descriptorType = VkDescriptor(binding.type);
				    b.descriptorCount = 1;
				    b.stageFlags = VkStages(binding.stages);
				    vkBindings.push_back(b);
			    }
			    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			    info.bindingCount = static_cast<Ursa::UInt32>(vkBindings.size());
			    info.pBindings = vkBindings.data();
			    Check(vkCreateDescriptorSetLayout(ctx->device, &info, nullptr, &layout->layout),
			          "Creating descriptor layout");
			    return std::static_pointer_cast<BindGroupLayout>(layout);
		    });
	}

	Result<BindGroupPtr> CreateBindGroup(const BindGroupDesc& desc) override
	{
		return Guarded<BindGroupPtr>([&] { return CreateDescriptorGroup(ctx, desc, {}); });
	}

	Result<PipelinePtr> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override
	{
		return Guarded<PipelinePtr>(
		    [&]
		    {
			    auto vertex = checked<ShaderImpl>(desc.vertex, ctx, "vertex shader");
			    auto fragment = desc.fragment ? checked<ShaderImpl>(desc.fragment, ctx, "fragment shader") : nullptr;
			    Require(vertex->stage == ShaderStage::Vertex &&
			                (!fragment || fragment->stage == ShaderStage::Fragment) && (fragment || desc.depthOnly),
			            "Graphics pipeline shader stages are invalid");
			    Require(!desc.depthOnly || desc.colorFormats.empty(), "Depth-only pipeline cannot have color formats");
			    Vector<Format> formats = desc.depthOnly              ? Vector<Format>{}
			                             : desc.colorFormats.empty() ? Vector<Format>{desc.colorFormat}
			                                                         : desc.colorFormats;
			    Require(formats.size() <= ctx->properties.limits.maxColorAttachments,
			            "Pipeline color attachment count exceeds adapter limit");
			    const bool useDepthStencil = desc.depthTest || desc.stencil.enabled || desc.depthOnly;
			    Require(!useDepthStencil || IsDepthFormat(desc.depthFormat),
			            "Depth or stencil testing requires a depth-stencil format");
			    Require(!desc.stencil.enabled || desc.depthFormat == Format::D32FloatS8Uint,
			            "Stencil testing requires D32FloatS8Uint format");
			    const bool blendingEnabled = desc.blend || desc.blendState.enabled;
			    for (Format format : formats)
			    {
				    Require(!IsDepthFormat(format), "Graphics pipeline needs color formats");
				    auto support = QueryFormat(format);
				    Require(support && support.value.colorAttachment, "Pipeline color format is unsupported");
				    Require(!blendingEnabled || format != Format::R32Uint,
				            "Blending is invalid for integer color format");
			    }
			    Require((static_cast<Ursa::UInt32>(desc.colorWriteMask) &
			             ~static_cast<Ursa::UInt32>(ColorWriteMask::All)) == 0,
			            "Color write mask contains unsupported channels");
			    Require(std::isfinite(desc.depthBiasSlope), "Depth bias slope must be finite");
			    auto pipeline = std::make_shared<PipelineImpl>();
			    pipeline->ctx = ctx;
			    pipeline->shaderA = vertex;
			    pipeline->shaderB = fragment;
			    pipeline->colorFormat = desc.colorFormat;
			    pipeline->colorFormats = formats;
			    pipeline->hasDepth = useDepthStencil;
			    pipeline->depthFormat = desc.depthFormat;
			    const auto layouts = pipeline->InitializeLayouts(desc.layout, desc.layouts);
			    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			    layoutInfo.setLayoutCount = UInt32(layouts.size());
			    layoutInfo.pSetLayouts = layouts.data();
			    Check(vkCreatePipelineLayout(ctx->device, &layoutInfo, nullptr, &pipeline->layout),
			          "Creating graphics pipeline layout");
			    Vector<PassAttachment> passColors;
			    for (Format format : formats)
			    {
				    passColors.push_back({format});
			    }
			    const auto renderPass =
			        GetRenderPass(ctx, passColors, useDepthStencil,
			                      {desc.depthFormat, AttachmentLoadOp::Clear, AttachmentStoreOp::Store,
			                       AttachmentLoadOp::Clear, AttachmentStoreOp::Store});
			    pipeline->renderPass = renderPass.pass;
			    pipeline->renderPassCached = renderPass.cached;
			    VkPipelineShaderStageCreateInfo stages[2]{};
			    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			    stages[0].module = vertex->module;
			    stages[0].pName = vertex->entry.c_str();
			    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			    stages[1].module = fragment ? fragment->module : VK_NULL_HANDLE;
			    stages[1].pName = fragment ? fragment->entry.c_str() : nullptr;
			    VkVertexInputBindingDescription binding{};
			    binding.binding = 0;
			    binding.stride = desc.vertexStride;
			    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			    Vector<VkVertexInputAttributeDescription> attributes;
			    for (const auto& a : desc.attributes)
			    {
				    Require(desc.vertexStride && a.offset < desc.vertexStride, "Vertex attribute exceeds stride");
				    VkFormat format = a.format == VertexFormat::Float2   ? VK_FORMAT_R32G32_SFLOAT
				                      : a.format == VertexFormat::Float3 ? VK_FORMAT_R32G32B32_SFLOAT
				                      : a.format == VertexFormat::UInt4  ? VK_FORMAT_R32G32B32A32_UINT
				                                                         : VK_FORMAT_R32G32B32A32_SFLOAT;
				    attributes.push_back({a.location, 0, format, a.offset});
			    }
			    VkPipelineVertexInputStateCreateInfo vertexInput{
			        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
			    if (desc.vertexStride)
			    {
				    vertexInput.vertexBindingDescriptionCount = 1;
				    vertexInput.pVertexBindingDescriptions = &binding;
			    }
			    vertexInput.vertexAttributeDescriptionCount = static_cast<Ursa::UInt32>(attributes.size());
			    vertexInput.pVertexAttributeDescriptions = attributes.data();
			    VkPipelineInputAssemblyStateCreateInfo assembly{
			        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
			    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
			    viewport.viewportCount = viewport.scissorCount = 1;
			    VkPipelineRasterizationStateCreateInfo raster{
			        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
			    raster.polygonMode = VK_POLYGON_MODE_FILL;
			    const CullMode cullMode = desc.cullBack ? CullMode::Back : desc.cullMode;
			    switch (cullMode)
			    {
				    case CullMode::None:
					    raster.cullMode = VK_CULL_MODE_NONE;
					    break;
				    case CullMode::Front:
					    raster.cullMode = VK_CULL_MODE_FRONT_BIT;
					    break;
				    case CullMode::Back:
					    raster.cullMode = VK_CULL_MODE_BACK_BIT;
					    break;
			    }
			    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
			    raster.depthBiasEnable = desc.depthBiasConstant != 0 || desc.depthBiasSlope != 0.0f;
			    raster.depthBiasConstantFactor = static_cast<float>(desc.depthBiasConstant);
			    raster.depthBiasSlopeFactor = desc.depthBiasSlope;
			    raster.lineWidth = 1.0f;
			    VkPipelineMultisampleStateCreateInfo multisample{
			        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
			    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
			    depth.depthTestEnable = desc.depthTest;
			    depth.depthWriteEnable = desc.depthTest && desc.depthWrite;
			    depth.depthCompareOp = ToVkCompare(desc.depthCompare);
			    depth.stencilTestEnable = desc.stencil.enabled;
			    const auto convertStencilFace = [&](const StencilFaceState& face)
			    {
				    VkStencilOpState state{};
				    state.failOp = ToVkStencilOperation(face.failOperation);
				    state.passOp = ToVkStencilOperation(face.passOperation);
				    state.depthFailOp = ToVkStencilOperation(face.depthFailOperation);
				    state.compareOp = ToVkCompare(face.compare);
				    state.compareMask = desc.stencil.readMask;
				    state.writeMask = desc.stencil.writeMask;
				    state.reference = desc.stencil.reference;
				    return state;
			    };
			    depth.front = convertStencilFace(desc.stencil.front);
			    depth.back = convertStencilFace(desc.stencil.back);
			    VkPipelineColorBlendAttachmentState blend{};
			    blend.colorWriteMask = ToVkColorWriteMask(desc.colorWriteMask);
			    blend.blendEnable = blendingEnabled;
			    blend.srcColorBlendFactor = ToVkBlendFactor(desc.blendState.sourceColor);
			    blend.dstColorBlendFactor = ToVkBlendFactor(desc.blendState.destinationColor);
			    blend.colorBlendOp = ToVkBlendOperation(desc.blendState.colorOperation);
			    blend.srcAlphaBlendFactor = ToVkBlendFactor(desc.blendState.sourceAlpha);
			    blend.dstAlphaBlendFactor = ToVkBlendFactor(desc.blendState.destinationAlpha);
			    blend.alphaBlendOp = ToVkBlendOperation(desc.blendState.alphaOperation);
			    VkPipelineColorBlendStateCreateInfo blendState{
			        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
			    Vector<VkPipelineColorBlendAttachmentState> blends(formats.size(), blend);
			    blendState.attachmentCount = UInt32(blends.size());
			    blendState.pAttachments = blends.data();
			    VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
			    dynamic.dynamicStateCount = 2;
			    dynamic.pDynamicStates = dynamics;
			    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
			    info.stageCount = fragment ? 2 : 1;
			    info.pStages = stages;
			    info.pVertexInputState = &vertexInput;
			    info.pInputAssemblyState = &assembly;
			    info.pViewportState = &viewport;
			    info.pRasterizationState = &raster;
			    info.pMultisampleState = &multisample;
			    info.pDepthStencilState = &depth;
			    info.pColorBlendState = &blendState;
			    info.pDynamicState = &dynamic;
			    info.layout = pipeline->layout;
			    info.renderPass = pipeline->renderPass;
			    Check(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->pipeline),
			          "Creating graphics pipeline");
			    return std::static_pointer_cast<Pipeline>(pipeline);
		    });
	}

	Result<PipelinePtr> CreateComputePipeline(const ComputePipelineDesc& desc) override
	{
		return Guarded<PipelinePtr>(
		    [&]
		    {
			    auto shader = checked<ShaderImpl>(desc.compute, ctx, "compute shader");
			    Require(shader->stage == ShaderStage::Compute, "Compute pipeline requires a compute shader");
			    auto pipeline = std::make_shared<PipelineImpl>();
			    pipeline->ctx = ctx;
			    pipeline->shaderA = shader;
			    pipeline->compute = true;
			    const auto layouts = pipeline->InitializeLayouts(desc.layout, desc.layouts);
			    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			    layoutInfo.setLayoutCount = UInt32(layouts.size());
			    layoutInfo.pSetLayouts = layouts.data();
			    Check(vkCreatePipelineLayout(ctx->device, &layoutInfo, nullptr, &pipeline->layout),
			          "Creating compute pipeline layout");
			    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
			    info.layout = pipeline->layout;
			    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			    info.stage.module = shader->module;
			    info.stage.pName = shader->entry.c_str();
			    Check(vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->pipeline),
			          "Creating compute pipeline");
			    return std::static_pointer_cast<Pipeline>(pipeline);
		    });
	}

	Result<CommandListPtr> CreateCommandList() override
	{
		return Guarded<CommandListPtr>(
		    [&]
		    {
			    auto list = std::make_shared<CommandImpl>();
			    list->ctx = ctx;
			    list->framebufferCache = framebufferCache;
			    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
			    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			    pool.queueFamilyIndex = ctx->family;
			    Check(vkCreateCommandPool(ctx->device, &pool, nullptr, &list->pool), "Creating command pool");
			    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
			    alloc.commandPool = list->pool;
			    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			    alloc.commandBufferCount = 1;
			    Check(vkAllocateCommandBuffers(ctx->device, &alloc, &list->command), "Allocating command list");
			    return std::static_pointer_cast<CommandList>(list);
		    });
	}

	Result<SwapchainPtr> CreateSwapchain(const SwapchainDesc& desc) override
	{
		return Guarded<SwapchainPtr>(
		    [&]
		    {
			    auto targetWindow = desc.window ? desc.window : ctx->window;
			    Require(static_cast<bool>(targetWindow),
			            "Swapchain requires a window; create the Device with a presentation window first");
			    auto swapchain = std::make_shared<SwapchainImpl>();
			    swapchain->ctx = ctx;
			    swapchain->window = std::move(targetWindow);
			    swapchain->desc = desc;
			    auto surface = std::make_shared<SurfaceState>();
			    surface->ctx = ctx;
			    if (swapchain->window == ctx->window)
			    {
				    Require(ctx->surface, "Device was created without a Vulkan presentation surface");
				    surface->surface = ctx->surface;
			    }
			    else
			    {
				    Check(glfwCreateWindowSurface(
				              ctx->instance, static_cast<GLFWwindow*>(Detail::WindowAccess::Native(*swapchain->window)),
				              nullptr, &surface->surface),
				          "Creating additional window surface");
				    surface->ownsSurface = true;
			    }
			    VkBool32 present = VK_FALSE;
			    Check(vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physical, ctx->family, surface->surface, &present),
			          "Checking presentation support for the requested window");
			    Require(present == VK_TRUE, "The selected Vulkan queue cannot present to the requested window");
			    swapchain->surface = std::move(surface);
			    if (swapchain->window->FramebufferWidth() && swapchain->window->FramebufferHeight())
			    {
				    swapchain->Rebuild();
			    }
			    return std::static_pointer_cast<Swapchain>(swapchain);
		    });
	}

	Result<FencePtr> Submit(const CommandListPtr& list, const SwapchainPtr& swapchain) override
	{
		return Guarded<FencePtr>(
		    [&]
		    {
			    auto command = checked<CommandImpl>(list, ctx, "command list");
			    Require(command->state == 2, "Command list must be ended before submission");
			    std::shared_ptr<SwapchainImpl> sc;
			    if (swapchain)
			    {
				    sc = checked<SwapchainImpl>(swapchain, ctx, "swapchain");
				    Require(sc->acquired && !sc->submitted, "Swapchain image must be acquired before submission");
			    }
			    auto fence = std::make_shared<FenceImpl>();
			    fence->ctx = ctx;
			    fence->command = command;
			    if (sc)
			    {
				    fence->swapchainState = sc->state;
			    }
			    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			    Check(vkCreateFence(ctx->device, &fenceInfo, nullptr, &fence->fence), "Creating submission fence");
			    VkSemaphore wait = sc ? sc->state->imageAvailable[sc->slot] : VK_NULL_HANDLE;
			    VkSemaphore signal = sc ? sc->state->renderComplete[sc->imageIndex] : VK_NULL_HANDLE;
			    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
			    info.commandBufferCount = 1;
			    info.pCommandBuffers = &command->command;
			    if (sc)
			    {
				    info.waitSemaphoreCount = info.signalSemaphoreCount = 1;
				    info.pWaitSemaphores = &wait;
				    info.pSignalSemaphores = &signal;
				    info.pWaitDstStageMask = &waitStage;
			    }
			    {
				    std::lock_guard<std::mutex> lock(ctx->queueMutex);
				    auto nextTextureStates = ctx->textureStates;
				    auto nextBufferStates = ctx->bufferStates;
				    for (auto it = nextTextureStates.begin(); it != nextTextureStates.end();)
				    {
					    it = it->second.owner.expired() ? nextTextureStates.erase(it) : std::next(it);
				    }
				    for (auto it = nextBufferStates.begin(); it != nextBufferStates.end();)
				    {
					    it = it->second.owner.expired() ? nextBufferStates.erase(it) : std::next(it);
				    }
				    for (const auto& transition : command->textureTransitions)
				    {
					    auto [tracked, inserted] = nextTextureStates.try_emplace(transition.texture.get());
					    if (inserted || tracked->second.owner.lock() != transition.texture)
					    {
						    tracked->second.owner = transition.texture;
						    tracked->second.states.assign(transition.texture->desc.mipLevels, transition.before);
					    }
					    const UInt32 mipCount = transition.range.mipLevelCount ? transition.range.mipLevelCount
					                                                           : transition.texture->desc.mipLevels -
					                                                                 transition.range.baseMipLevel;
					    for (UInt32 mip = transition.range.baseMipLevel; mip < transition.range.baseMipLevel + mipCount;
					         ++mip)
					    {
						    if (tracked->second.states[mip] != transition.before)
						    {
							    throw std::runtime_error(
							        std::string(
							            "Submitted texture transition does not match the queue resource state (") +
							        std::to_string(transition.texture->desc.width) + "x" +
							        std::to_string(transition.texture->desc.height) + ", mip " + std::to_string(mip) +
							        ", expected " + std::to_string(static_cast<int>(transition.before)) + ", actual " +
							        std::to_string(static_cast<int>(tracked->second.states[mip])) + ")");
						    }
						    tracked->second.states[mip] = transition.after;
					    }
				    }
				    for (const auto& transition : command->bufferTransitions)
				    {
					    auto [tracked, inserted] = nextBufferStates.try_emplace(transition.buffer.get());
					    if (inserted || tracked->second.owner.lock() != transition.buffer)
					    {
						    tracked->second.owner = transition.buffer;
						    tracked->second.states.assign(1, transition.before);
					    }
					    Require(tracked->second.states.front() == transition.before,
					            "Submitted buffer transition does not match the queue resource state");
					    tracked->second.states.front() = transition.after;
				    }
				    Check(vkQueueSubmit(ctx->queue, 1, &info, fence->fence), "Submitting GPU work");
				    ctx->textureStates = std::move(nextTextureStates);
				    ctx->bufferStates = std::move(nextBufferStates);
			    }
			    fence->submitted = true;
			    command->state = 3;
			    command->completion = fence;
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
			    ctx->SampleMemory();
#endif
			    if (sc)
			    {
				    sc->submitted = true;
				    sc->slotFences[sc->slot] = fence;
			    }
			    return std::static_pointer_cast<Fence>(fence);
		    });
	}

	Result<void> WaitIdle() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Check(vkDeviceWaitIdle(ctx->device), "Waiting for device idle");
			    Require(ctx->validationErrors.load(std::memory_order_relaxed) == 0,
			            "Vulkan validation errors were reported; inspect stderr");
		    });
	}
};
} // namespace

Result<DevicePtr> CreateDevice(const DeviceDesc& desc)
{
	return Guarded<DevicePtr>(
	    [&] { return std::static_pointer_cast<Device>(std::make_shared<DeviceImpl>(CreateContext(desc))); });
}
} // namespace Ursa::Rhi
