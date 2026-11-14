// MIT License - see LICENSE
//
// A progressive path tracer on the Vulkan ray tracing pipeline, in a QVulkanWindow. The scene is a checkered floor,
// boxes and a field of spheres with diffuse, metal, glass and emissive materials, lit by a sun and the sky. Each frame
// traces a few paths per pixel (as many as keep the frame rate up) with up to `bounces` bounces, sampling the lights
// directly with shadow rays at diffuse surfaces; while the camera rests and the scene stands still, the frames
// accumulate and the image converges. Beyond the core ray tracing extensions it uses what an NVIDIA RTX GPU
// adds: shader execution reordering (VK_NV_ray_tracing_invocation_reorder, toggled with R) and ray tracing position
// fetch (VK_KHR_ray_tracing_position_fetch, so the meshes bind no vertex buffers); the spheres are analytic, from an
// intersection shader. It runs on any GPU with VK_KHR_ray_tracing_pipeline, and on Mesa's lavapipe in software.
//
//   pathtracer [--device INDEX] [--devices] [--frames N] [--vsync] [--spp N] [--history N] [--candidates N]
//              [--scale F] [--no-denoise] [--no-reorder] [--dlss] [--dlss-mode performance|balanced|quality|dlaa]
//              [--still] [--fly] [--validate] [--size WxH]
//              [--screenshot FILE.png] [--record DIR] [--seconds S] [--fps N] [--passes N]
//
// --dlss (or the N key) hands the frame to DLSS Ray Reconstruction instead of the history and the filter, on an
// NVIDIA RTX GPU, when the example was built against the DLSS SDK: one path per pixel at the size DLSS asks for, with
// its guides (albedos, normals, roughness, depth, motion vectors) and one sub-pixel jitter per frame; it denoises and
// upscales to the window.
//
// --record renders a fly-through one frame at a time, offline: the scene and the camera step by the video's frame
// time, each frame converges over --passes renderings (8, times --spp paths per pixel, 16 by default) before it is saved
// as DIR/frame_NNNNN.png, and ffmpeg, if installed, encodes DIR/flythrough.mp4.
//
//   Drag: orbit   Wheel: zoom   Space: pause/resume motion   F: fly around   R: reordering   D: denoiser
//   N: DLSS ray reconstruction   +/-: bounces   S: screenshot   Q: quit
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QVulkanWindow>
#include <QtGui/qevent.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef HAVE_DLSS
// NVIDIA's DLSS SDK (proprietary): built in with -DDLSS_SDK=<checkout of github.com/NVIDIA/DLSS>.
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h> // before the ray reconstruction helpers, which use its types
#include <nvsdk_ngx_helpers_dlssd_vk.h>
#endif

namespace {

template <typename T> T make(VkStructureType type) {
    T t{};
    t.sType = type;
    return t;
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) { return (value + alignment - 1) & ~(alignment - 1); }

[[noreturn]] void fail(const QString &message) {
    std::fprintf(stderr, "pathtracer: %s\n", message.toUtf8().constData());
    std::exit(1);
}

void check(VkResult result, const char *what) {
    if (result != VK_SUCCESS)
        fail(QString("%1 failed: %2").arg(what).arg(static_cast<int>(result)));
}

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    void *mapped = nullptr;
};

struct AccelerationStructure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    Buffer buffer;
    VkDeviceAddress address = 0;
};

// The material of an instance, as the shaders read it (std430): albedo, then roughness, index of refraction,
// emission and type.
struct Material {
    float albedo[4];
    float params[4];
};
enum MaterialType { Diffuse = 0, Metal = 1, Glass = 2, Emissive = 3, Checker = 4 };

Material material(MaterialType type, float r, float g, float b, float roughness = 0.0f, float ior = 1.5f,
                  float emission = 0.0f) {
    return Material{{r, g, b, 1.0f}, {roughness, ior, emission, static_cast<float>(type)}};
}

// One object of the scene: which geometry (0 triangles of the floor, 1 the box, 2 the sphere), its material, and
// where it is. Spheres and boxes may bob and turn.
struct Object {
    int geometry;
    Material material;
    QVector3D position;
    QVector3D scale;
    float bob = 0.0f;  // amplitude of the vertical motion
    float phase = 0.0f;
    float spin = 0.0f; // degrees per second about the vertical axis
};

// The camera of this frame and of the previous one, and the frame's parameters, as the shaders read them from a
// uniform buffer (std140: the vec4s first, then the scalars).
struct FrameConstants {
    float position[4];
    float forward[4];
    float right[4];
    float up[4];
    float previousPosition[4];
    float previousForward[4];
    float previousRight[4];
    float previousUp[4];
    uint32_t frame;
    uint32_t samples;
    uint32_t bounces;
    uint32_t flags;
    uint32_t lightCount;
    uint32_t maxHistory;
    uint32_t candidates;
    uint32_t denoise;
    uint32_t dlss;
    uint32_t padding[3];
    float jitter[4];
};

// The push constants of the compute passes: the à-trous step and which images to read and write.
struct PassConstants {
    int32_t step;
    int32_t source;
    int32_t target;
};

// A sphere light, as the shaders read it: position and radius, then the emitted radiance.
struct Light {
    float positionRadius[4];
    float emission[4];
};

const char *const kRayTracingExtensions[] = {"VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline",
                                             "VK_KHR_deferred_host_operations",
                                             "VK_KHR_ray_tracing_position_fetch"};
const char *const kReorderExtension = "VK_NV_ray_tracing_invocation_reorder";

} // namespace

class PathTracer;

class Window : public QVulkanWindow {
public:
    explicit Window(int frameLimit) : m_frameLimit(frameLimit) {}
    QVulkanWindowRenderer *createRenderer() override;

    // What the renderer reads each frame.
    float yaw = 150.0f;
    float pitch = 16.0f;
    float distance = 15.0f;
    bool animating = true;
    bool flying = false; // the camera circles the scene by itself
    bool reorder = true;
    uint32_t bounces = 6;
    uint32_t samples = 0; // paths per pixel per frame; 0 picks as many as keep the frame rate up
    uint32_t history = 48; // previous samples a pixel keeps while things move; at rest it keeps them all
    bool denoise = true;
    bool dlss = false; // DLSS Ray Reconstruction, where the build and the GPU have it
    int dlssMode = 1;  // 0 performance, 1 balanced, 2 quality, 3 DLAA (native resolution, denoise only)
    uint32_t candidates = 8; // light candidates resampled per diffuse hit
    float scale = 0.0f; // the rendered size over the window's; 0 keeps the rendering near 1080p and upscales
    float fixedTime = -1.0f; // recording: the scene's time, instead of the clock
    bool resetHistory = false; // recording: the next frame starts a new accumulation
    bool cameraMoved = true;
    int frameLimit() const { return m_frameLimit; }

protected:
    void mousePressEvent(QMouseEvent *event) override { m_last = event->pos(); }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        flying = false; // the user takes the camera
        const QPoint delta = event->pos() - m_last;
        m_last = event->pos();
        yaw += static_cast<float>(delta.x()) * 0.3f;
        pitch = std::fmin(std::fmax(pitch - static_cast<float>(delta.y()) * 0.3f, -5.0f), 85.0f);
        cameraMoved = true;
    }
    void wheelEvent(QWheelEvent *event) override {
        distance = std::fmin(std::fmax(distance * (1.0f - static_cast<float>(event->angleDelta().y()) / 1200.0f), 3.0f),
                             60.0f);
        cameraMoved = true;
    }
    void keyPressEvent(QKeyEvent *event) override {
        switch (event->key()) {
        case Qt::Key_Space:
            animating = !animating;
            cameraMoved = true;
            break;
        case Qt::Key_F:
            flying = !flying;
            break;
        case Qt::Key_R:
            reorder = !reorder;
            break;
        case Qt::Key_D:
            denoise = !denoise;
            break;
        case Qt::Key_N:
            dlss = !dlss;
            break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: // the + key without shift
            bounces = std::min(bounces + 1, 16u);
            cameraMoved = true;
            break;
        case Qt::Key_Minus:
            bounces = std::max(bounces - 1, 1u);
            cameraMoved = true;
            break;
        case Qt::Key_S: { // grab() renders a frame of its own and reads it back
            const QString name = QString("pathtracer-%1.png").arg(++m_screenshots);
            const QImage image = grab();
            if (!image.isNull() && image.save(name))
                std::printf("pathtracer: saved %s\n", name.toUtf8().constData());
            else
                std::puts("pathtracer: could not save a screenshot");
            break;
        }
        case Qt::Key_Q:
        case Qt::Key_Escape:
            QCoreApplication::quit();
            break;
        default:
            break;
        }
    }

private:
    int m_frameLimit;
    int m_screenshots = 0;
    QPoint m_last;
};

// Element `index` of the Halton sequence in `base`, in [0, 1): the sub-pixel jitter pattern DLSS is trained on.
static float halton(uint32_t index, uint32_t base) {
    float f = 1.0f, result = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        result += f * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

// The pitch and distance of the flight at `time`; the yaw advances at 12 degrees a second.
static void flyPath(float time, float *pitch, float *distance) {
    *pitch = 14.0f + 10.0f * std::sin(time * 0.37f);
    *distance = 14.0f + 5.0f * std::sin(time * 0.23f);
}

class PathTracer : public QVulkanWindowRenderer {
public:
    explicit PathTracer(Window *window) : m_window(window) {}

    // Before the device is made: the ray tracing extensions and their features, and NVIDIA's reordering if the GPU has it.
    void preInitResources() override {
        const QVulkanInfoVector<QVulkanExtension> extensions = m_window->supportedDeviceExtensions();
        QByteArrayList wanted;
        for (const char *name : kRayTracingExtensions) {
            if (!extensions.contains(name))
                fail(QString("the device has no %1; pick another with --device (see --devices)").arg(name));
            wanted.append(name);
        }
        m_hasReorder = extensions.contains(kReorderExtension);
        if (m_hasReorder)
            wanted.append(kReorderExtension);
#ifdef HAVE_DLSS
        // NGX runs its networks through extensions of NVIDIA's driver (binary import, image view handles, push
        // descriptors); without them on the device, ray reconstruction reports itself unavailable.
        if (m_window->dlss) {
            unsigned int instanceCount = 0, deviceCount = 0;
            const char **instanceExts = nullptr, **deviceExts = nullptr;
            if (NVSDK_NGX_SUCCEED(
                    NVSDK_NGX_VULKAN_RequiredExtensions(&instanceCount, &instanceExts, &deviceCount, &deviceExts))) {
                for (unsigned int i = 0; i < deviceCount; ++i) {
                    if (extensions.contains(deviceExts[i]) && !wanted.contains(deviceExts[i]))
                        wanted.append(deviceExts[i]);
                    else if (!extensions.contains(deviceExts[i]))
                        std::printf("pathtracer: the device lacks %s, which DLSS needs\n", deviceExts[i]);
                }
            }
        }
#endif
        else
            std::puts("pathtracer: no VK_NV_ray_tracing_invocation_reorder on this device; R does nothing");
        m_window->setDeviceExtensions(wanted);
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
            m_fetchFeatures = make<VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR);
            m_fetchFeatures.rayTracingPositionFetch = VK_TRUE;
            m_fetchFeatures.pNext = &m_rtFeatures;
            m_reorderFeatures = make<VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV);
            m_reorderFeatures.rayTracingInvocationReorder = VK_TRUE;
            m_reorderFeatures.pNext = &m_fetchFeatures;
            features.pNext = m_hasReorder ? static_cast<void *>(&m_reorderFeatures) : static_cast<void *>(&m_fetchFeatures);
        });
    }

    void initResources() override {
        m_dev = m_window->device();
        QVulkanInstance *inst = m_window->vulkanInstance();
        QVulkanFunctions *f = inst->functions();
        m_df = inst->deviceFunctions(m_dev);
        f->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &m_memProps);

        resolve(f, "vkCreateAccelerationStructureKHR", &m_createAS);
        resolve(f, "vkDestroyAccelerationStructureKHR", &m_destroyAS);
        resolve(f, "vkGetAccelerationStructureBuildSizesKHR", &m_getBuildSizes);
        resolve(f, "vkGetAccelerationStructureDeviceAddressKHR", &m_getASAddress);
        resolve(f, "vkCmdBuildAccelerationStructuresKHR", &m_cmdBuildAS);
        resolve(f, "vkCreateRayTracingPipelinesKHR", &m_createPipelines);
        resolve(f, "vkGetRayTracingShaderGroupHandlesKHR", &m_getGroupHandles);
        resolve(f, "vkCmdTraceRaysKHR", &m_cmdTraceRays);

        m_rtProps = make<VkPhysicalDeviceRayTracingPipelinePropertiesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
        m_asProps = make<VkPhysicalDeviceAccelerationStructurePropertiesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
        m_rtProps.pNext = &m_asProps;
        auto props = make<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        props.pNext = &m_rtProps;
        f->vkGetPhysicalDeviceProperties2(m_window->physicalDevice(), &props);
        m_deviceName = QString::fromUtf8(props.properties.deviceName);
        std::printf("pathtracer: %s, ray recursion depth %u, reordering %s\n", props.properties.deviceName,
                    m_rtProps.maxRayRecursionDepth, m_hasReorder ? "available" : "not available");

#ifdef HAVE_DLSS
        initNgx();
#endif
        buildScene();
        createGeometry();
        createMaterials();
        createTopLevel();
        createDescriptorLayout();
        m_pipeline = createPipeline(":/shaders/pathtracer.rgen.spv", m_sbt);
        if (m_hasReorder)
            m_reorderPipeline = createPipeline(":/shaders/pathtracer_ser.rgen.spv", m_reorderSbt);
        createComputePipelines();
        createDescriptors();
        m_clock.start();
        m_statsClock.start();
    }

    void initSwapChainResources() override {
        // The rendering happens at a fraction of the window's size when it is large, and is upscaled at the end,
        // which is where DLSS sits in an RTX pipeline; with ray reconstruction, DLSS says what size it wants.
        const QSize windowSize = m_window->swapChainImageSize();
        m_dlssActive = false;
        float scale = m_window->scale;
        if (scale <= 0.0f) {
            const float pixels = static_cast<float>(windowSize.width()) * static_cast<float>(windowSize.height());
            scale = std::min(1.0f, std::sqrt(2.1e6f / pixels));
        }
        m_renderSize = QSize(std::max(1, static_cast<int>(std::lround(static_cast<float>(windowSize.width()) * scale))),
                             std::max(1, static_cast<int>(std::lround(static_cast<float>(windowSize.height()) * scale))));
#ifdef HAVE_DLSS
        if (m_window->dlss && m_rrAvailable) {
            unsigned int optimalWidth = 0, optimalHeight = 0, maxWidth = 0, maxHeight = 0, minWidth = 0, minHeight = 0;
            float sharpness = 0.0f;
            const NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
                m_ngxParams, static_cast<unsigned int>(windowSize.width()), static_cast<unsigned int>(windowSize.height()),
                dlssQuality(), &optimalWidth, &optimalHeight, &maxWidth, &maxHeight, &minWidth, &minHeight, &sharpness);
            if (m_window->dlssMode == 3)
                m_renderSize = windowSize; // DLAA: no upscaling
            else if (NVSDK_NGX_SUCCEED(result) && optimalWidth > 0 && optimalHeight > 0)
                m_renderSize = QSize(static_cast<int>(optimalWidth), static_cast<int>(optimalHeight));
            else
                m_renderSize = QSize(std::max(64, windowSize.width() * 58 / 100), std::max(32, windowSize.height() * 58 / 100));
            m_dlssActive = true;
        }
#endif
        const QSize size = m_renderSize;
        m_outputSize = m_dlssActive ? windowSize : size;
        // The history ping-pongs: frame f reads what frame 1 - f wrote.
        for (int i = 0; i < 2; ++i) {
            createImage(VK_FORMAT_R32G32B32A32_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_accumulation[i]);
            createImage(VK_FORMAT_R32G32B32A32_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_hit[i]);
            m_imagesUsed[i] = false;
        }
        createImage(VK_FORMAT_R8G8B8A8_UNORM, m_outputSize, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    &m_output);
        createImage(VK_FORMAT_R16G16B16A16_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_albedo);
        createImage(VK_FORMAT_R16G16B16A16_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_normal);
        createImage(VK_FORMAT_R32G32B32A32_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_filter[0]);
        createImage(VK_FORMAT_R32G32B32A32_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_filter[1]);
        // The guides of ray reconstruction, and its result at the window's size; made whether or not it is used,
        // since the shaders refer to them.
        createImage(VK_FORMAT_R16G16B16A16_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_color);
        createImage(VK_FORMAT_R16G16B16A16_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_specularAlbedo);
        createImage(VK_FORMAT_R32_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_depth);
        createImage(VK_FORMAT_R16G16_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_motion);
        createImage(VK_FORMAT_R16_SFLOAT, size, VK_IMAGE_USAGE_STORAGE_BIT, &m_roughness);
        createImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_dlssActive ? windowSize : QSize(1, 1), VK_IMAGE_USAGE_STORAGE_BIT,
                    &m_reconstructed);
#ifdef HAVE_DLSS
        if (m_dlssActive)
            createRayReconstruction(size, windowSize);
#endif
        m_swapRedBlue = false; // the blit to the swapchain image converts the format
        m_stillFrames = 0;

        for (std::size_t frame = 0; frame < m_descriptorSets.size(); ++frame) {
            const std::size_t current = frame % 2, previous = 1 - current;
            const struct {
                uint32_t binding;
                VkImageView view;
            } images[15] = {
                {1, m_accumulation[previous].view}, {2, m_accumulation[current].view}, {3, m_hit[previous].view},
                {4, m_hit[current].view},           {5, m_output.view},                {10, m_albedo.view},
                {11, m_normal.view},                {12, m_filter[0].view},            {13, m_filter[1].view},
                {14, m_color.view},                 {15, m_specularAlbedo.view},       {16, m_depth.view},
                {17, m_motion.view},                {18, m_roughness.view},            {19, m_reconstructed.view},
            };
            VkDescriptorImageInfo infos[15];
            VkWriteDescriptorSet writes[15];
            for (uint32_t i = 0; i < 15; ++i) {
                infos[i] = {VK_NULL_HANDLE, images[i].view, VK_IMAGE_LAYOUT_GENERAL};
                writes[i] = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
                writes[i].dstSet = m_descriptorSets[frame];
                writes[i].dstBinding = images[i].binding;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[i].pImageInfo = &infos[i];
            }
            m_df->vkUpdateDescriptorSets(m_dev, 15, writes, 0, nullptr);
        }
    }

    void releaseSwapChainResources() override {
#ifdef HAVE_DLSS
        releaseRayReconstruction();
#endif
        for (int i = 0; i < 2; ++i) {
            destroyImage(m_accumulation[i]);
            destroyImage(m_hit[i]);
            destroyImage(m_filter[i]);
        }
        destroyImage(m_output);
        destroyImage(m_albedo);
        destroyImage(m_normal);
        for (Image *image : {&m_color, &m_specularAlbedo, &m_depth, &m_motion, &m_roughness, &m_reconstructed})
            destroyImage(*image);
    }

    void releaseResources() override {
        if (!m_dev)
            return;
#ifdef HAVE_DLSS
        shutdownNgx();
#endif
        m_df->vkDestroyDescriptorPool(m_dev, m_descriptorPool, nullptr);
        m_df->vkDestroyPipeline(m_dev, m_atrousPipeline, nullptr);
        m_df->vkDestroyPipeline(m_dev, m_composePipeline, nullptr);
        m_df->vkDestroyPipelineLayout(m_dev, m_computeLayout, nullptr);
        m_df->vkDestroyPipeline(m_dev, m_pipeline, nullptr);
        if (m_reorderPipeline)
            m_df->vkDestroyPipeline(m_dev, m_reorderPipeline, nullptr);
        m_df->vkDestroyPipelineLayout(m_dev, m_pipelineLayout, nullptr);
        m_df->vkDestroyDescriptorSetLayout(m_dev, m_descriptorSetLayout, nullptr);
        destroyBuffer(m_sbt.buffer);
        destroyBuffer(m_reorderSbt.buffer);
        destroyAccelerationStructure(m_tlas);
        destroyBuffer(m_tlasScratch);
        for (Buffer &b : m_instanceBuffers)
            destroyBuffer(b);
        m_instanceBuffers.clear();
        for (Buffer &b : m_lightBuffers)
            destroyBuffer(b);
        m_lightBuffers.clear();
        for (Buffer &b : m_previousTransformBuffers)
            destroyBuffer(b);
        m_previousTransformBuffers.clear();
        for (Buffer &b : m_frameBuffers)
            destroyBuffer(b);
        m_frameBuffers.clear();
        destroyBuffer(m_materials);
        for (AccelerationStructure &as : m_blas)
            destroyAccelerationStructure(as);
        m_blas.clear();
        for (Buffer &b : m_geometryBuffers)
            destroyBuffer(b);
        m_geometryBuffers.clear();
        m_dev = VK_NULL_HANDLE;
    }

    void startNextFrame() override {
        VkCommandBuffer cmd = m_window->currentCommandBuffer();
#ifdef HAVE_DLSS
        // Switching ray reconstruction on or off changes the render size and the resources: remake them.
        if ((m_window->dlss && m_rrAvailable) != m_dlssActive) {
            m_df->vkDeviceWaitIdle(m_dev);
            releaseSwapChainResources();
            initSwapChainResources();
        }
#endif
        const bool dlss = m_dlssActive;
        const QSize size = m_renderSize;
        const auto width = static_cast<uint32_t>(size.width());
        const auto height = static_cast<uint32_t>(size.height());
        const int frame = m_window->currentFrame();
        const bool recording = m_window->fixedTime >= 0.0f;
        const float time = recording ? m_window->fixedTime : static_cast<float>(m_clock.elapsed()) / 1000.0f;

        // The scene moves while animating, so the top level is rebuilt; the previous transforms go to the shaders for
        // the reprojection of the history.
        const bool moving = m_window->animating;
        if (moving || recording)
            m_sceneTime = time;
        std::memcpy(m_previousTransformBuffers[static_cast<std::size_t>(frame)].mapped, m_lastTransforms.data(),
                    m_lastTransforms.size() * sizeof(float));
        writeInstances(frame, m_sceneTime);
        buildTopLevel(cmd, frame);
        // While anything moves a pixel keeps a short history; at rest, all of it, and the image converges.
        const bool changing = moving || m_window->flying || m_window->cameraMoved;
        m_window->cameraMoved = false;
        m_stillFrames = changing ? 0 : m_stillFrames + 1;

        // The images: the history images keep their contents, the output is rewritten.
        const std::size_t current = static_cast<std::size_t>(frame) % 2, previous = 1 - current;
        for (std::size_t i : {previous, current}) {
            for (Image *image : {&m_accumulation[i], &m_hit[i]}) {
                imageBarrier(cmd, image->image, m_imagesUsed[i] ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
            }
            m_imagesUsed[i] = true;
        }
        for (Image *image : {&m_output, &m_albedo, &m_normal, &m_filter[0], &m_filter[1], &m_color, &m_specularAlbedo,
                             &m_depth, &m_motion, &m_roughness, &m_reconstructed}) {
            imageBarrier(cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        // Flying: the camera circles the scene, swooping in and out.
        if (m_window->flying) {
            const float dt = std::min(time - m_lastFrameTime, 0.1f);
            m_window->yaw += dt * 12.0f;
            flyPath(time, &m_window->pitch, &m_window->distance);
            m_window->cameraMoved = true;
        }
        m_lastFrameTime = time;

        // The camera, orbiting the middle of the scene.
        const float yaw = m_window->yaw * 3.14159265f / 180.0f;
        const float pitch = m_window->pitch * 3.14159265f / 180.0f;
        const QVector3D target(0.0f, 1.5f, 0.0f);
        const QVector3D eye = target + m_window->distance * QVector3D(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                                                       std::cos(pitch) * std::cos(yaw));
        const QVector3D forward = (target - eye).normalized();
        const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
        FrameConstants constants{};
        setVector(constants.position, eye, std::tan(0.5f * 50.0f * 3.14159265f / 180.0f));
        setVector(constants.forward, forward, static_cast<float>(width) / static_cast<float>(height));
        setVector(constants.right, right, 0.0f);
        setVector(constants.up, up, 0.0f);
        if (m_frameIndex == 0)
            m_lastCamera = constants;
        std::memcpy(constants.previousPosition, m_lastCamera.position, 4 * sizeof(float));
        std::memcpy(constants.previousForward, m_lastCamera.forward, 4 * sizeof(float));
        std::memcpy(constants.previousRight, m_lastCamera.right, 4 * sizeof(float));
        std::memcpy(constants.previousUp, m_lastCamera.up, 4 * sizeof(float));
        constants.frame = m_frameIndex++;
        constants.samples = dlss ? std::max(1u, m_window->samples) : m_window->samples > 0 ? m_window->samples : m_autoSamples;
        constants.bounces = m_window->bounces;
        constants.flags = m_swapRedBlue ? 1u : 0u;
        constants.lightCount = m_lightCount;
        constants.maxHistory = dlss ? 0u : changing ? m_window->history : 0x7fffffffu;
        constants.dlss = dlss ? 1u : 0u;
        // Ray reconstruction integrates the frames itself and wants one sub-pixel offset per frame, from a Halton
        // sequence long enough to cover the output pixels under the upscale.
        const uint32_t phase = (m_jitterIndex++ % 32u) + 1u;
        constants.jitter[0] = dlss ? halton(phase, 2) : 0.5f;
        constants.jitter[1] = dlss ? halton(phase, 3) : 0.5f;
        if (m_window->resetHistory) { // a new video frame: nothing of the previous one is reused
            constants.maxHistory = 0;
            m_window->resetHistory = false;
            m_stillFrames = 0;
        }
        constants.candidates = m_window->candidates;
        constants.denoise = m_window->denoise ? 1u : 0u;
        std::memcpy(m_frameBuffers[static_cast<std::size_t>(frame)].mapped, &constants, sizeof(constants));
        m_lastCamera = constants;

        // Trace: the plain pipeline, or the one with shader execution reordering.
        const bool reorder = m_hasReorder && m_window->reorder;
        m_df->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, reorder ? m_reorderPipeline : m_pipeline);
        m_df->vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1,
                                      &m_descriptorSets[static_cast<std::size_t>(frame)], 0, nullptr);
        const ShaderBindingTable &sbt = reorder ? m_reorderSbt : m_sbt;
        m_cmdTraceRays(cmd, &sbt.rgen, &sbt.miss, &sbt.hit, &sbt.callable, width, height, 1);

        // Ray reconstruction, or the denoiser: four à-trous iterations over the irradiance; then the compose pass.
        computeBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
        PassConstants pass{1, 0, 1};
        uint32_t groupsX = (width + 7) / 8, groupsY = (height + 7) / 8;
        if (dlss) {
#ifdef HAVE_DLSS
            evaluateRayReconstruction(cmd, size, constants);
            computeBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
#endif
            pass.source = 3;
            groupsX = (static_cast<uint32_t>(m_outputSize.width()) + 7) / 8;
            groupsY = (static_cast<uint32_t>(m_outputSize.height()) + 7) / 8;
        }
        m_df->vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeLayout, 0, 1,
                                      &m_descriptorSets[static_cast<std::size_t>(frame)], 0, nullptr);
        if (m_window->denoise && !dlss) {
            m_df->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_atrousPipeline);
            for (int iteration = 0; iteration < 4; ++iteration) {
                pass.step = 1 << iteration;
                pass.source = iteration == 0 ? 0 : (iteration % 2 == 1 ? 1 : 2);
                pass.target = iteration % 2 == 0 ? 1 : 2;
                m_df->vkCmdPushConstants(cmd, m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pass), &pass);
                m_df->vkCmdDispatch(cmd, groupsX, groupsY, 1);
                computeBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            }
            pass.source = pass.target; // the last one written
        }
        m_df->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_composePipeline);
        m_df->vkCmdPushConstants(cmd, m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pass), &pass);
        m_df->vkCmdDispatch(cmd, groupsX, groupsY, 1);

        // The output to the swapchain image.
        VkImage target_image = m_window->swapChainImage(m_window->currentSwapChainImageIndex());
        imageBarrier(cmd, m_output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
        imageBarrier(cmd, target_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
        // Scaled up to the window with a linear filter.
        const QSize windowSize = m_window->swapChainImageSize();
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstSubresource = blit.srcSubresource;
        blit.srcOffsets[1] = {m_outputSize.width(), m_outputSize.height(), 1};
        blit.dstOffsets[1] = {windowSize.width(), windowSize.height(), 1};
        m_df->vkCmdBlitImage(cmd, m_output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target_image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                             m_outputSize == windowSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
        imageBarrier(cmd, target_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        m_window->frameReady();
        m_window->requestUpdate();
        countFrame(width, height, reorder);
    }

private:
#ifdef HAVE_DLSS
    // NGX, the driver's side of DLSS: started on the device, asked whether ray reconstruction is there.
    void initNgx() {
        QVulkanInstance *inst = m_window->vulkanInstance();
        const QByteArray dataDir = (QDir::tempPath() + "/nt-pathtracer-ngx").toUtf8();
        QDir().mkpath(QString::fromUtf8(dataDir));
        m_ngxDataPath = widen(dataDir);
        m_ngxLibraryPath = widen(QByteArray(DLSS_SDK_LIB_DIR));
        const wchar_t *paths[1] = {m_ngxLibraryPath.c_str()};
        NVSDK_NGX_FeatureCommonInfo info{};
        info.PathListInfo.Path = paths;
        info.PathListInfo.Length = 1;
        auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(inst->getInstanceProcAddr("vkGetInstanceProcAddr"));
        auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(inst->getInstanceProcAddr("vkGetDeviceProcAddr"));
        NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
            "4b2f0c1e-7a3d-4d5e-9b8a-2c6f1e0d9a47", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", m_ngxDataPath.c_str(),
            inst->vkInstance(), m_window->physicalDevice(), m_dev, gipa, gdpa, &info, NVSDK_NGX_Version_API);
        if (NVSDK_NGX_FAILED(result)) {
            std::printf("pathtracer: NGX did not start (0x%x): no DLSS on this device\n", static_cast<unsigned>(result));
            return;
        }
        m_ngxReady = true;
        result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&m_ngxParams);
        if (NVSDK_NGX_FAILED(result)) {
            std::printf("pathtracer: NGX capabilities unavailable (0x%x)\n", static_cast<unsigned>(result));
            return;
        }
        int available = 0, needsDriver = 0, initResult = 0;
        NVSDK_NGX_Parameter_GetI(m_ngxParams, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);
        NVSDK_NGX_Parameter_GetI(m_ngxParams, NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &needsDriver);
        NVSDK_NGX_Parameter_GetI(m_ngxParams, NVSDK_NGX_Parameter_SuperSamplingDenoising_FeatureInitResult, &initResult);
        m_rrAvailable = available != 0;
        std::printf("pathtracer: DLSS Ray Reconstruction %s%s\n", m_rrAvailable ? "available" : "not available",
                    needsDriver ? " (the driver is too old)" : "");
        if (!m_rrAvailable && initResult != 0)
            std::printf("pathtracer: NGX feature init result 0x%x\n", static_cast<unsigned>(initResult));
    }

    NVSDK_NGX_PerfQuality_Value dlssQuality() const {
        switch (m_window->dlssMode) {
        case 0:
            return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case 2:
            return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case 3:
            return NVSDK_NGX_PerfQuality_Value_DLAA;
        default:
            return NVSDK_NGX_PerfQuality_Value_Balanced;
        }
    }

    void shutdownNgx() {
        if (m_ngxParams)
            NVSDK_NGX_VULKAN_DestroyParameters(m_ngxParams);
        m_ngxParams = nullptr;
        if (m_ngxReady)
            NVSDK_NGX_VULKAN_Shutdown1(m_dev);
        m_ngxReady = false;
        m_rrAvailable = false;
    }

    static std::wstring widen(const QByteArray &utf8) {
        std::wstring wide;
        for (const char c : utf8)
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        return wide;
    }

    // The ray reconstruction feature: denoises `renderSize` into `windowSize`, with HDR colour and motion vectors at
    // the render size.
    void createRayReconstruction(const QSize &renderSize, const QSize &windowSize) {
        NVSDK_NGX_DLSSD_Create_Params params{};
        params.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
        params.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
        params.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
        params.InWidth = static_cast<unsigned int>(renderSize.width());
        params.InHeight = static_cast<unsigned int>(renderSize.height());
        params.InTargetWidth = static_cast<unsigned int>(windowSize.width());
        params.InTargetHeight = static_cast<unsigned int>(windowSize.height());
        params.InPerfQualityValue = dlssQuality();
        params.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
        submitAndWait([&](VkCommandBuffer cmd) {
            result = NGX_VULKAN_CREATE_DLSSD_EXT1(m_dev, cmd, 1, 1, &m_rrFeature, m_ngxParams, &params);
        });
        if (NVSDK_NGX_FAILED(result)) {
            std::printf("pathtracer: could not create DLSS Ray Reconstruction (0x%x)\n", static_cast<unsigned>(result));
            m_rrFeature = nullptr;
            m_rrAvailable = false;
            m_dlssActive = false;
            m_outputSize = renderSize;
            return;
        }
        m_rrReset = true;
        std::printf("pathtracer: DLSS Ray Reconstruction %dx%d -> %dx%d\n", renderSize.width(), renderSize.height(),
                    windowSize.width(), windowSize.height());
    }

    void releaseRayReconstruction() {
        if (m_rrFeature)
            NVSDK_NGX_VULKAN_ReleaseFeature(m_rrFeature);
        m_rrFeature = nullptr;
    }

    template <typename ImageT> // Image is declared further down
    static NVSDK_NGX_Resource_VK ngxResource(const ImageT &image, VkFormat format, const QSize &size, bool readWrite) {
        return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                                     format, static_cast<unsigned int>(size.width()),
                                                     static_cast<unsigned int>(size.height()), readWrite);
    }

    // The frame's guides and colour go in, the denoised, upscaled colour comes out in m_reconstructed.
    void evaluateRayReconstruction(VkCommandBuffer cmd, const QSize &renderSize, const FrameConstants &constants) {
        if (!m_rrFeature)
            return;
        const QSize windowSize = m_outputSize;
        NVSDK_NGX_Resource_VK color = ngxResource(m_color, VK_FORMAT_R16G16B16A16_SFLOAT, renderSize, false);
        NVSDK_NGX_Resource_VK diffuseAlbedo = ngxResource(m_albedo, VK_FORMAT_R16G16B16A16_SFLOAT, renderSize, false);
        NVSDK_NGX_Resource_VK specularAlbedo =
            ngxResource(m_specularAlbedo, VK_FORMAT_R16G16B16A16_SFLOAT, renderSize, false);
        NVSDK_NGX_Resource_VK normals = ngxResource(m_normal, VK_FORMAT_R16G16B16A16_SFLOAT, renderSize, false);
        NVSDK_NGX_Resource_VK roughness = ngxResource(m_roughness, VK_FORMAT_R16_SFLOAT, renderSize, false);
        NVSDK_NGX_Resource_VK depth = ngxResource(m_depth, VK_FORMAT_R32_SFLOAT, renderSize, false);
        NVSDK_NGX_Resource_VK motion = ngxResource(m_motion, VK_FORMAT_R16G16_SFLOAT, renderSize, false);
        NVSDK_NGX_Resource_VK output = ngxResource(m_reconstructed, VK_FORMAT_R16G16B16A16_SFLOAT, windowSize, true);
        NVSDK_NGX_VK_DLSSD_Eval_Params eval{};
        eval.pInColor = &color;
        eval.pInOutput = &output;
        eval.pInDiffuseAlbedo = &diffuseAlbedo;
        eval.pInSpecularAlbedo = &specularAlbedo;
        eval.pInNormals = &normals;
        eval.pInRoughness = &roughness;
        eval.pInDepth = &depth;
        eval.pInMotionVectors = &motion;
        eval.InJitterOffsetX = constants.jitter[0] - 0.5f;
        eval.InJitterOffsetY = constants.jitter[1] - 0.5f;
        eval.InRenderSubrectDimensions = {static_cast<unsigned int>(renderSize.width()),
                                          static_cast<unsigned int>(renderSize.height())};
        eval.InReset = m_rrReset ? 1 : 0;
        eval.InMVScaleX = 1.0f;
        eval.InMVScaleY = 1.0f;
        eval.InPreExposure = 1.0f;
        eval.InExposureScale = 1.0f;
        eval.InFrameTimeDeltaInMsec = 1000.0f / 90.0f;
        const NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSSD_EXT(cmd, m_rrFeature, m_ngxParams, &eval);
        if (NVSDK_NGX_FAILED(result) && !m_rrEvaluateFailed) {
            std::printf("pathtracer: DLSS Ray Reconstruction evaluate failed (0x%x)\n", static_cast<unsigned>(result));
            m_rrEvaluateFailed = true;
        }
        m_rrReset = false;
    }
#endif

    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct ShaderBindingTable {
        VkStridedDeviceAddressRegionKHR rgen{};
        VkStridedDeviceAddressRegionKHR miss{};
        VkStridedDeviceAddressRegionKHR hit{};
        VkStridedDeviceAddressRegionKHR callable{};
    };
    struct TableWithBuffer : ShaderBindingTable {
        Buffer buffer;
    };

    static void setVector(float *out, const QVector3D &v, float w) {
        out[0] = v.x();
        out[1] = v.y();
        out[2] = v.z();
        out[3] = w;
    }

    template <typename F> void resolve(QVulkanFunctions *f, const char *name, F *out) {
        *out = reinterpret_cast<F>(f->vkGetDeviceProcAddr(m_dev, name));
        if (!*out)
            fail(QString("%1 is missing").arg(name));
    }

    uint32_t memoryType(uint32_t typeBits, VkMemoryPropertyFlags wanted) const {
        for (uint32_t i = 0; i < m_memProps.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (m_memProps.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
        }
        fail("no suitable memory type");
    }

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
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
        alloc.memoryTypeIndex = memoryType(
            req.memoryTypeBits, hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                            : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            alloc.pNext = &flags;
        check(m_df->vkAllocateMemory(m_dev, &alloc, nullptr, &b.memory), "vkAllocateMemory");
        m_df->vkBindBufferMemory(m_dev, b.buffer, b.memory, 0);
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            auto addressInfo = make<VkBufferDeviceAddressInfo>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
            addressInfo.buffer = b.buffer;
            b.address = m_df->vkGetBufferDeviceAddress(m_dev, &addressInfo);
        }
        if (hostVisible)
            check(m_df->vkMapMemory(m_dev, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "vkMapMemory");
        return b;
    }

    Buffer createBuffer(const void *data, std::size_t size, VkBufferUsageFlags usage) {
        Buffer b = createBuffer(size, usage, true);
        std::memcpy(b.mapped, data, size);
        return b;
    }

    void destroyBuffer(Buffer &b) {
        if (b.mapped)
            m_df->vkUnmapMemory(m_dev, b.memory);
        if (b.buffer)
            m_df->vkDestroyBuffer(m_dev, b.buffer, nullptr);
        if (b.memory)
            m_df->vkFreeMemory(m_dev, b.memory, nullptr);
        b = Buffer();
    }

    void createImage(VkFormat format, const QSize &size, VkImageUsageFlags usage, Image *image) {
        auto info = make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()), 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT; // ray reconstruction samples its guides
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(m_df->vkCreateImage(m_dev, &info, nullptr, &image->image), "vkCreateImage");
        VkMemoryRequirements req{};
        m_df->vkGetImageMemoryRequirements(m_dev, image->image, &req);
        auto alloc = make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(m_df->vkAllocateMemory(m_dev, &alloc, nullptr, &image->memory), "vkAllocateMemory");
        m_df->vkBindImageMemory(m_dev, image->image, image->memory, 0);
        auto view = make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = image->image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(m_df->vkCreateImageView(m_dev, &view, nullptr, &image->view), "vkCreateImageView");
    }

    void destroyImage(Image &image) {
        if (image.view)
            m_df->vkDestroyImageView(m_dev, image.view, nullptr);
        if (image.image)
            m_df->vkDestroyImage(m_dev, image.image, nullptr);
        if (image.memory)
            m_df->vkFreeMemory(m_dev, image.memory, nullptr);
        image = Image();
    }

    // Makes what shaders wrote at `srcStage` visible to the compute shaders that follow.
    void computeBarrier(VkCommandBuffer cmd, VkPipelineStageFlags srcStage) {
        auto barrier = make<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        m_df->vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                   nullptr);
    }

    // The à-trous filter and the compose pass share the descriptor set of the frame and a layout with the pass
    // constants.
    void createComputePipelines() {
        VkPushConstantRange pushConstants{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PassConstants)};
        auto layoutInfo = make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstants;
        check(m_df->vkCreatePipelineLayout(m_dev, &layoutInfo, nullptr, &m_computeLayout), "vkCreatePipelineLayout");
        const struct {
            const char *file;
            VkPipeline *pipeline;
        } passes[2] = {{":/shaders/atrous.comp.spv", &m_atrousPipeline}, {":/shaders/compose.comp.spv", &m_composePipeline}};
        for (const auto &p : passes) {
            auto info = make<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
            info.stage = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module = loadShader(p.file);
            info.stage.pName = "main";
            info.layout = m_computeLayout;
            check(m_df->vkCreateComputePipelines(m_dev, VK_NULL_HANDLE, 1, &info, nullptr, p.pipeline),
                  "vkCreateComputePipelines");
            m_df->vkDestroyShaderModule(m_dev, info.stage.module, nullptr);
        }
    }

    void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                      VkPipelineStageFlags dstStage) {
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

    // The objects: a floor, boxes of metal, glass and color, a field of spheres, and a lamp.
    void buildScene() {
        m_objects.clear();
        m_objects.push_back({0, material(Checker, 0.85f, 0.85f, 0.8f), {0.0f, 0.0f, 0.0f}, {400.0f, 1.0f, 400.0f}});
        // Boxes around the middle.
        const struct {
            float x, z, height;
            Material mat;
            float spin;
        } boxes[] = {
            {-5.0f, -4.0f, 3.0f, material(Metal, 0.95f, 0.93f, 0.88f, 0.02f), 10.0f},
            {5.5f, -3.0f, 2.0f, material(Glass, 0.9f, 0.95f, 1.0f, 0.0f, 1.5f), 0.0f},
            {4.0f, 5.0f, 4.0f, material(Diffuse, 0.8f, 0.2f, 0.15f), 0.0f},
            {-4.5f, 5.5f, 1.5f, material(Metal, 1.0f, 0.78f, 0.35f, 0.25f), -15.0f},
            {0.0f, -7.0f, 2.5f, material(Diffuse, 0.2f, 0.4f, 0.85f), 20.0f},
        };
        for (const auto &b : boxes) {
            Object o{1, b.mat, {b.x, b.height / 2.0f, b.z}, {1.0f, b.height / 2.0f, 1.0f}};
            o.spin = b.spin;
            m_objects.push_back(o);
        }
        // A field of spheres: metals, glass and colors in turn.
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) {
                const float x = (static_cast<float>(i) - 3.5f) * 1.7f;
                const float z = (static_cast<float>(j) - 3.5f) * 1.7f;
                if (std::abs(x) < 2.6f && std::abs(z) < 2.6f)
                    continue; // room for the big ones
                const int kind = (i * 3 + j * 5) % 7;
                const float hue = static_cast<float>((i * 8 + j) % 12) / 12.0f;
                Material mat;
                if (kind < 3)
                    mat = material(Diffuse, 0.5f + 0.5f * std::cos(6.2832f * hue), 0.5f + 0.5f * std::cos(6.2832f * (hue + 0.33f)),
                                   0.5f + 0.5f * std::cos(6.2832f * (hue + 0.67f)));
                else if (kind < 5)
                    mat = material(Metal, 0.9f, 0.9f, 0.9f, 0.02f + 0.1f * static_cast<float>(kind - 3));
                else if (kind == 5)
                    mat = material(Glass, 0.95f, 1.0f, 0.95f, 0.0f, 1.5f);
                else
                    mat = material(Emissive, 1.0f, 0.6f + 0.3f * hue, 0.3f, 0.0f, 1.0f, 6.0f);
                Object o{2, mat, {x, 0.6f, z}, {0.6f, 0.6f, 0.6f}};
                o.bob = 0.35f;
                o.phase = static_cast<float>(i) * 0.9f + static_cast<float>(j) * 1.3f;
                m_objects.push_back(o);
            }
        }
        // Three big spheres in the middle: glass, mirror and a lamp above.
        m_objects.push_back({2, material(Glass, 1.0f, 1.0f, 1.0f, 0.0f, 1.5f), {-1.4f, 1.3f, 0.6f}, {1.3f, 1.3f, 1.3f}});
        m_objects.push_back({2, material(Metal, 0.98f, 0.98f, 0.98f, 0.0f), {1.6f, 1.3f, -0.8f}, {1.3f, 1.3f, 1.3f}});
        Object lamp{2, material(Emissive, 1.0f, 0.9f, 0.7f, 0.0f, 1.0f, 25.0f), {0.0f, 7.0f, 0.0f}, {0.8f, 0.8f, 0.8f}};
        lamp.bob = 0.5f;
        m_objects.push_back(lamp);
    }

    // The two triangle meshes (a unit quad for the floor, a unit box) and the sphere's bounding box, each in a
    // bottom-level acceleration structure.
    void createGeometry() {
        const float quad[] = {-1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, -1, 1, 0, 1, -1, 0, 1};
        std::vector<float> box;
        const float faces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const float *n : faces) {
            const QVector3D normal(n[0], n[1], n[2]);
            const QVector3D u = std::abs(normal.y()) > 0.5f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
            const QVector3D v = QVector3D::crossProduct(normal, u);
            const QVector3D corners[4] = {normal - u - v, normal + u - v, normal + u + v, normal - u + v};
            for (int k : {0, 1, 2, 0, 2, 3}) {
                box.push_back(corners[k].x());
                box.push_back(corners[k].y());
                box.push_back(corners[k].z());
            }
        }
        m_blas.push_back(buildTriangles(quad, 6));
        m_blas.push_back(buildTriangles(box.data(), static_cast<uint32_t>(box.size() / 3)));

        const VkAabbPositionsKHR aabb{-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
        Buffer aabbBuffer = createBuffer(&aabb, sizeof(aabb),
                                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        auto geometry = make<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.geometry.aabbs = make<VkAccelerationStructureGeometryAabbsDataKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR);
        geometry.geometry.aabbs.data.deviceAddress = aabbBuffer.address;
        geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);
        m_blas.push_back(build(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, 1, nullptr));
        m_geometryBuffers.push_back(aabbBuffer);
    }

    AccelerationStructure buildTriangles(const float *vertices, uint32_t vertexCount) {
        Buffer vertexBuffer = createBuffer(vertices, vertexCount * 3 * sizeof(float),
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        auto geometry = make<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles = make<VkAccelerationStructureGeometryTrianglesDataKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR);
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress = vertexBuffer.address;
        geometry.geometry.triangles.vertexStride = 3 * sizeof(float);
        geometry.geometry.triangles.maxVertex = vertexCount - 1;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
        // The data stays, so that position fetch can read the vertices from the acceleration structure.
        AccelerationStructure as = build(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, vertexCount / 3,
                                         nullptr, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR);
        m_geometryBuffers.push_back(vertexBuffer);
        return as;
    }

    // Builds an acceleration structure for one geometry, into a new one or, with `into`, an existing one.
    AccelerationStructure build(VkAccelerationStructureTypeKHR type, const VkAccelerationStructureGeometryKHR &geometry,
                                uint32_t primitiveCount, VkCommandBuffer into,
                                VkBuildAccelerationStructureFlagsKHR extraFlags = 0) {
        auto info = make<VkAccelerationStructureBuildGeometryInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        info.type = type;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | extraFlags;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = 1;
        info.pGeometries = &geometry;
        auto sizes = make<VkAccelerationStructureBuildSizesInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        m_getBuildSizes(m_dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &primitiveCount, &sizes);

        AccelerationStructure as;
        as.buffer = createBuffer(sizes.accelerationStructureSize,
                                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 false);
        auto createInfo =
            make<VkAccelerationStructureCreateInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
        createInfo.buffer = as.buffer.buffer;
        createInfo.size = sizes.accelerationStructureSize;
        createInfo.type = type;
        check(m_createAS(m_dev, &createInfo, nullptr, &as.handle), "vkCreateAccelerationStructureKHR");
        auto addressInfo = make<VkAccelerationStructureDeviceAddressInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
        addressInfo.accelerationStructure = as.handle;
        as.address = m_getASAddress(m_dev, &addressInfo);

        const VkDeviceSize alignment = m_asProps.minAccelerationStructureScratchOffsetAlignment;
        Buffer scratch = createBuffer(sizes.buildScratchSize + alignment,
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                      false);
        info.dstAccelerationStructure = as.handle;
        info.scratchData.deviceAddress = alignUp(scratch.address, alignment);
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
        if (into) {
            m_cmdBuildAS(into, 1, &info, &ranges);
            m_tlasScratch = scratch; // kept for the rebuilds
        } else {
            submitAndWait([&](VkCommandBuffer cmd) { m_cmdBuildAS(cmd, 1, &info, &ranges); });
            destroyBuffer(scratch);
        }
        return as;
    }

    void destroyAccelerationStructure(AccelerationStructure &as) {
        if (as.handle)
            m_destroyAS(m_dev, as.handle, nullptr);
        destroyBuffer(as.buffer);
        as = AccelerationStructure();
    }

    void createMaterials() {
        std::vector<Material> materials;
        for (const Object &o : m_objects)
            materials.push_back(o.material);
        m_materials = createBuffer(materials.data(), materials.size() * sizeof(Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    // The instances of a frame: every object with its transform at `time`; and the sphere lights among them, for
    // direct light sampling.
    void writeInstances(int frame, float time) {
        auto *instances = static_cast<VkAccelerationStructureInstanceKHR *>(m_instanceBuffers[static_cast<std::size_t>(frame)].mapped);
        auto *lights = static_cast<Light *>(m_lightBuffers[static_cast<std::size_t>(frame)].mapped);
        m_lightCount = 0;
        for (std::size_t i = 0; i < m_objects.size(); ++i) {
            const Object &o = m_objects[i];
            const QVector3D position = o.position + QVector3D(0.0f, o.bob * std::sin(time * 1.5f + o.phase), 0.0f);
            if (o.geometry == 2 && static_cast<int>(o.material.params[3]) == Emissive) {
                Light &light = lights[m_lightCount++];
                light.positionRadius[0] = position.x();
                light.positionRadius[1] = position.y();
                light.positionRadius[2] = position.z();
                light.positionRadius[3] = o.scale.x();
                for (int c = 0; c < 3; ++c)
                    light.emission[c] = o.material.albedo[c] * o.material.params[2];
                light.emission[3] = 0.0f;
            }
            QMatrix4x4 m;
            m.translate(position);
            if (o.spin != 0.0f)
                m.rotate(time * o.spin, 0.0f, 1.0f, 0.0f);
            m.scale(o.scale);
            VkAccelerationStructureInstanceKHR &instance = instances[i];
            instance = VkAccelerationStructureInstanceKHR{};
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 4; ++column) {
                    instance.transform.matrix[row][column] = m(row, column);
                    m_lastTransforms[(i * 3 + static_cast<std::size_t>(row)) * 4 + static_cast<std::size_t>(column)] =
                        m(row, column);
                }
            }
            instance.instanceCustomIndex = static_cast<uint32_t>(i) & 0xffffffu; // the material
            instance.mask = 0xff;
            instance.instanceShaderBindingTableRecordOffset = o.geometry == 2 ? 1u : 0u; // the hit group
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = m_blas[static_cast<std::size_t>(o.geometry)].address;
        }
    }

    void createTopLevel() {
        m_instanceBuffers.resize(static_cast<std::size_t>(m_window->concurrentFrameCount()));
        for (Buffer &b : m_instanceBuffers)
            b = createBuffer(m_objects.size() * sizeof(VkAccelerationStructureInstanceKHR),
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             true);
        m_lightBuffers.resize(m_instanceBuffers.size());
        for (Buffer &b : m_lightBuffers)
            b = createBuffer(m_objects.size() * sizeof(Light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        m_lastTransforms.assign(m_objects.size() * 12, 0.0f);
        m_previousTransformBuffers.resize(m_instanceBuffers.size());
        for (Buffer &b : m_previousTransformBuffers)
            b = createBuffer(m_lastTransforms.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        m_frameBuffers.resize(m_instanceBuffers.size());
        for (Buffer &b : m_frameBuffers)
            b = createBuffer(sizeof(FrameConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        writeInstances(0, 0.0f);
        VkAccelerationStructureGeometryKHR geometry = instanceGeometry(0);
        // Built once here, in a command buffer that is waited for, and its scratch buffer kept for the rebuilds.
        submitAndWait([&](VkCommandBuffer cmd) {
            m_tlas = build(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geometry,
                           static_cast<uint32_t>(m_objects.size()), cmd);
        });
    }

    VkAccelerationStructureGeometryKHR instanceGeometry(int frame) const {
        auto geometry = make<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = make<VkAccelerationStructureGeometryInstancesDataKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR);
        geometry.geometry.instances.data.deviceAddress = m_instanceBuffers[static_cast<std::size_t>(frame)].address;
        return geometry;
    }

    void buildTopLevel(VkCommandBuffer cmd, int frame) {
        auto barrier = make<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        m_df->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                   VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0,
                                   nullptr);
        VkAccelerationStructureGeometryKHR geometry = instanceGeometry(frame);
        auto info = make<VkAccelerationStructureBuildGeometryInfoKHR>(
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = 1;
        info.pGeometries = &geometry;
        info.dstAccelerationStructure = m_tlas.handle;
        info.scratchData.deviceAddress =
            alignUp(m_tlasScratch.address, m_asProps.minAccelerationStructureScratchOffsetAlignment);
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = static_cast<uint32_t>(m_objects.size());
        const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
        m_cmdBuildAS(cmd, 1, &info, &ranges);
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        m_df->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                   VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0,
                                   nullptr);
    }

    void createDescriptorLayout() {
        // 0 the scene; 1-4 the history, previous and current; 5 the output; 6 materials, 7 lights, 8 the previous
        // transforms, 9 the frame's camera and parameters; 10-13 the denoiser's albedo, normal and two work images;
        // 14-19 the guides and the result of ray reconstruction.
        VkDescriptorSetLayoutBinding bindings[20]{};
        const VkShaderStageFlags rayStages =
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
        const VkShaderStageFlags stages = rayStages | VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, rayStages, nullptr};
        for (uint32_t i = 1; i <= 5; ++i)
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, stages, nullptr};
        for (uint32_t i = 6; i <= 8; ++i)
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr};
        bindings[9] = {9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr};
        for (uint32_t i = 10; i <= 19; ++i)
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, stages, nullptr};
        auto layoutInfo = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layoutInfo.bindingCount = 20;
        layoutInfo.pBindings = bindings;
        check(m_df->vkCreateDescriptorSetLayout(m_dev, &layoutInfo, nullptr, &m_descriptorSetLayout),
              "vkCreateDescriptorSetLayout");
        auto pipelineLayoutInfo = make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        check(m_df->vkCreatePipelineLayout(m_dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout),
              "vkCreatePipelineLayout");
    }

    VkShaderModule loadShader(const char *path) {
        QFile file(QString::fromLatin1(path));
        if (!file.open(QIODevice::ReadOnly))
            fail(QString("cannot open %1").arg(QString::fromLatin1(path)));
        const QByteArray code = file.readAll();
        auto info = make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        info.codeSize = static_cast<std::size_t>(code.size());
        info.pCode = reinterpret_cast<const uint32_t *>(code.constData());
        VkShaderModule module = VK_NULL_HANDLE;
        check(m_df->vkCreateShaderModule(m_dev, &info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    // A pipeline with the given ray generation shader: miss, the triangle hit group and the sphere hit group
    // (intersection and closest hit), and its shader binding table.
    VkPipeline createPipeline(const char *raygen, TableWithBuffer &sbt) {
        const struct {
            VkShaderStageFlagBits stage;
            const char *file;
        } shaders[] = {
            {VK_SHADER_STAGE_RAYGEN_BIT_KHR, raygen},
            {VK_SHADER_STAGE_MISS_BIT_KHR, ":/shaders/pathtracer.rmiss.spv"},
            {VK_SHADER_STAGE_MISS_BIT_KHR, ":/shaders/shadow.rmiss.spv"},
            {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, ":/shaders/triangles.rchit.spv"},
            {VK_SHADER_STAGE_INTERSECTION_BIT_KHR, ":/shaders/sphere.rint.spv"},
            {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, ":/shaders/sphere.rchit.spv"},
        };
        VkPipelineShaderStageCreateInfo stages[6];
        for (int i = 0; i < 6; ++i) {
            stages[i] = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stages[i].stage = shaders[i].stage;
            stages[i].module = loadShader(shaders[i].file);
            stages[i].pName = "main";
        }
        VkRayTracingShaderGroupCreateInfoKHR groups[5];
        for (VkRayTracingShaderGroupCreateInfoKHR &g : groups) {
            g = make<VkRayTracingShaderGroupCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
            g.generalShader = g.closestHitShader = g.anyHitShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR; // ray generation
        groups[0].generalShader = 0;
        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR; // miss: the sky
        groups[1].generalShader = 1;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR; // miss: a shadow ray that reached its light
        groups[2].generalShader = 2;
        groups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[3].closestHitShader = 3;
        groups[4].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        groups[4].intersectionShader = 4;
        groups[4].closestHitShader = 5;

        auto info = make<VkRayTracingPipelineCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
        info.stageCount = 6;
        info.pStages = stages;
        info.groupCount = 5;
        info.pGroups = groups;
        info.maxPipelineRayRecursionDepth = 2; // the bounces are a loop in the ray generation shader; shadow rays nest once
        info.layout = m_pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        check(m_createPipelines(m_dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
              "vkCreateRayTracingPipelinesKHR");
        for (const VkPipelineShaderStageCreateInfo &stage : stages)
            m_df->vkDestroyShaderModule(m_dev, stage.module, nullptr);

        // The shader binding table: ray generation, the two miss shaders, then the two hit groups in one region, so
        // that an instance's record offset picks the triangle or the sphere group.
        const uint32_t handleSize = m_rtProps.shaderGroupHandleSize;
        const VkDeviceSize handleStride = alignUp(handleSize, m_rtProps.shaderGroupHandleAlignment);
        const VkDeviceSize baseAlignment = m_rtProps.shaderGroupBaseAlignment;
        const VkDeviceSize rgenOffset = 0, missOffset = alignUp(handleStride, baseAlignment);
        const VkDeviceSize hitOffset = missOffset + alignUp(2 * handleStride, baseAlignment);
        const VkDeviceSize total = hitOffset + alignUp(2 * handleStride, baseAlignment);
        std::vector<uint8_t> handles(static_cast<std::size_t>(5 * handleSize));
        check(m_getGroupHandles(m_dev, pipeline, 0, 5, handles.size(), handles.data()),
              "vkGetRayTracingShaderGroupHandlesKHR");
        Buffer &buffer = sbt.buffer;
        buffer = createBuffer(total,
                              VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                              true);
        auto *dst = static_cast<uint8_t *>(buffer.mapped);
        std::memcpy(dst + rgenOffset, handles.data(), handleSize);
        std::memcpy(dst + missOffset, handles.data() + handleSize, handleSize);
        std::memcpy(dst + missOffset + handleStride, handles.data() + 2 * handleSize, handleSize);
        std::memcpy(dst + hitOffset, handles.data() + 3 * handleSize, handleSize);
        std::memcpy(dst + hitOffset + handleStride, handles.data() + 4 * handleSize, handleSize);
        sbt.rgen = {buffer.address + rgenOffset, alignUp(handleStride, baseAlignment), alignUp(handleStride, baseAlignment)};
        sbt.miss = {buffer.address + missOffset, handleStride, alignUp(2 * handleStride, baseAlignment)};
        sbt.hit = {buffer.address + hitOffset, handleStride, alignUp(2 * handleStride, baseAlignment)};
        sbt.callable = {};
        return pipeline;
    }

    void createDescriptors() {
        // One set per frame in flight: each frame has its own lights buffer.
        const auto frames = static_cast<uint32_t>(m_window->concurrentFrameCount());
        VkDescriptorPoolSize sizes[4] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, frames},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 15 * frames},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * frames},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames}};
        auto poolInfo = make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        poolInfo.maxSets = frames;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = sizes;
        check(m_df->vkCreateDescriptorPool(m_dev, &poolInfo, nullptr, &m_descriptorPool), "vkCreateDescriptorPool");
        m_descriptorSets.resize(frames);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            auto allocInfo = make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            allocInfo.descriptorPool = m_descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &m_descriptorSetLayout;
            check(m_df->vkAllocateDescriptorSets(m_dev, &allocInfo, &m_descriptorSets[frame]),
                  "vkAllocateDescriptorSets");

            // The scene, the materials and the lights; the images follow in initSwapChainResources().
            auto asWrite = make<VkWriteDescriptorSetAccelerationStructureKHR>(
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &m_tlas.handle;
            const VkDescriptorBufferInfo bufferInfos[4] = {
                {m_materials.buffer, 0, VK_WHOLE_SIZE},
                {m_lightBuffers[frame].buffer, 0, VK_WHOLE_SIZE},
                {m_previousTransformBuffers[frame].buffer, 0, VK_WHOLE_SIZE},
                {m_frameBuffers[frame].buffer, 0, VK_WHOLE_SIZE},
            };
            VkWriteDescriptorSet writes[5];
            writes[0] = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            writes[0].pNext = &asWrite;
            writes[0].dstSet = m_descriptorSets[frame];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            for (uint32_t i = 0; i < 4; ++i) {
                writes[1 + i] = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
                writes[1 + i].dstSet = m_descriptorSets[frame];
                writes[1 + i].dstBinding = 6 + i;
                writes[1 + i].descriptorCount = 1;
                writes[1 + i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[1 + i].pBufferInfo = &bufferInfos[i];
            }
            m_df->vkUpdateDescriptorSets(m_dev, 5, writes, 0, nullptr);
        }
    }

    // The frame rate and the paths per second in the title, once a second; screenshots; the frame limit.
    void countFrame(uint32_t width, uint32_t height, bool reorder) {
        ++m_frames;
        ++m_framesThisPeriod;
        const qint64 elapsed = m_statsClock.elapsed();
        if (elapsed >= 500) {
            const double fps = m_framesThisPeriod * 1000.0 / static_cast<double>(elapsed);
            const uint32_t samples =
                m_dlssActive ? std::max(1u, m_window->samples) : m_window->samples > 0 ? m_window->samples : m_autoSamples;
            const double paths = fps * width * height * samples / 1e6;
            // As many paths per pixel as keep about 90 frames per second: the cost is close to linear in them.
            if (m_window->samples == 0 && !m_dlssActive) {
                // Rounded, not floored: from 1 path per pixel at 175 fps the next step is 2, not 1 forever.
                const double wanted = static_cast<double>(m_autoSamples) * fps / 90.0;
                m_autoSamples = static_cast<uint32_t>(std::max(1.0, std::min(64.0, std::round(wanted))));
            }
            char title[256];
            std::snprintf(title, sizeof title,
                          "Nt path tracer | %s | %ux%u in %dx%d | %.0f fps | %u spp | %.0f Mpaths/s | %u bounces | "
                          "reordering %s | %s | %s",
                          m_deviceName.toUtf8().constData(), width, height, m_window->swapChainImageSize().width(),
                          m_window->swapChainImageSize().height(), fps, samples, paths, m_window->bounces,
                          m_hasReorder ? (reorder ? "on" : "off") : "n/a",
                          m_dlssActive ? "DLSS ray reconstruction" : m_window->denoise ? "denoise on" : "denoise off",
                          m_stillFrames == 0 ? (m_window->flying ? "flying" : "moving")
                                             : QString("%1 samples").arg(m_stillFrames * samples).toUtf8().constData());
            m_window->setTitle(QString::fromUtf8(title));
            std::puts(title);
            m_framesThisPeriod = 0;
            m_statsClock.restart();
        }
        if (m_window->frameLimit() > 0 && m_frames >= m_window->frameLimit())
            QCoreApplication::quit();
    }

    Window *m_window;
    bool m_hasReorder = false;
    VkDevice m_dev = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_df = nullptr;
    QString m_deviceName;
    VkPhysicalDeviceMemoryProperties m_memProps{};
    VkPhysicalDeviceBufferDeviceAddressFeatures m_addressFeatures{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_asFeatures{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_rtFeatures{};
    VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR m_fetchFeatures{};
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV m_reorderFeatures{};
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

    std::vector<Object> m_objects;
    std::vector<Buffer> m_geometryBuffers;
    std::vector<AccelerationStructure> m_blas;
    Buffer m_materials;
    std::vector<Buffer> m_instanceBuffers;
    AccelerationStructure m_tlas;
    Buffer m_tlasScratch;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipeline m_reorderPipeline = VK_NULL_HANDLE;
    TableWithBuffer m_sbt;
    TableWithBuffer m_reorderSbt;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<Buffer> m_lightBuffers;
    uint32_t m_lightCount = 0;
    uint32_t m_autoSamples = 1; // paths per pixel per frame, raised while the frame rate allows
    Image m_accumulation[2];
    Image m_hit[2];
    bool m_imagesUsed[2] = {false, false};
    Image m_output;
    QSize m_renderSize;
    QSize m_outputSize;
    Image m_albedo;
    Image m_color;
    Image m_specularAlbedo;
    Image m_depth;
    Image m_motion;
    Image m_roughness;
    Image m_reconstructed;
    bool m_dlssActive = false;
    uint32_t m_jitterIndex = 0;
#ifdef HAVE_DLSS
    bool m_ngxReady = false;
    bool m_rrAvailable = false;
    bool m_rrReset = true;
    bool m_rrEvaluateFailed = false;
    NVSDK_NGX_Parameter *m_ngxParams = nullptr;
    NVSDK_NGX_Handle *m_rrFeature = nullptr;
    std::wstring m_ngxDataPath;
    std::wstring m_ngxLibraryPath;
#endif
    Image m_normal;
    Image m_filter[2];
    VkPipelineLayout m_computeLayout = VK_NULL_HANDLE;
    VkPipeline m_atrousPipeline = VK_NULL_HANDLE;
    VkPipeline m_composePipeline = VK_NULL_HANDLE;
    std::vector<Buffer> m_previousTransformBuffers;
    std::vector<float> m_lastTransforms;
    std::vector<Buffer> m_frameBuffers;
    FrameConstants m_lastCamera{};
    bool m_swapRedBlue = false;
    uint32_t m_stillFrames = 0;
    uint32_t m_frameIndex = 0;
    float m_sceneTime = 0.0f;
    float m_lastFrameTime = 0.0f;

    QElapsedTimer m_clock;
    QElapsedTimer m_statsClock;
    int m_frames = 0;
    int m_framesThisPeriod = 0;
};

QVulkanWindowRenderer *Window::createRenderer() { return new PathTracer(this); }

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    int deviceIndex = -1;
    int frameLimit = 0;
    bool vsync = false; // mailbox or immediate presentation: the frame rate tells the GPU's pace
    bool validate = false;
    bool listDevices = false;
    bool reorder = true;
    bool still = false;
    bool fly = false;
    uint32_t samples = 0;
    uint32_t history = 48;
    uint32_t candidates = 8;
    bool denoise = true;
    bool dlss = false;
    int dlssMode = 1;
    float scale = 0.0f;
    QString recordDir;
    double seconds = 30.0;
    int fps = 30;
    int passes = 8;
    QString screenshot;
    int width = 1280, height = 720;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc)
            deviceIndex = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            frameLimit = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc && std::sscanf(argv[++i], "%dx%d", &width, &height) != 2)
            fail("--size takes WIDTHxHEIGHT");
        else if (std::strcmp(argv[i], "--vsync") == 0)
            vsync = true;
        else if (std::strcmp(argv[i], "--no-vsync") == 0)
            vsync = false;
        else if (std::strcmp(argv[i], "--spp") == 0 && i + 1 < argc)
            samples = static_cast<uint32_t>(std::max(1, std::min(64, std::atoi(argv[++i]))));
        else if (std::strcmp(argv[i], "--history") == 0 && i + 1 < argc)
            history = static_cast<uint32_t>(std::max(0, std::min(1000, std::atoi(argv[++i]))));
        else if (std::strcmp(argv[i], "--candidates") == 0 && i + 1 < argc)
            candidates = static_cast<uint32_t>(std::max(1, std::min(64, std::atoi(argv[++i]))));
        else if (std::strcmp(argv[i], "--no-denoise") == 0)
            denoise = false;
        else if (std::strcmp(argv[i], "--dlss") == 0)
            dlss = true;
        else if (std::strcmp(argv[i], "--dlss-mode") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            dlssMode = !std::strcmp(mode, "performance") ? 0 : !std::strcmp(mode, "quality") ? 2 : !std::strcmp(mode, "dlaa") ? 3 : 1;
        }
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = std::max(0.1f, std::min(1.0f, static_cast<float>(std::atof(argv[++i]))));
        else if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            recordDir = QString::fromLocal8Bit(argv[++i]);
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = std::max(0.1, std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc)
            fps = std::max(1, std::min(240, std::atoi(argv[++i])));
        else if (std::strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
            passes = std::max(1, std::min(4096, std::atoi(argv[++i])));
        else if (std::strcmp(argv[i], "--no-reorder") == 0)
            reorder = false;
        else if (std::strcmp(argv[i], "--still") == 0)
            still = true; // the scene stands, so the frames accumulate from the start
        else if (std::strcmp(argv[i], "--fly") == 0)
            fly = true;
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshot = QString::fromLocal8Bit(argv[++i]);
        else if (std::strcmp(argv[i], "--validate") == 0)
            validate = true;
        else if (std::strcmp(argv[i], "--devices") == 0)
            listDevices = true;
    }

    QVulkanInstance inst;
    inst.setApiVersion(QVersionNumber(1, 3));
    if (validate)
        inst.setLayers({"VK_LAYER_KHRONOS_validation"});
#ifdef HAVE_DLSS
    if (dlss) {
        unsigned int instanceCount = 0, deviceCount = 0;
        const char **instanceExts = nullptr, **deviceExts = nullptr;
        if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_RequiredExtensions(&instanceCount, &instanceExts, &deviceCount, &deviceExts))) {
            QByteArrayList wanted;
            for (unsigned int i = 0; i < instanceCount; ++i)
                wanted.append(instanceExts[i]);
            inst.setExtensions(wanted);
        }
    }
#endif
    if (!inst.create())
        fail(QString("no Vulkan (error %1)").arg(static_cast<int>(inst.errorCode())));

    Window window(frameLimit);
    window.reorder = reorder;
    window.samples = samples;
    window.history = history;
    window.candidates = candidates;
    window.denoise = denoise;
    window.dlss = dlss;
    window.dlssMode = dlssMode;
    window.scale = scale;
    window.animating = !still;
    window.flying = fly;
    window.setVulkanInstance(&inst);
    const QList<VkPhysicalDeviceProperties> devices = window.availablePhysicalDevices();
    if (listDevices) {
        int index = 0;
        for (const VkPhysicalDeviceProperties &props : devices)
            std::printf("%d: %s (Vulkan %u.%u)\n", index++, props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
                        VK_API_VERSION_MINOR(props.apiVersion));
        return 0;
    }
    if (deviceIndex < 0) {
        // A discrete GPU with ray tracing, else any device with it.
        for (int pass = 0; pass < 2 && deviceIndex < 0; ++pass) {
            for (int i = 0; i < devices.size(); ++i) {
                if (pass == 0 && devices.at(i).deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    continue;
                window.setPhysicalDeviceIndex(i);
                const QVulkanInfoVector<QVulkanExtension> extensions = window.supportedDeviceExtensions();
                if (extensions.contains("VK_KHR_ray_tracing_pipeline") &&
                    extensions.contains("VK_KHR_ray_tracing_position_fetch")) {
                    deviceIndex = i;
                    break;
                }
            }
        }
        if (deviceIndex < 0)
            fail("no device with ray tracing; see --devices");
    }
    window.setPhysicalDeviceIndex(deviceIndex);
    QSurfaceFormat format;
    format.setSwapInterval(vsync ? 1 : 0);
    window.setFormat(format);
    window.setTitle("Nt path tracer");
    window.resize(width, height);
    window.show();

    if (!recordDir.isEmpty()) {
        // Offline: each video frame at its own time, converged over `passes` renderings, read back and saved.
        if (window.samples == 0)
            window.samples = 16;
        if (window.scale <= 0.0f)
            window.scale = 1.0f; // no upscaling in a video
        window.animating = false;
        window.flying = false;
        if (!QDir().mkpath(recordDir))
            fail(QString("cannot create %1").arg(recordDir));
        QElapsedTimer waited;
        waited.start();
        while (!window.isValid() && waited.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
        if (!window.isValid())
            fail("the window did not initialize");
        const int total = static_cast<int>(std::lround(seconds * fps));
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < total; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(fps);
            window.yaw = 150.0f + 12.0f * t;
            flyPath(t, &window.pitch, &window.distance);
            window.fixedTime = t;
            window.resetHistory = true;
            QImage image;
            for (int pass = 0; pass < passes; ++pass)
                image = window.grab();
            char name[64];
            std::snprintf(name, sizeof name, "/frame_%05d.png", i);
            if (image.isNull() || !image.save(recordDir + QString::fromLatin1(name)))
                fail(QString("could not save frame %1").arg(i));
            if (i % fps == 0 || i == total - 1)
                std::printf("pathtracer: frame %d of %d (%d x %d, %u paths per pixel), %.0f s\n", i + 1, total,
                            image.width(), image.height(), window.samples * static_cast<uint32_t>(passes),
                            static_cast<double>(clock.elapsed()) / 1000.0);
            QCoreApplication::processEvents();
        }
        const QString video = recordDir + "/flythrough.mp4";
        const int encoded = QProcess::execute(
            "ffmpeg", {"-y", "-loglevel", "error", "-framerate", QString::number(fps), "-i",
                       recordDir + "/frame_%05d.png", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", video});
        if (encoded == 0)
            std::printf("pathtracer: wrote %s\n", video.toUtf8().constData());
        else
            std::printf("pathtracer: ffmpeg is not available or failed (%d); the frames are in %s\n", encoded,
                        recordDir.toUtf8().constData());
        return 0;
    }

    const int result = app.exec();
    if (!screenshot.isEmpty() && window.isValid()) { // when the frames are up: the picture as it stands
        const QImage image = window.grab();
        if (image.isNull() || !image.save(screenshot))
            fail(QString("could not save %1").arg(screenshot));
        std::printf("pathtracer: saved %s\n", screenshot.toUtf8().constData());
    }
    return result;
}
