// MIT License - see LICENSE
//
// A QVulkanWindow with no shaders: each frame clears to a colour that changes with time, and clears a rectangle that
// slides across it. It prints the name of the Vulkan device. The same source runs on the desktop.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QVulkanDeviceFunctions>
#include <QVulkanInstance>
#include <QVulkanWindow>

#include <cmath>

class Renderer : public QVulkanWindowRenderer {
public:
    explicit Renderer(QVulkanWindow *window) : m_window(window) {}

    void initResources() override {
        m_functions = m_window->vulkanInstance()->deviceFunctions(m_window->device());
        qInfo("Vulkan device: %s", m_window->physicalDeviceProperties()->deviceName);
        m_clock.start();
    }

    void startNextFrame() override {
        const float t = static_cast<float>(m_clock.elapsed()) / 1000.0f;
        const QSize size = m_window->swapChainImageSize();

        VkClearValue clear[3] = {};
        clear[0].color = {{0.12f, 0.14f, 0.28f + 0.12f * std::sin(t), 1.0f}};
        clear[1].depthStencil = {1.0f, 0};
        clear[2].color = clear[0].color;

        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = m_window->defaultRenderPass();
        begin.framebuffer = m_window->currentFramebuffer();
        begin.renderArea.extent = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height())};
        begin.clearValueCount = m_window->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
        begin.pClearValues = clear;
        const VkCommandBuffer commands = m_window->currentCommandBuffer();
        m_functions->vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);

        VkClearAttachment rectangle{};
        rectangle.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rectangle.clearValue.color = {{0.98f, 0.82f, 0.24f, 1.0f}};
        VkClearRect area{};
        const int w = size.width() / 5, h = size.height() / 5;
        area.rect.offset = {static_cast<int32_t>((size.width() - w) * (0.5f + 0.5f * std::sin(t))),
                            (size.height() - h) / 2};
        area.rect.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        area.layerCount = 1;
        m_functions->vkCmdClearAttachments(commands, 1, &rectangle, 1, &area);

        m_functions->vkCmdEndRenderPass(commands);
        m_window->frameReady();
        m_window->requestUpdate(); // the next frame
    }

private:
    QVulkanWindow *m_window;
    QVulkanDeviceFunctions *m_functions = nullptr;
    QElapsedTimer m_clock;
};

class Window : public QVulkanWindow {
public:
    QVulkanWindowRenderer *createRenderer() override { return new Renderer(this); }
};

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QVulkanInstance instance;
    if (!instance.create()) {
        qWarning("No Vulkan instance: %d", static_cast<int>(instance.errorCode()));
        return 1;
    }
    Window window;
    window.setVulkanInstance(&instance);
    window.resize(800, 600);
    window.show();
    return app.exec();
}
