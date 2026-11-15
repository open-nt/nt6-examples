// MIT License - see LICENSE
//
// A triangle rendered with hardware ray tracing (VK_KHR_ray_tracing_pipeline) in a QVulkanWindow. The renderer builds a
// bottom-level acceleration structure for one triangle and a top-level one for its instance, which turns each frame, a
// ray tracing pipeline with ray generation, miss and closest-hit shaders, and traces one ray per pixel into a storage
// image that is copied to the swapchain image. It needs a device with VK_KHR_acceleration_structure and
// VK_KHR_ray_tracing_pipeline: an NVIDIA RTX, AMD RDNA 2 or Intel Arc GPU, or Mesa's lavapipe in software. Without one
// the window shows the background color only, and says so.
//
//   rtriangle [--device INDEX] [--devices] [--frames N] [--no-vsync] [--validate]
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QString>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QVulkanWindow>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// A Vulkan structure with its sType set and the rest zero.
template <typename T> T make(VkStructureType type) {
    T t{};
    t.sType = type;
    return t;
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) { return (value + alignment - 1) & ~(alignment - 1); }

[[noreturn]] void fail(const QString &message) {
    std::fprintf(stderr, "rtriangle: %s\n", message.toUtf8().constData());
    std::exit(1);
}

void check(VkResult result, const char *what) {
    if (result != VK_SUCCESS)
        fail(QString("%1 failed: %2").arg(what).arg(static_cast<int>(result)));
}

template <typename F> void resolve(QVulkanFunctions *f, VkDevice dev, const char *name, F *out) {
    *out = reinterpret_cast<F>(f->vkGetDeviceProcAddr(dev, name));
    if (!*out)
        fail(QString("%1 is missing").arg(name));
}

// A buffer with its memory, and its device address when it was made with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
};

struct AccelerationStructure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    Buffer buffer;
    VkDeviceAddress address = 0;
};

} // namespace

class RayTracer : public QVulkanWindowRenderer {
public:
    explicit RayTracer(QVulkanWindow *window, int frameLimit) : m_window(window), m_frameLimit(frameLimit) {}

    // Before the device is made: ask for the ray tracing extensions and their features.
    void preInitResources() override {
        const QVulkanInfoVector<QVulkanExtension> extensions = m_window->supportedDeviceExtensions();
        m_supported = extensions.contains("VK_KHR_acceleration_structure") &&
                      extensions.contains("VK_KHR_ray_tracing_pipeline") &&
                      extensions.contains("VK_KHR_deferred_host_operations");
        if (!m_supported) {
            std::puts("rtriangle: the device has no ray tracing extensions; showing the background only");
            return;
        }
        m_window->setDeviceExtensions(
            {"VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline", "VK_KHR_deferred_host_operations"});
        // The features of the extensions live in their own structures, chained to VkPhysicalDeviceFeatures2; the
        // structures must live until the device is created, so they are members.
        m_window->setEnabledFeaturesModifier([this](VkPhysicalDeviceFeatures2 &features) {
            m_addressFeatures = make<VkPhysicalDeviceBufferDeviceAddressFeatures>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
            m_addressFeatures.bufferDeviceAddress = VK_TRUE;
            m_asFeatures = make<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
            m_asFeatures.accelerationStructure = VK_TRUE;
            m_asFeatures.pNext = &m_addressFeatures;
            m_rtFeatures = make<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
            m_rtFeatures.rayTracingPipeline = VK_TRUE;
            m_rtFeatures.pNext = &m_asFeatures;
            features.pNext = &m_rtFeatures;
        });
    }

    void initResources() override {
        m_clock.start();
        if (!m_supported)
            return;
        m_dev = m_window->device();
        QVulkanInstance *inst = m_window->vulkanInstance();
        QVulkanFunctions *f = inst->functions();
        m_df = inst->deviceFunctions(m_dev);
        f->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &m_memProps);

        // The functions of the extensions are not in QVulkanDeviceFunctions: resolve them.
        resolve(f, m_dev, "vkCreateAccelerationStructureKHR", &m_createAS);
        resolve(f, m_dev, "vkDestroyAccelerationStructureKHR", &m_destroyAS);
        resolve(f, m_dev, "vkGetAccelerationStructureBuildSizesKHR", &m_getBuildSizes);
        resolve(f, m_dev, "vkGetAccelerationStructureDeviceAddressKHR", &m_getASAddress);
        resolve(f, m_dev, "vkCmdBuildAccelerationStructuresKHR", &m_cmdBuildAS);
        resolve(f, m_dev, "vkCreateRayTracingPipelinesKHR", &m_createPipelines);
        resolve(f, m_dev, "vkGetRayTracingShaderGroupHandlesKHR", &m_getGroupHandles);
        resolve(f, m_dev, "vkCmdTraceRaysKHR", &m_cmdTraceRays);

        // The sizes and alignments of shader group handles and of scratch memory.
        m_rtProps = make<VkPhysicalDeviceRayTracingPipelinePropertiesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
        m_asProps = make<VkPhysicalDeviceAccelerationStructurePropertiesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
        m_rtProps.pNext = &m_asProps;
        auto props = make<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        props.pNext = &m_rtProps;
        f->vkGetPhysicalDeviceProperties2(m_window->physicalDevice(), &props);

        createGeometry();
        createTopLevel();
        createPipeline();
        createShaderBindingTable();
        createDescriptors();
    }

    void initSwapChainResources() override {
        if (!m_supported)
            return;
        // The storage image the rays write; copied to the swapchain image, so it has the same size and byte layout.
        const QSize size = m_window->swapChainImageSize();
        auto info = make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()), 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(m_df->vkCreateImage(m_dev, &info, nullptr, &m_image), "vkCreateImage");
        VkMemoryRequirements req{};
        m_df->vkGetImageMemoryRequirements(m_dev, m_image, &req);
        auto alloc = make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(m_df->vkAllocateMemory(m_dev, &alloc, nullptr, &m_imageMemory), "vkAllocateMemory");
        m_df->vkBindImageMemory(m_dev, m_image, m_imageMemory, 0);
        auto view = make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = m_image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = info.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(m_df->vkCreateImageView(m_dev, &view, nullptr, &m_imageView), "vkCreateImageView");
        // Swapchains are usually BGRA; the shader then writes the channels swapped, so the copy needs no conversion.
        const VkFormat color = m_window->colorFormat();
        m_swapRedBlue = color == VK_FORMAT_B8G8R8A8_UNORM || color == VK_FORMAT_B8G8R8A8_SRGB ? 1u : 0u;

        // The descriptors: the scene and the image.
        auto asWrite = make<VkWriteDescriptorSetAccelerationStructureKHR>(
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
        asWrite.accelerationStructureCount = 1;
        asWrite.pAccelerationStructures = &m_tlas.handle;
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = m_imageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet writes[2] = {make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET),
                                          make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET)};
        writes[0].pNext = &asWrite;
        writes[0].dstSet = m_descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        writes[1].dstSet = m_descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &imageInfo;
        m_df->vkUpdateDescriptorSets(m_dev, 2, writes, 0, nullptr);
    }

    void releaseSwapChainResources() override {
        if (!m_supported)
            return;
        m_df->vkDestroyImageView(m_dev, m_imageView, nullptr);
        m_df->vkDestroyImage(m_dev, m_image, nullptr);
        m_df->vkFreeMemory(m_dev, m_imageMemory, nullptr);
        m_imageView = VK_NULL_HANDLE;
        m_image = VK_NULL_HANDLE;
        m_imageMemory = VK_NULL_HANDLE;
    }

    void releaseResources() override {
        if (!m_supported || !m_dev)
            return;
        m_df->vkDestroyDescriptorPool(m_dev, m_descriptorPool, nullptr);
        m_df->vkDestroyPipeline(m_dev, m_pipeline, nullptr);
        m_df->vkDestroyPipelineLayout(m_dev, m_pipelineLayout, nullptr);
        m_df->vkDestroyDescriptorSetLayout(m_dev, m_descriptorSetLayout, nullptr);
        destroyBuffer(m_sbt);
        destroyAccelerationStructure(m_tlas);
        destroyBuffer(m_tlasScratch);
        for (Buffer &b : m_instances)
            destroyBuffer(b);
        m_instances.clear();
        destroyAccelerationStructure(m_blas);
        destroyBuffer(m_vertices);
        m_dev = VK_NULL_HANDLE;
    }

    void startNextFrame() override {
        VkCommandBuffer cmd = m_window->currentCommandBuffer();
        const QSize size = m_window->swapChainImageSize();
        const auto width = static_cast<uint32_t>(size.width());
        const auto height = static_cast<uint32_t>(size.height());
        VkImage target = m_window->swapChainImage(m_window->currentSwapChainImageIndex());

        if (!m_supported) {
            // No ray tracing: the default render pass clears the window to the background color.
            VkClearValue clearValues[2]{};
            clearValues[0].color = {{0.1f, 0.12f, 0.2f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};
            auto pass = make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
            pass.renderPass = m_window->defaultRenderPass();
            pass.framebuffer = m_window->currentFramebuffer();
            pass.renderArea.extent = {width, height};
            pass.clearValueCount = 2;
            pass.pClearValues = clearValues;
            m_df->vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
            m_df->vkCmdEndRenderPass(cmd);
        } else {
            // The triangle turns: write its transform into this frame's instance buffer and rebuild the top level.
            updateInstance(m_window->currentFrame());
            buildTopLevel(cmd, m_window->currentFrame());

            // Trace one ray per pixel into the storage image.
            imageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
            m_df->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
            m_df->vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1,
                                          &m_descriptorSet, 0, nullptr);
            m_df->vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(uint32_t),
                                     &m_swapRedBlue);
            m_cmdTraceRays(cmd, &m_rgenRegion, &m_missRegion, &m_hitRegion, &m_callableRegion, width, height, 1);

            // Copy it to the swapchain image, which must end in the present layout.
            imageBarrier(cmd, m_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT);
            imageBarrier(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                         VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = copy.srcSubresource;
            copy.extent = {width, height, 1};
            m_df->vkCmdCopyImage(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            imageBarrier(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        }

        m_window->frameReady();
        m_window->requestUpdate(); // render continuously, at the display's rate with vsync
        countFrame();
    }

private:
    uint32_t memoryType(uint32_t typeBits, VkMemoryPropertyFlags wanted) const {
        for (uint32_t i = 0; i < m_memProps.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (m_memProps.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
        }
        fail("no suitable memory type");
    }

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
        Buffer b;
        auto info = make<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        info.size = size;
        info.usage = usage;
        check(m_df->vkCreateBuffer(m_dev, &info, nullptr, &b.buffer), "vkCreateBuffer");
        VkMemoryRequirements req{};
        m_df->vkGetBufferMemoryRequirements(m_dev, b.buffer, &req);
        auto flags = make<VkMemoryAllocateFlagsInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        auto alloc = make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, properties);
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            alloc.pNext = &flags;
        check(m_df->vkAllocateMemory(m_dev, &alloc, nullptr, &b.memory), "vkAllocateMemory");
        m_df->vkBindBufferMemory(m_dev, b.buffer, b.memory, 0);
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            auto addressInfo = make<VkBufferDeviceAddressInfo>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
            addressInfo.buffer = b.buffer;
            b.address = m_df->vkGetBufferDeviceAddress(m_dev, &addressInfo);
        }
        return b;
    }

    void destroyBuffer(Buffer &b) {
        if (b.buffer)
            m_df->vkDestroyBuffer(m_dev, b.buffer, nullptr);
        if (b.memory)
            m_df->vkFreeMemory(m_dev, b.memory, nullptr);
        b = Buffer();
    }

    void upload(const Buffer &b, const void *data, std::size_t size) {
        void *mapped = nullptr;
        check(m_df->vkMapMemory(m_dev, b.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
        std::memcpy(mapped, data, size);
        m_df->vkUnmapMemory(m_dev, b.memory);
    }

    AccelerationStructure createAccelerationStructure(VkAccelerationStructureTypeKHR type, VkDeviceSize size) {
        AccelerationStructure as;
        as.buffer = createBuffer(
            size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto info =
            make<VkAccelerationStructureCreateInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
        info.buffer = as.buffer.buffer;
        info.size = size;
        info.type = type;
        check(m_createAS(m_dev, &info, nullptr, &as.handle), "vkCreateAccelerationStructureKHR");
        auto addressInfo = make<VkAccelerationStructureDeviceAddressInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
        addressInfo.accelerationStructure = as.handle;
        as.address = m_getASAddress(m_dev, &addressInfo);
        return as;
    }

    void destroyAccelerationStructure(AccelerationStructure &as) {
        if (as.handle)
            m_destroyAS(m_dev, as.handle, nullptr);
        destroyBuffer(as.buffer);
        as = AccelerationStructure();
    }

    // Runs `record` in a command buffer and waits for it.
    template <typename Record> void submitAndWait(Record record) {
        auto allocInfo = make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocInfo.commandPool = m_window->graphicsCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(m_df->vkAllocateCommandBuffers(m_dev, &allocInfo, &cmd), "vkAllocateCommandBuffers");
        auto begin = make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        m_df->vkBeginCommandBuffer(cmd, &begin);
        record(cmd);
        m_df->vkEndCommandBuffer(cmd);
        auto submit = make<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        check(m_df->vkQueueSubmit(m_window->graphicsQueue(), 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        m_df->vkQueueWaitIdle(m_window->graphicsQueue());
        m_df->vkFreeCommandBuffers(m_dev, m_window->graphicsCommandPool(), 1, &cmd);
    }

    // The triangle and its bottom-level acceleration structure, built once.
    void createGeometry() {
        const float vertices[] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        m_vertices = createBuffer(sizeof(vertices),
                                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        upload(m_vertices, vertices, sizeof(vertices));

        auto geometry = make<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles = make<VkAccelerationStructureGeometryTrianglesDataKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR);
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress = m_vertices.address;
        geometry.geometry.triangles.vertexStride = 3 * sizeof(float);
        geometry.geometry.triangles.maxVertex = 2;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

        auto build = make<VkAccelerationStructureBuildGeometryInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        const uint32_t primitiveCount = 1;
        auto sizes = make<VkAccelerationStructureBuildSizesInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        m_getBuildSizes(m_dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitiveCount, &sizes);

        m_blas = createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                                             sizes.accelerationStructureSize);
        Buffer scratch = createScratch(sizes.buildScratchSize);
        build.dstAccelerationStructure = m_blas.handle;
        build.scratchData.deviceAddress = scratchAddress(scratch);
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
        submitAndWait([&](VkCommandBuffer cmd) { m_cmdBuildAS(cmd, 1, &build, &ranges); });
        destroyBuffer(scratch);
    }

    Buffer createScratch(VkDeviceSize size) {
        return createBuffer(size + m_asProps.minAccelerationStructureScratchOffsetAlignment,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    VkDeviceAddress scratchAddress(const Buffer &scratch) const {
        return alignUp(scratch.address, m_asProps.minAccelerationStructureScratchOffsetAlignment);
    }

    // The instance buffers, one per frame in flight so that a frame being traced keeps its own, and the top-level
    // acceleration structure, rebuilt each frame.
    void createTopLevel() {
        m_instances.resize(static_cast<std::size_t>(m_window->concurrentFrameCount()));
        for (Buffer &b : m_instances) {
            b = createBuffer(sizeof(VkAccelerationStructureInstanceKHR),
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
        updateInstance(0);
        VkAccelerationStructureGeometryKHR geometry = instanceGeometry(0);
        auto build = topLevelBuildInfo(&geometry);
        const uint32_t instanceCount = 1;
        auto sizes = make<VkAccelerationStructureBuildSizesInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        m_getBuildSizes(m_dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &instanceCount, &sizes);
        m_tlas =
            createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes.accelerationStructureSize);
        m_tlasScratch = createScratch(sizes.buildScratchSize);
    }

    VkAccelerationStructureGeometryKHR instanceGeometry(int frame) const {
        auto geometry = make<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = make<VkAccelerationStructureGeometryInstancesDataKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR);
        geometry.geometry.instances.data.deviceAddress = m_instances[static_cast<std::size_t>(frame)].address;
        return geometry;
    }

    VkAccelerationStructureBuildGeometryInfoKHR
    topLevelBuildInfo(const VkAccelerationStructureGeometryKHR *geometry) const {
        auto build = make<VkAccelerationStructureBuildGeometryInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.geometryCount = 1;
        build.pGeometries = geometry;
        return build;
    }

    // Writes the instance of the triangle, turned about the vertical axis by the time, into a frame's buffer.
    void updateInstance(int frame) {
        const float angle = static_cast<float>(m_clock.elapsed()) * 0.06f;
        QMatrix4x4 transform;
        transform.rotate(angle, 0.0f, 1.0f, 0.0f);
        VkAccelerationStructureInstanceKHR instance{};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                instance.transform.matrix[row][column] = transform(row, column);
        instance.mask = 0xff;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = m_blas.address;
        upload(m_instances[static_cast<std::size_t>(frame)], &instance, sizeof(instance));
    }

    void buildTopLevel(VkCommandBuffer cmd, int frame) {
        // The previous frame may still be tracing against the top level: wait for it before writing.
        auto barrier = make<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        m_df->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                   VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr,
                                   0, nullptr);
        VkAccelerationStructureGeometryKHR geometry = instanceGeometry(frame);
        auto build = topLevelBuildInfo(&geometry);
        build.dstAccelerationStructure = m_tlas.handle;
        build.scratchData.deviceAddress = scratchAddress(m_tlasScratch);
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = 1;
        const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
        m_cmdBuildAS(cmd, 1, &build, &ranges);
        // And the build must finish before the rays read it.
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        m_df->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                   VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0,
                                   nullptr);
    }

    VkShaderModule loadShader(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            fail(QString("cannot open %1").arg(path));
        const QByteArray code = file.readAll();
        auto info = make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        info.codeSize = static_cast<std::size_t>(code.size());
        info.pCode = reinterpret_cast<const uint32_t *>(code.constData());
        VkShaderModule module = VK_NULL_HANDLE;
        check(m_df->vkCreateShaderModule(m_dev, &info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    // The pipeline: ray generation, miss and closest hit, in three shader groups.
    void createPipeline() {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        auto layoutInfo = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        check(m_df->vkCreateDescriptorSetLayout(m_dev, &layoutInfo, nullptr, &m_descriptorSetLayout),
              "vkCreateDescriptorSetLayout");

        VkPushConstantRange pushConstants{VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(uint32_t)};
        auto pipelineLayoutInfo = make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstants;
        check(m_df->vkCreatePipelineLayout(m_dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout),
              "vkCreatePipelineLayout");

        const VkShaderStageFlagBits stageBits[3] = {VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
                                                    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
        const char *const files[3] = {":/shaders/rtriangle.rgen.spv", ":/shaders/rtriangle.rmiss.spv",
                                      ":/shaders/rtriangle.rchit.spv"};
        VkPipelineShaderStageCreateInfo stages[3];
        VkRayTracingShaderGroupCreateInfoKHR groups[3];
        for (int i = 0; i < 3; ++i) {
            stages[i] = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stages[i].stage = stageBits[i];
            stages[i].module = loadShader(QString::fromLatin1(files[i]));
            stages[i].pName = "main";
            groups[i] =
                make<VkRayTracingShaderGroupCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
            groups[i].generalShader = VK_SHADER_UNUSED_KHR;
            groups[i].closestHitShader = VK_SHADER_UNUSED_KHR;
            groups[i].anyHitShader = VK_SHADER_UNUSED_KHR;
            groups[i].intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0;
        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].closestHitShader = 2;

        auto info = make<VkRayTracingPipelineCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
        info.stageCount = 3;
        info.pStages = stages;
        info.groupCount = 3;
        info.pGroups = groups;
        info.maxPipelineRayRecursionDepth = 1;
        info.layout = m_pipelineLayout;
        check(m_createPipelines(m_dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline),
              "vkCreateRayTracingPipelinesKHR");
        for (const VkPipelineShaderStageCreateInfo &stage : stages)
            m_df->vkDestroyShaderModule(m_dev, stage.module, nullptr);
    }

    // The shader binding table: the handle of each group, in its own region.
    void createShaderBindingTable() {
        const uint32_t handleSize = m_rtProps.shaderGroupHandleSize;
        const VkDeviceSize handleStride = alignUp(handleSize, m_rtProps.shaderGroupHandleAlignment);
        const VkDeviceSize regionSize = alignUp(handleStride, m_rtProps.shaderGroupBaseAlignment);
        std::vector<uint8_t> handles(static_cast<std::size_t>(3 * handleSize));
        check(m_getGroupHandles(m_dev, m_pipeline, 0, 3, handles.size(), handles.data()),
              "vkGetRayTracingShaderGroupHandlesKHR");
        m_sbt = createBuffer(3 * regionSize,
                             VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void *mapped = nullptr;
        check(m_df->vkMapMemory(m_dev, m_sbt.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
        for (std::size_t group = 0; group < 3; ++group)
            std::memcpy(static_cast<uint8_t *>(mapped) + group * regionSize, handles.data() + group * handleSize,
                        handleSize);
        m_df->vkUnmapMemory(m_dev, m_sbt.memory);
        m_rgenRegion = {m_sbt.address, regionSize, regionSize};
        m_missRegion = {m_sbt.address + regionSize, handleStride, regionSize};
        m_hitRegion = {m_sbt.address + 2 * regionSize, handleStride, regionSize};
        m_callableRegion = {};
    }

    void createDescriptors() {
        VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        auto poolInfo = make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = sizes;
        check(m_df->vkCreateDescriptorPool(m_dev, &poolInfo, nullptr, &m_descriptorPool), "vkCreateDescriptorPool");
        auto allocInfo = make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;
        check(m_df->vkAllocateDescriptorSets(m_dev, &allocInfo, &m_descriptorSet), "vkAllocateDescriptorSets");
    }

    void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                      VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        auto barrier = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        m_df->vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void countFrame() {
        ++m_frames;
        ++m_framesThisSecond;
        if (m_fpsClock.isValid() && m_fpsClock.elapsed() >= 1000) {
            std::printf("%d frames/s\n", m_framesThisSecond);
            m_framesThisSecond = 0;
            m_fpsClock.restart();
        } else if (!m_fpsClock.isValid()) {
            m_fpsClock.start();
        }
        if (m_frameLimit > 0 && m_frames >= m_frameLimit)
            QCoreApplication::quit();
    }

    QVulkanWindow *m_window;
    int m_frameLimit;
    bool m_supported = false;
    VkDevice m_dev = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_df = nullptr;
    VkPhysicalDeviceMemoryProperties m_memProps{};
    VkPhysicalDeviceBufferDeviceAddressFeatures m_addressFeatures{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_asFeatures{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_rtFeatures{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProps{};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProps{};

    PFN_vkCreateAccelerationStructureKHR m_createAS = nullptr;
    PFN_vkDestroyAccelerationStructureKHR m_destroyAS = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR m_getBuildSizes = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_getASAddress = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR m_cmdBuildAS = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR m_createPipelines = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR m_getGroupHandles = nullptr;
    PFN_vkCmdTraceRaysKHR m_cmdTraceRays = nullptr;

    Buffer m_vertices;
    AccelerationStructure m_blas;
    std::vector<Buffer> m_instances;
    AccelerationStructure m_tlas;
    Buffer m_tlasScratch;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    Buffer m_sbt;
    VkStridedDeviceAddressRegionKHR m_rgenRegion{};
    VkStridedDeviceAddressRegionKHR m_missRegion{};
    VkStridedDeviceAddressRegionKHR m_hitRegion{};
    VkStridedDeviceAddressRegionKHR m_callableRegion{};
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    uint32_t m_swapRedBlue = 0;

    QElapsedTimer m_clock;
    QElapsedTimer m_fpsClock;
    int m_frames = 0;
    int m_framesThisSecond = 0;
};

class Window : public QVulkanWindow {
public:
    explicit Window(int frameLimit) : m_frameLimit(frameLimit) {}
    QVulkanWindowRenderer *createRenderer() override { return new RayTracer(this, m_frameLimit); }

private:
    int m_frameLimit;
};

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    int deviceIndex = -1;
    int frameLimit = 0;
    bool vsync = true;
    bool validate = false;
    bool listDevices = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc)
            deviceIndex = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            frameLimit = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--no-vsync") == 0)
            vsync = false;
        else if (std::strcmp(argv[i], "--validate") == 0)
            validate = true;
        else if (std::strcmp(argv[i], "--devices") == 0)
            listDevices = true;
    }

    QVulkanInstance inst;
    inst.setApiVersion(QVersionNumber(1, 2)); // buffer device addresses and SPIR-V 1.4 are core from 1.2
    if (validate)
        inst.setLayers({"VK_LAYER_KHRONOS_validation"});
    if (!inst.create()) {
        std::fprintf(stderr, "rtriangle: no Vulkan (error %d)\n", static_cast<int>(inst.errorCode()));
        return 1;
    }

    Window window(frameLimit);
    window.setVulkanInstance(&inst);
    if (listDevices) {
        int index = 0;
        for (const VkPhysicalDeviceProperties &props : window.availablePhysicalDevices())
            std::printf("%d: %s (Vulkan %u.%u)\n", index++, props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
                        VK_API_VERSION_MINOR(props.apiVersion));
        return 0;
    }
    if (deviceIndex >= 0)
        window.setPhysicalDeviceIndex(deviceIndex);
    QSurfaceFormat format;
    format.setSwapInterval(vsync ? 1 : 0);
    window.setFormat(format);
    window.setTitle("Nt ray tracing");
    window.resize(800, 600);
    window.show();
    return app.exec();
}
