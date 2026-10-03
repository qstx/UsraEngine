#include <rhi/RHI.hpp>
#include <core/StringInterop.hpp>
#include <rhi/vulkan/ValidationLog.hpp>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vk_mem_alloc.h>

namespace Ursa::Rhi
{
namespace
{
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
		case Format::rgbA8Unorm:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case Format::rgbA8Srgb:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case Format::bgrA8Unorm:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case Format::r32Uint:
			return VK_FORMAT_R32_UINT;
		case Format::d32Float:
			return VK_FORMAT_D32_SFLOAT;
	}
	throw std::runtime_error("Unknown texture format");
}

Format FromVkFormat(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_R8G8B8A8_UNORM:
			return Format::rgbA8Unorm;
		case VK_FORMAT_R8G8B8A8_SRGB:
			return Format::rgbA8Srgb;
		case VK_FORMAT_B8G8R8A8_UNORM:
			return Format::bgrA8Unorm;
		case VK_FORMAT_R32_UINT:
			return Format::r32Uint;
		case VK_FORMAT_D32_SFLOAT:
			return Format::d32Float;
		default:
			throw std::runtime_error("Unsupported surface format");
	}
}

uint32_t PixelSize(Format f)
{
	return f == Format::r32Uint ? 4 : 4;
}

VkBufferUsageFlags VkBufferUsage(BufferUsage usage)
{
	VkBufferUsageFlags flags = 0;
	if (Has(usage, BufferUsage::transferSource))
	{
		flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}
	if (Has(usage, BufferUsage::transferDestination))
	{
		flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	if (Has(usage, BufferUsage::vertex))
	{
		flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if (Has(usage, BufferUsage::index))
	{
		flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if (Has(usage, BufferUsage::uniform))
	{
		flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	if (Has(usage, BufferUsage::storage))
	{
		flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	return flags;
}

VkImageUsageFlags VkImageUsage(TextureUsage usage)
{
	VkImageUsageFlags flags = 0;
	if (Has(usage, TextureUsage::transferSource))
	{
		flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (Has(usage, TextureUsage::transferDestination))
	{
		flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if (Has(usage, TextureUsage::sampled))
	{
		flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if (Has(usage, TextureUsage::storage))
	{
		flags |= VK_IMAGE_USAGE_STORAGE_BIT;
	}
	if (Has(usage, TextureUsage::colorAttachment))
	{
		flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if (Has(usage, TextureUsage::depthAttachment))
	{
		flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	return flags;
}

VkShaderStageFlags VkStages(uint32_t mask)
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
		case ShaderStage::vertex:
			return VK_SHADER_STAGE_VERTEX_BIT;
		case ShaderStage::fragment:
			return VK_SHADER_STAGE_FRAGMENT_BIT;
		case ShaderStage::compute:
			return VK_SHADER_STAGE_COMPUTE_BIT;
	}
	throw std::runtime_error("Unknown shader stage");
}

VkDescriptorType VkDescriptor(BindingType type)
{
	switch (type)
	{
		case BindingType::uniformBuffer:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case BindingType::storageBuffer:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case BindingType::sampledTexture:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case BindingType::storageTexture:
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case BindingType::sampler:
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		case BindingType::combinedImageSampler:
			return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
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
		case ResourceState::undefined:
			return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::present:
			return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
		case ResourceState::transferSource:
			return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
		case ResourceState::transferDestination:
			return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
		case ResourceState::shaderRead:
			return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			        VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		case ResourceState::shaderWrite:
			return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			        VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
		case ResourceState::colorAttachment:
			return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		case ResourceState::depthAttachment:
			return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		case ResourceState::vertexBuffer:
			return {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::indexBuffer:
			return {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::uniformRead:
			return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			        VK_ACCESS_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::storageReadWrite:
			return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
	}
	throw std::runtime_error("Unknown resource state");
}

struct Context
{
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t family = UINT32_MAX;
	VmaAllocator allocator = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties properties{};
	std::atomic<uint32_t> validationErrors{0};
	std::shared_ptr<Window> window;
	std::mutex queueMutex;

	~Context()
	{
		if (device)
		{
			vkDeviceWaitIdle(device);
		}
		if (descriptorPool)
		{
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}
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
	Require(desc.backend == Backend::vulkan, "Requested RHI backend is not implemented");
	auto ctx = std::make_shared<Context>();
	ctx->window = desc.window;
	Vector<const char*> extensions;
	if (ctx->window)
	{
		Require(glfwVulkanSupported() == GLFW_TRUE, "GLFW reports no Vulkan loader or driver");
		uint32_t count = 0;
		const char** required = glfwGetRequiredInstanceExtensions(&count);
		Require(required && count, "GLFW cannot provide Vulkan surface extensions");
		extensions.assign(required, required + count);
	}
	Vector<const char*> layers;
	if (desc.validation)
	{
		uint32_t count = 0;
		Check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerating instance layers");
		Vector<VkLayerProperties> available(count);
		Check(vkEnumerateInstanceLayerProperties(&count, available.data()), "Enumerating instance layers");
		bool found = std::any_of(available.begin(), available.end(), [](const auto& layer)
		                         { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
		Require(found, "VK_LAYER_KHRONOS_validation is required in Debug mode");
		layers.push_back("VK_LAYER_KHRONOS_validation");
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
	VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	app.pApplicationName = "UrsaEngine";
	app.apiVersion = VK_API_VERSION_1_2;
	VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.pApplicationInfo = &app;
	instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	instanceInfo.ppEnabledExtensionNames = extensions.data();
	instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
	instanceInfo.ppEnabledLayerNames = layers.data();
	Check(vkCreateInstance(&instanceInfo, nullptr, &ctx->instance), "Creating Vulkan instance");
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
	uint32_t physicalCount = 0;
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
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
		Vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
		for (uint32_t i = 0; i < familyCount; ++i)
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
			uint32_t extensionCount = 0;
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
			}
		}
	}
	Require(ctx->physical != VK_NULL_HANDLE, "No Vulkan 1.2 graphics/compute device supports the requested surface");
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	queueInfo.queueFamilyIndex = ctx->family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	VkPhysicalDeviceFeatures features{};
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
	Check(vmaCreateAllocator(&allocatorInfo, &ctx->allocator), "Creating Vulkan memory allocator");
	Array<VkDescriptorPoolSize, 6> poolSizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
	                                          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
	                                          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
	                                          {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
	                                          {VK_DESCRIPTOR_TYPE_SAMPLER, 1024},
	                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024}}};
	VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1024;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	Check(vkCreateDescriptorPool(ctx->device, &poolInfo, nullptr, &ctx->descriptorPool), "Creating descriptor pool");
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

	Result<void> Write(size_t offset, const void* data, size_t size) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(desc.memory == Memory::upload, "Buffer is not CPU writable");
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
			    std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
			    Check(vmaFlushAllocation(ctx->allocator, allocation, offset, size), "Flushing upload buffer");
		    });
	}

	Result<void> Read(size_t offset, void* data, size_t size) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(desc.memory == Memory::readback, "Buffer is not CPU readable");
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
			    std::memcpy(data, static_cast<uint8_t*>(mapped) + offset, size);
		    });
	}
};

struct SwapchainState
{
	std::shared_ptr<Context> ctx;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	Vector<VkSemaphore> imageAvailable, renderComplete;
	Vector<VkImage> images;
	Vector<bool> presented;
	VkExtent2D extent{};
	Format format = Format::bgrA8Unorm;

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
	VmaAllocation allocation = VK_NULL_HANDLE;

	TextureImpl(std::shared_ptr<Context> c, TextureDesc d) : Texture(d), ctx(std::move(c))
	{
	}

	~TextureImpl() override
	{
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

struct GroupImpl final : BindGroup
{
	std::shared_ptr<Context> ctx;
	std::shared_ptr<LayoutImpl> layout;
	VkDescriptorSet set = VK_NULL_HANDLE;
	Vector<std::shared_ptr<void>> keepAlive;

	~GroupImpl() override
	{
		if (set && ctx->descriptorPool)
		{
			vkFreeDescriptorSets(ctx->device, ctx->descriptorPool, 1, &set);
		}
	}
};

struct PipelineImpl final : Pipeline
{
	std::shared_ptr<Context> ctx;
	std::shared_ptr<LayoutImpl> groupLayout;
	ShaderPtr shaderA, shaderB;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	bool compute = false;
	Format colorFormat{}, depthFormat{};
	bool hasDepth = false;

	~PipelineImpl() override
	{
		if (pipeline)
		{
			vkDestroyPipeline(ctx->device, pipeline, nullptr);
		}
		if (renderPass)
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

VkRenderPass MakeRenderPass(VkDevice device, Format colorFormat, bool depth, Format depthFormat, bool clearColor,
                            bool clearDepth)
{
	Array<VkAttachmentDescription, 2> attachments{};
	attachments[0].format = VkFormat(colorFormat);
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].initialLayout = attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	if (depth)
	{
		attachments[1].format = VkFormat(depthFormat);
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].initialLayout = attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = depth ? &depthRef : nullptr;
	Array<VkSubpassDependency, 2> deps{};
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	deps[0].dstStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | (depth ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : 0);
	deps[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	deps[0].dstAccessMask =
	    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | (depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = deps[0].dstStageMask;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	deps[1].srcAccessMask = deps[0].dstAccessMask;
	deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	info.attachmentCount = depth ? 2 : 1;
	info.pAttachments = attachments.data();
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 2;
	info.pDependencies = deps.data();
	VkRenderPass pass = VK_NULL_HANDLE;
	Check(vkCreateRenderPass(device, &info, nullptr, &pass), "Creating render pass");
	return pass;
}

struct CommandImpl final : CommandList
{
	std::shared_ptr<Context> ctx;
	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandBuffer command = VK_NULL_HANDLE;
	Vector<VkRenderPass> transientPasses;
	Vector<VkFramebuffer> framebuffers;
	Vector<std::shared_ptr<void>> keepAlive;
	std::shared_ptr<PipelineImpl> boundPipeline;
	std::shared_ptr<BufferImpl> boundIndexBuffer;
	VkExtent2D extent{};
	Format colorFormat{};
	Format depthFormat{};
	bool hasDepth = false;
	bool rendering = false;
	int state = 0;

	~CommandImpl() override
	{
		if (!ctx)
		{
			return;
		}
		for (auto frame : framebuffers)
		{
			vkDestroyFramebuffer(ctx->device, frame, nullptr);
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
			    state = 1;
		    });
	}

	Result<void> End() override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    Check(vkEndCommandBuffer(command), "Ending command list");
			    state = 2;
		    });
	}

	Result<void> Barrier(const TexturePtr& texture, ResourceState before, ResourceState after) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto t = checked<TextureImpl>(texture, ctx, "texture");
			    Require(before != ResourceState::vertexBuffer && before != ResourceState::indexBuffer &&
			                before != ResourceState::uniformRead && before != ResourceState::storageReadWrite,
			            "Invalid texture state");
			    Require(after != ResourceState::vertexBuffer && after != ResourceState::indexBuffer &&
			                after != ResourceState::uniformRead && after != ResourceState::storageReadWrite,
			            "Invalid texture state");
			    const auto src = Access(before), dst = Access(after);
			    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			    b.srcAccessMask = src.access;
			    b.dstAccessMask = dst.access;
			    b.oldLayout = src.layout;
			    b.newLayout = dst.layout;
			    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			    b.image = t->image;
			    b.subresourceRange.aspectMask =
			        t->desc.format == Format::d32Float ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			    b.subresourceRange.levelCount = b.subresourceRange.layerCount = 1;
			    vkCmdPipelineBarrier(command, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1, &b);
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
			    const auto src = Access(before), dst = Access(after);
			    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
			    barrier.srcAccessMask = src.access;
			    barrier.dstAccessMask = dst.access;
			    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			    barrier.buffer = b->buffer;
			    barrier.offset = 0;
			    barrier.size = VK_WHOLE_SIZE;
			    vkCmdPipelineBarrier(command, src.stage, dst.stage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
			    keepAlive.push_back(b);
		    });
	}

	Result<void> CopyBuffer(const BufferPtr& source, const BufferPtr& destination, size_t size, size_t sourceOffset,
	                        size_t destinationOffset) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto src = checked<BufferImpl>(source, ctx, "source buffer");
			    auto dst = checked<BufferImpl>(destination, ctx, "destination buffer");
			    Require(Has(src->desc.usage, BufferUsage::transferSource) &&
			                Has(dst->desc.usage, BufferUsage::transferDestination),
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

	Result<void> CopyBufferToTexture(const BufferPtr& source, const TexturePtr& destination) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto src = checked<BufferImpl>(source, ctx, "source buffer");
			    auto dst = checked<TextureImpl>(destination, ctx, "destination texture");
			    Require(Has(src->desc.usage, BufferUsage::transferSource) &&
			                Has(dst->desc.usage, TextureUsage::transferDestination),
			            "Buffer-to-texture transfer usage missing");
			    Require(src->desc.size >= size_t(dst->desc.width) * dst->desc.height * PixelSize(dst->desc.format),
			            "Upload buffer too small");
			    VkBufferImageCopy region{};
			    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			    region.imageSubresource.layerCount = 1;
			    region.imageExtent = {dst->desc.width, dst->desc.height, 1};
			    vkCmdCopyBufferToImage(command, src->buffer, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			                           &region);
			    keepAlive.push_back(src);
			    keepAlive.push_back(dst);
		    });
	}

	Result<void> CopyTextureToBuffer(const TexturePtr& source, const BufferPtr& destination) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    auto src = checked<TextureImpl>(source, ctx, "source texture");
			    auto dst = checked<BufferImpl>(destination, ctx, "destination buffer");
			    Require(Has(src->desc.usage, TextureUsage::transferSource) &&
			                Has(dst->desc.usage, BufferUsage::transferDestination),
			            "Texture-to-buffer transfer usage missing");
			    Require(dst->desc.size >= size_t(src->desc.width) * src->desc.height * PixelSize(src->desc.format),
			            "Readback buffer too small");
			    VkBufferImageCopy region{};
			    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			    region.imageSubresource.layerCount = 1;
			    region.imageExtent = {src->desc.width, src->desc.height, 1};
			    vkCmdCopyImageToBuffer(command, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->buffer, 1,
			                           &region);
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
			    auto color = checked<TextureImpl>(target.color, ctx, "color target");
			    Require(Has(color->desc.usage, TextureUsage::colorAttachment), "Texture lacks color-attachment usage");
			    Require(color->desc.format != Format::d32Float, "Depth format cannot be a color target");
			    std::shared_ptr<TextureImpl> depth;
			    if (target.depth)
			    {
				    depth = checked<TextureImpl>(target.depth, ctx, "depth target");
				    Require(depth->desc.format == Format::d32Float &&
				                Has(depth->desc.usage, TextureUsage::depthAttachment),
				            "Invalid depth target");
				    Require(depth->desc.width == color->desc.width && depth->desc.height == color->desc.height,
				            "Attachment sizes differ");
			    }
			    VkRenderPass pass =
			        MakeRenderPass(ctx->device, color->desc.format, !!depth,
			                       depth ? depth->desc.format : Format::d32Float, target.colorValue, target.clearDepth);
			    transientPasses.push_back(pass);
			    VkImageView views[2]{color->view, depth ? depth->view : VK_NULL_HANDLE};
			    VkFramebufferCreateInfo frameInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
			    frameInfo.renderPass = pass;
			    frameInfo.attachmentCount = depth ? 2 : 1;
			    frameInfo.pAttachments = views;
			    frameInfo.width = color->desc.width;
			    frameInfo.height = color->desc.height;
			    frameInfo.layers = 1;
			    VkFramebuffer frame = VK_NULL_HANDLE;
			    Check(vkCreateFramebuffer(ctx->device, &frameInfo, nullptr, &frame), "Creating framebuffer");
			    framebuffers.push_back(frame);
			    VkClearValue clears[2]{};
			    for (int i = 0; i < 4; ++i)
			    {
				    clears[0].color.float32[i] = target.colorValue[i];
			    }
			    clears[1].depthStencil.depth = target.depthValue;
			    VkRenderPassBeginInfo beginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
			    beginInfo.renderPass = pass;
			    beginInfo.framebuffer = frame;
			    beginInfo.renderArea.extent = {color->desc.width, color->desc.height};
			    beginInfo.clearValueCount = depth ? 2 : 1;
			    beginInfo.pClearValues = clears;
			    vkCmdBeginRenderPass(command, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
			    extent = beginInfo.renderArea.extent;
			    colorFormat = color->desc.format;
			    hasDepth = !!depth;
			    if (depth)
			    {
				    depthFormat = depth->desc.format;
			    }
			    rendering = true;
			    boundPipeline.reset();
			    keepAlive.push_back(color);
			    if (depth)
			    {
				    keepAlive.push_back(depth);
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
			    boundPipeline.reset();
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
				    Require(p->colorFormat == colorFormat && p->hasDepth == hasDepth &&
				                (!hasDepth || p->depthFormat == depthFormat),
				            "Pipeline and render target formats differ");
			    }
			    vkCmdBindPipeline(command,
			                      p->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
			                      p->pipeline);
			    if (rendering)
			    {
				    VkViewport viewport{0.0f,
				                        static_cast<float>(extent.height),
				                        static_cast<float>(extent.width),
				                        -static_cast<float>(extent.height),
				                        0.0f,
				                        1.0f};
				    VkRect2D scissor{{0, 0}, extent};
				    vkCmdSetViewport(command, 0, 1, &viewport);
				    vkCmdSetScissor(command, 0, 1, &scissor);
			    }
			    boundPipeline = p;
			    keepAlive.push_back(p);
		    });
	}

	Result<void> BindGroup(const BindGroupPtr& group) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording();
			    Require(bool(boundPipeline), "Bind a pipeline before its descriptor group");
			    auto g = checked<GroupImpl>(group, ctx, "descriptor group");
			    Require(boundPipeline->groupLayout == g->layout, "Descriptor group layout does not match pipeline");
			    vkCmdBindDescriptorSets(
			        command, boundPipeline->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
			        boundPipeline->layout, 0, 1, &g->set, 0, nullptr);
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
			    Require(Has(b->desc.usage, BufferUsage::vertex), "Buffer lacks vertex usage");
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
			    Require(Has(b->desc.usage, BufferUsage::index), "Buffer lacks index usage");
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
			    Require(static_cast<uint64_t>(rect.x) + rect.width <= extent.width &&
			                static_cast<uint64_t>(rect.y) + rect.height <= extent.height,
			            "Scissor exceeds render target");
			    VkRect2D scissor{{rect.x, rect.y}, {rect.width, rect.height}};
			    vkCmdSetScissor(command, 0, 1, &scissor);
		    });
	}

	Result<void> Draw(uint32_t vertices, uint32_t instances) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering && boundPipeline && !boundPipeline->compute,
			            "Draw requires a graphics pipeline and render pass");
			    Require(vertices && instances, "Draw count must be positive");
			    vkCmdDraw(command, vertices, instances, 0, 0);
		    });
	}

	Result<void> DrawIndexed(uint32_t indices, uint32_t instances, uint32_t firstInstance, uint32_t firstIndex,
	                         int32_t vertexOffset) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Require(state == 1 && rendering && boundPipeline && !boundPipeline->compute,
			            "Indexed draw requires a graphics pipeline and render pass");
			    Require(indices && instances, "Draw count must be positive");
			    Require(bool(boundIndexBuffer), "Indexed draw requires an index buffer");
			    Require(static_cast<uint64_t>(firstIndex) + indices <= boundIndexBuffer->desc.size / sizeof(uint32_t),
			            "Indexed draw exceeds index buffer");
			    vkCmdDrawIndexed(command, indices, instances, firstIndex, vertexOffset, firstInstance);
		    });
	}

	Result<void> Dispatch(uint32_t x, uint32_t y, uint32_t z) override
	{
		return GuardedVoid(
		    [&]
		    {
			    Recording(true);
			    Require(boundPipeline && boundPipeline->compute, "Dispatch requires a compute pipeline");
			    Require(x && y && z, "Dispatch dimensions must be positive");
			    vkCmdDispatch(command, x, y, z);
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

	Result<void> Wait(uint64_t timeout) override
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
	SwapchainDesc desc;
	std::shared_ptr<SwapchainState> state;
	Vector<TexturePtr> textures;
	Array<FencePtr, 2> slotFences;
	uint32_t slot = 0, imageIndex = 0;
	bool acquired = false, submitted = false, recreate = false;

	Ursa::Rhi::Format Format() const override
	{
		return state ? state->format : Format::bgrA8Unorm;
	}

	void Rebuild()
	{
		Require(ctx->window && ctx->surface, "Swapchain requires a window surface");
		const uint32_t width = ctx->window->FramebufferWidth(), height = ctx->window->FramebufferHeight();
		Require(width && height, "Cannot rebuild swapchain for zero-sized window");
		Check(vkDeviceWaitIdle(ctx->device), "Waiting before swapchain rebuild");
		slotFences = {};
		textures.clear();
		state.reset();
		auto next = std::make_shared<SwapchainState>();
		next->ctx = ctx;
		VkSurfaceCapabilitiesKHR caps{};
		Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical, ctx->surface, &caps),
		      "Querying surface capabilities");
		uint32_t formatCount = 0;
		Check(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical, ctx->surface, &formatCount, nullptr),
		      "Querying surface formats");
		Require(formatCount, "Surface has no formats");
		Vector<VkSurfaceFormatKHR> formats(formatCount);
		Check(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical, ctx->surface, &formatCount, formats.data()),
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
		uint32_t modeCount = 0;
		Check(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical, ctx->surface, &modeCount, nullptr),
		      "Querying present modes");
		Vector<VkPresentModeKHR> modes(modeCount);
		if (modeCount)
		{
			Check(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical, ctx->surface, &modeCount, modes.data()),
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
		uint32_t imageCount = std::max(2u, caps.minImageCount + 1);
		if (caps.maxImageCount)
		{
			imageCount = std::min(imageCount, caps.maxImageCount);
		}
		VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
		info.surface = ctx->surface;
		info.minImageCount = imageCount;
		info.imageFormat = chosen.format;
		info.imageColorSpace = chosen.colorSpace;
		info.imageExtent = next->extent;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = caps.currentTransform;
		info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
		                          ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
		                          : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha &
		                                                                     (~caps.supportedCompositeAlpha + 1));
		info.presentMode = mode;
		info.clipped = VK_TRUE;
		Check(vkCreateSwapchainKHR(ctx->device, &info, nullptr, &next->swapchain), "Creating swapchain");
		uint32_t actualCount = 0;
		Check(vkGetSwapchainImagesKHR(ctx->device, next->swapchain, &actualCount, nullptr), "Getting swapchain images");
		next->images.resize(actualCount);
		Check(vkGetSwapchainImagesKHR(ctx->device, next->swapchain, &actualCount, next->images.data()),
		      "Getting swapchain images");
		next->presented.resize(actualCount, false);
		VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		for (uint32_t i = 0; i < 2; ++i)
		{
			VkSemaphore s = VK_NULL_HANDLE;
			Check(vkCreateSemaphore(ctx->device, &semaphoreInfo, nullptr, &s), "Creating acquire semaphore");
			next->imageAvailable.push_back(s);
		}
		for (uint32_t i = 0; i < actualCount; ++i)
		{
			VkSemaphore s = VK_NULL_HANDLE;
			Check(vkCreateSemaphore(ctx->device, &semaphoreInfo, nullptr, &s), "Creating present semaphore");
			next->renderComplete.push_back(s);
		}
		Vector<TexturePtr> newTextures;
		for (auto image : next->images)
		{
			TextureDesc textureDesc{next->extent.width, next->extent.height, next->format,
			                        TextureUsage::colorAttachment};
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
	}

	Result<AcquiredImage> Acquire() override
	{
		return Guarded<AcquiredImage>(
		    [&]
		    {
			    Require(!acquired, "Previous swapchain image must be presented first");
			    if (!ctx->window->FramebufferWidth() || !ctx->window->FramebufferHeight())
			    {
				    return AcquiredImage{};
			    }
			    if (recreate || !state || state->extent.width != ctx->window->FramebufferWidth() ||
			        state->extent.height != ctx->window->FramebufferHeight())
			    {
				    Rebuild();
			    }
			    if (slotFences[slot])
			    {
				    auto done = slotFences[slot]->Wait();
				    Require(bool(done), done.error);
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
			    return AcquiredImage{textures[imageIndex],
			                         state->presented[imageIndex] ? ResourceState::present : ResourceState::undefined};
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
		    });
	}
};

struct DeviceImpl final : Device
{
	std::shared_ptr<Context> ctx;

	explicit DeviceImpl(std::shared_ptr<Context> c) : ctx(std::move(c))
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
		        ctx->properties.limits.maxPushConstantsSize};
	}

	Result<BufferPtr> CreateBuffer(const BufferDesc& desc) override
	{
		return Guarded<BufferPtr>(
		    [&]
		    {
			    Require(desc.size && desc.usage != BufferUsage::none, "Buffer size and usage must be set");
			    auto resource = std::make_shared<BufferImpl>(ctx, desc);
			    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			    info.size = desc.size;
			    info.usage = VkBufferUsage(desc.usage);
			    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			    VmaAllocationCreateInfo alloc{};
			    alloc.usage = VMA_MEMORY_USAGE_AUTO;
			    if (desc.memory == Memory::upload)
			    {
				    alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			    }
			    else if (desc.memory == Memory::readback)
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
			    Require(desc.width && desc.height && desc.usage != TextureUsage::none,
			            "Texture dimensions and usage must be set");
			    Require(desc.width <= ctx->properties.limits.maxImageDimension2D &&
			                desc.height <= ctx->properties.limits.maxImageDimension2D,
			            "Texture exceeds adapter limit");
			    Require(desc.format != Format::d32Float || !Has(desc.usage, TextureUsage::colorAttachment),
			            "Depth texture cannot be a color attachment");
			    Require(desc.format != Format::rgbA8Srgb || !Has(desc.usage, TextureUsage::storage),
			            "sRGB texture cannot be a storage image");
			    auto texture = std::make_shared<TextureImpl>(ctx, desc);
			    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			    info.imageType = VK_IMAGE_TYPE_2D;
			    info.format = VkFormat(desc.format);
			    info.extent = {desc.width, desc.height, 1};
			    info.mipLevels = 1;
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
			    view.subresourceRange.aspectMask =
			        desc.format == Format::d32Float ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			    view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
			    Check(vkCreateImageView(ctx->device, &view, nullptr, &texture->view), "Creating texture view");
			    return std::static_pointer_cast<Texture>(texture);
		    });
	}

	Result<SamplerPtr> CreateSampler(const SamplerDesc& desc) override
	{
		return Guarded<SamplerPtr>(
		    [&]
		    {
			    auto sampler = std::make_shared<SamplerImpl>();
			    sampler->ctx = ctx;
			    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			    info.magFilter = info.minFilter = desc.linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			    info.mipmapMode = desc.linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			    info.addressModeU = info.addressModeV = info.addressModeW =
			        desc.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			    info.maxLod = 0.0f;
			    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			    Check(vkCreateSampler(ctx->device, &info, nullptr, &sampler->sampler), "Creating sampler");
			    return std::static_pointer_cast<Sampler>(sampler);
		    });
	}

	Result<ShaderPtr> CreateShader(const ShaderDesc& desc) override
	{
		return Guarded<ShaderPtr>(
		    [&]
		    {
			    Require(desc.format == ShaderFormat::spirv, "Vulkan backend requires SPIR-V shader data");
			    Require(desc.code.size() >= 4 && desc.code.size() % 4 == 0,
			            "SPIR-V bytecode size must be a nonzero multiple of four");
			    Require(!desc.entry.empty(), "Shader entry point is empty");
			    Vector<uint32_t> aligned(desc.code.size() / 4);
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
			    Vector<VkDescriptorSetLayoutBinding> vkBindings;
			    for (const auto& binding : bindings)
			    {
				    Require(binding.count == 1 && VkStages(binding.stages) != 0,
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
			    info.bindingCount = static_cast<uint32_t>(vkBindings.size());
			    info.pBindings = vkBindings.data();
			    Check(vkCreateDescriptorSetLayout(ctx->device, &info, nullptr, &layout->layout),
			          "Creating descriptor layout");
			    return std::static_pointer_cast<BindGroupLayout>(layout);
		    });
	}

	Result<BindGroupPtr> CreateBindGroup(const BindGroupDesc& desc) override
	{
		return Guarded<BindGroupPtr>(
		    [&]
		    {
			    auto layout = checked<LayoutImpl>(desc.layout, ctx, "descriptor layout");
			    Require(desc.resources.size() == layout->bindings.size(),
			            "Descriptor group resource count does not match layout");
			    auto group = std::make_shared<GroupImpl>();
			    group->ctx = ctx;
			    group->layout = layout;
			    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
			    alloc.descriptorPool = ctx->descriptorPool;
			    alloc.descriptorSetCount = 1;
			    alloc.pSetLayouts = &layout->layout;
			    Check(vkAllocateDescriptorSets(ctx->device, &alloc, &group->set), "Allocating descriptor group");
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
				    if (binding.type == BindingType::uniformBuffer || binding.type == BindingType::storageBuffer)
				    {
					    auto b = checked<BufferImpl>(it->buffer, ctx, "descriptor buffer");
					    Require(Has(b->desc.usage, binding.type == BindingType::uniformBuffer ? BufferUsage::uniform
					                                                                          : BufferUsage::storage),
					            "Buffer lacks descriptor usage");
					    bufferInfos.push_back({b->buffer, 0, b->desc.size});
					    write.pBufferInfo = &bufferInfos.back();
					    group->keepAlive.push_back(b);
				    }
				    else
				    {
					    VkDescriptorImageInfo image{};
					    if (binding.type != BindingType::sampler)
					    {
						    auto t = checked<TextureImpl>(it->texture, ctx, "descriptor texture");
						    Require(Has(t->desc.usage, binding.type == BindingType::storageTexture
						                                   ? TextureUsage::storage
						                                   : TextureUsage::sampled),
						            "Texture lacks descriptor usage");
						    image.imageView = t->view;
						    image.imageLayout = binding.type == BindingType::storageTexture
						                            ? VK_IMAGE_LAYOUT_GENERAL
						                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						    group->keepAlive.push_back(t);
					    }
					    if (binding.type == BindingType::sampler || binding.type == BindingType::combinedImageSampler)
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
			    vkUpdateDescriptorSets(ctx->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
			    return std::static_pointer_cast<BindGroup>(group);
		    });
	}

	Result<PipelinePtr> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override
	{
		return Guarded<PipelinePtr>(
		    [&]
		    {
			    auto vertex = checked<ShaderImpl>(desc.vertex, ctx, "vertex shader");
			    auto fragment = checked<ShaderImpl>(desc.fragment, ctx, "fragment shader");
			    Require(vertex->stage == ShaderStage::vertex && fragment->stage == ShaderStage::fragment,
			            "Graphics pipeline shader stages are invalid");
			    Require(desc.colorFormat != Format::d32Float, "Graphics pipeline needs a color format");
			    Require(!desc.depthTest || desc.depthFormat == Format::d32Float,
			            "Depth testing requires D32Float format");
			    Require(!desc.blend || desc.colorFormat != Format::r32Uint,
			            "Blending is invalid for integer color format");
			    auto pipeline = std::make_shared<PipelineImpl>();
			    pipeline->ctx = ctx;
			    pipeline->shaderA = vertex;
			    pipeline->shaderB = fragment;
			    pipeline->colorFormat = desc.colorFormat;
			    pipeline->hasDepth = desc.depthTest;
			    pipeline->depthFormat = desc.depthFormat;
			    if (desc.layout)
			    {
				    pipeline->groupLayout = checked<LayoutImpl>(desc.layout, ctx, "descriptor layout");
			    }
			    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			    if (pipeline->groupLayout)
			    {
				    layoutInfo.setLayoutCount = 1;
				    layoutInfo.pSetLayouts = &pipeline->groupLayout->layout;
			    }
			    Check(vkCreatePipelineLayout(ctx->device, &layoutInfo, nullptr, &pipeline->layout),
			          "Creating graphics pipeline layout");
			    pipeline->renderPass =
			        MakeRenderPass(ctx->device, desc.colorFormat, desc.depthTest, desc.depthFormat, true, true);
			    VkPipelineShaderStageCreateInfo stages[2]{};
			    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			    stages[0].module = vertex->module;
			    stages[0].pName = vertex->entry.c_str();
			    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			    stages[1].module = fragment->module;
			    stages[1].pName = fragment->entry.c_str();
			    VkVertexInputBindingDescription binding{};
			    binding.binding = 0;
			    binding.stride = desc.vertexStride;
			    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			    Vector<VkVertexInputAttributeDescription> attributes;
			    for (const auto& a : desc.attributes)
			    {
				    Require(desc.vertexStride && a.offset < desc.vertexStride, "Vertex attribute exceeds stride");
				    VkFormat format = a.format == VertexFormat::float2   ? VK_FORMAT_R32G32_SFLOAT
				                      : a.format == VertexFormat::float3 ? VK_FORMAT_R32G32B32_SFLOAT
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
			    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
			    vertexInput.pVertexAttributeDescriptions = attributes.data();
			    VkPipelineInputAssemblyStateCreateInfo assembly{
			        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
			    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
			    viewport.viewportCount = viewport.scissorCount = 1;
			    VkPipelineRasterizationStateCreateInfo raster{
			        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
			    raster.polygonMode = VK_POLYGON_MODE_FILL;
			    raster.cullMode = desc.cullBack ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
			    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
			    raster.lineWidth = 1.0f;
			    VkPipelineMultisampleStateCreateInfo multisample{
			        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
			    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
			    depth.depthTestEnable = desc.depthTest;
			    depth.depthWriteEnable = desc.depthTest && desc.depthWrite;
			    switch (desc.depthCompare)
			    {
				    case Compare::never:
					    depth.depthCompareOp = VK_COMPARE_OP_NEVER;
					    break;
				    case Compare::less:
					    depth.depthCompareOp = VK_COMPARE_OP_LESS;
					    break;
				    case Compare::lessEqual:
					    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
					    break;
				    case Compare::equal:
					    depth.depthCompareOp = VK_COMPARE_OP_EQUAL;
					    break;
				    case Compare::greater:
					    depth.depthCompareOp = VK_COMPARE_OP_GREATER;
					    break;
				    case Compare::always:
					    depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
					    break;
			    }
			    VkPipelineColorBlendAttachmentState blend{};
			    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
			                           VK_COLOR_COMPONENT_A_BIT;
			    blend.blendEnable = desc.blend;
			    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			    blend.colorBlendOp = VK_BLEND_OP_ADD;
			    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			    blend.alphaBlendOp = VK_BLEND_OP_ADD;
			    VkPipelineColorBlendStateCreateInfo blendState{
			        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
			    blendState.attachmentCount = 1;
			    blendState.pAttachments = &blend;
			    VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
			    dynamic.dynamicStateCount = 2;
			    dynamic.pDynamicStates = dynamics;
			    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
			    info.stageCount = 2;
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
			    Require(shader->stage == ShaderStage::compute, "Compute pipeline requires a compute shader");
			    auto pipeline = std::make_shared<PipelineImpl>();
			    pipeline->ctx = ctx;
			    pipeline->shaderA = shader;
			    pipeline->compute = true;
			    if (desc.layout)
			    {
				    pipeline->groupLayout = checked<LayoutImpl>(desc.layout, ctx, "descriptor layout");
			    }
			    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			    if (pipeline->groupLayout)
			    {
				    layoutInfo.setLayoutCount = 1;
				    layoutInfo.pSetLayouts = &pipeline->groupLayout->layout;
			    }
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
			    Require(ctx->window && ctx->surface, "Device was created without a presentation window");
			    auto swapchain = std::make_shared<SwapchainImpl>();
			    swapchain->ctx = ctx;
			    swapchain->desc = desc;
			    if (ctx->window->FramebufferWidth() && ctx->window->FramebufferHeight())
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
				    Check(vkQueueSubmit(ctx->queue, 1, &info, fence->fence), "Submitting GPU work");
			    }
			    fence->submitted = true;
			    command->state = 3;
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
