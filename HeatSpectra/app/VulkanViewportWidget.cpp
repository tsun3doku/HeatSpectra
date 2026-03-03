#include "VulkanViewportWidget.hpp"

#include "App.h"
#include "render/SceneColorSpace.hpp"

#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtGui/rhi/qrhi_platform.h>

#include <chrono>
#include <iostream>

namespace {
const std::array<float, 4> srgb = clearColorSRGBA();
const QColor viewportClearColor = QColor::fromRgbF(srgb[0], srgb[1], srgb[2], srgb[3]);
}

VulkanViewportWidget::VulkanViewportWidget(QWidget* parent)
    : QRhiWidget(parent) {
    setApi(QRhiWidget::Api::Vulkan);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

VulkanViewportWidget::~VulkanViewportWidget() {
    releaseResources();
}

void VulkanViewportWidget::setApp(App* appPtr) {
    if (app == appPtr) {
        return;
    }

    if (app && appInitialized) {
        app->shutdown();
    }

    app = appPtr;
    appInitialized = false;
    frameTimer.invalidate();
    releaseImportedTextures();
    pipelineReady = false;
    sourceWidth = 0;
    sourceHeight = 0;
}

void VulkanViewportWidget::releaseImportedTextures() {
    for (auto& entry : importedTextures) {
        delete entry.second;
    }
    importedTextures.clear();
    activeTexture = nullptr;
    activeTextureHandle = 0;
    importedGeneration = 0;
}

void VulkanViewportWidget::releaseResources() {
    delete pipeline;
    pipeline = nullptr;
    delete srb;
    srb = nullptr;
    delete sampler;
    sampler = nullptr;
    releaseImportedTextures();
    pipelineReady = false;
    sourceWidth = 0;
    sourceHeight = 0;
}

QShader VulkanViewportWidget::loadShader(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::cerr << "[VulkanViewportWidget] Failed to open shader: "
                  << path.toStdString() << std::endl;
        return QShader();
    }

    QShader shader = QShader::fromSerialized(f.readAll());
    if (!shader.isValid()) {
        std::cerr << "[VulkanViewportWidget] Invalid shader: "
                  << path.toStdString() << std::endl;
    }
    return shader;
}

bool VulkanViewportWidget::buildAppVulkanContext(AppVulkanContext& outContext) const {
    if (!rhi()) {
        return false;
    }

    if (rhi()->backend() != QRhi::Vulkan) {
        std::cerr << "[VulkanViewportWidget] QRhi backend is not Vulkan" << std::endl;
        return false;
    }

    const auto* handles = static_cast<const QRhiVulkanNativeHandles*>(rhi()->nativeHandles());
    if (!handles || handles->physDev == VK_NULL_HANDLE || handles->dev == VK_NULL_HANDLE || handles->gfxQueue == VK_NULL_HANDLE) {
        std::cerr << "[VulkanViewportWidget] Failed to query Vulkan native handles from QRhi" << std::endl;
        return false;
    }

    outContext = {};
    outContext.physicalDevice = handles->physDev;
    outContext.device = handles->dev;
    outContext.graphicsQueue = handles->gfxQueue;
    outContext.queueFamilyIndex = handles->gfxQueueFamilyIdx;
    return true;
}

bool VulkanViewportWidget::initializeAppFromRhi() {
    if (!app) {
        return false;
    }

    AppVulkanContext context{};
    if (!buildAppVulkanContext(context)) {
        return false;
    }

    appInitialized = app->initialize(runtimeState, context);
    if (appInitialized) {
        frameTimer.start();
    } else {
        std::cerr << "[VulkanViewportWidget] App initialization failed" << std::endl;
    }
    return appInitialized;
}

bool VulkanViewportWidget::ensureAppInitialized() {
    if (appInitialized) {
        return true;
    }
    return initializeAppFromRhi();
}

bool VulkanViewportWidget::ensureImportedTexture(const AppViewportOutput& output) {
    if (!output.valid || output.imageHandle == 0 || output.width == 0 || output.height == 0) {
        activeTexture = nullptr;
        activeTextureHandle = 0;
        return false;
    }

    if (importedGeneration != output.generation) {
        releaseImportedTextures();
        importedGeneration = output.generation;
    }

    auto it = importedTextures.find(output.imageHandle);
    if (it == importedTextures.end()) {
        QRhiTexture* importedTexture = rhi()->newTexture(
            QRhiTexture::BGRA8,
            QSize(static_cast<int>(output.width), static_cast<int>(output.height)));
        if (!importedTexture) {
            return false;
        }

        QRhiTexture::NativeTexture nativeTexture{};
        nativeTexture.object = output.imageHandle;
        nativeTexture.layout = output.layout;
        if (!importedTexture->createFrom(nativeTexture)) {
            delete importedTexture;
            return false;
        }

        importedTextures.emplace(output.imageHandle, importedTexture);
        it = importedTextures.find(output.imageHandle);
    } else {
        it->second->setNativeLayout(output.layout);
    }

    activeTexture = it->second;
    activeTextureHandle = output.imageHandle;
    sourceWidth = output.width;
    sourceHeight = output.height;
    return true;
}

bool VulkanViewportWidget::updateShaderBindings(QRhiTexture* sceneTexture) {
    if (!srb || !sampler || !sceneTexture) {
        return false;
    }

    srb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(
            0,
            QRhiShaderResourceBinding::FragmentStage,
            sceneTexture,
            sampler)
    });
    if (!srb->create()) {
        std::cerr << "[VulkanViewportWidget] Failed to update SRB" << std::endl;
        return false;
    }

    return true;
}

bool VulkanViewportWidget::ensurePipeline() {
    if (pipelineReady) {
        return true;
    }
    if (!sampler || !activeTexture) {
        return false;
    }
    if (!vertShader.isValid() || !fragShader.isValid()) {
        return false;
    }

    if (!srb) {
        srb = rhi()->newShaderResourceBindings();
    }
    if (!updateShaderBindings(activeTexture)) {
        std::cerr << "[VulkanViewportWidget] Failed to create SRB" << std::endl;
        return false;
    }

    if (!pipeline) {
        pipeline = rhi()->newGraphicsPipeline();
    }

    pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vertShader },
        { QRhiShaderStage::Fragment, fragShader }
    });
    pipeline->setShaderResourceBindings(srb);
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    QRhiVertexInputLayout inputLayout;
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    pipeline->setDepthTest(false);
    pipeline->setDepthWrite(false);

    pipelineReady = pipeline->create();
    if (!pipelineReady) {
        std::cerr << "[VulkanViewportWidget] Failed to create graphics pipeline" << std::endl;
    }
    return pipelineReady;
}

void VulkanViewportWidget::initialize(QRhiCommandBuffer* cb) {
    (void)cb;

    if (rhi() != currentRhi) {
        releaseResources();
        currentRhi = rhi();
    }

    if (!vertShader.isValid()) {
        vertShader = loadShader(QStringLiteral("shaders/viewport_blit.vert.qsb"));
    }
    if (!fragShader.isValid()) {
        fragShader = loadShader(QStringLiteral("shaders/viewport_blit.frag.qsb"));
    }

    if (!sampler) {
        sampler = rhi()->newSampler(
            QRhiSampler::Linear,
            QRhiSampler::Linear,
            QRhiSampler::None,
            QRhiSampler::ClampToEdge,
            QRhiSampler::ClampToEdge);
        if (!sampler->create()) {
            std::cerr << "[VulkanViewportWidget] Failed to create sampler" << std::endl;
        }
    }

    const qreal dpr = devicePixelRatio();
    runtimeState.width.store(static_cast<uint32_t>(width() * dpr), std::memory_order_release);
    runtimeState.height.store(static_cast<uint32_t>(height() * dpr), std::memory_order_release);
    runtimeState.lastResizeEventNs.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
        std::memory_order_release);

    ensureAppInitialized();
}

void VulkanViewportWidget::render(QRhiCommandBuffer* cb) {
    if (ensureAppInitialized()) {
        const float deltaSeconds = frameTimer.isValid()
            ? static_cast<float>(frameTimer.restart()) / 1000.0f
            : (1.0f / 60.0f);
        app->tickFrame(deltaSeconds);

        const AppViewportOutput output = app->getViewportOutput();
        const uint64_t previousHandle = activeTextureHandle;
        if (ensureImportedTexture(output) && previousHandle != activeTextureHandle && srb) {
            pipelineReady = false;
        }
    }

    const QSize outputSize = colorTexture()->pixelSize();
    const float outputW = static_cast<float>(outputSize.width());
    const float outputH = static_cast<float>(outputSize.height());

    cb->beginPass(renderTarget(), viewportClearColor, { 1.0f, 0 });
    const bool hasScene = activeTexture && sourceWidth > 0 && sourceHeight > 0;
    if (hasScene && ensurePipeline() && updateShaderBindings(activeTexture)) {
        cb->setGraphicsPipeline(pipeline);
        const float srcAspect = static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight);
        const float dstAspect = outputW / outputH;
        float vpX = 0.0f;
        float vpY = 0.0f;
        float vpW = outputW;
        float vpH = outputH;
        if (srcAspect > dstAspect) {
            vpH = outputW / srcAspect;
            vpY = (outputH - vpH) * 0.5f;
        } else {
            vpW = outputH * srcAspect;
            vpX = (outputW - vpW) * 0.5f;
        }

        cb->setShaderResources(srb);
        cb->setViewport(QRhiViewport(vpX, vpY, vpW, vpH));
        cb->draw(3);
    }

    cb->endPass();
    update();
}

void VulkanViewportWidget::resizeEvent(QResizeEvent* event) {
    QRhiWidget::resizeEvent(event);

    const qreal dpr = devicePixelRatio();
    const int physW = static_cast<int>(width() * dpr);
    const int physH = static_cast<int>(height() * dpr);

    runtimeState.width.store(static_cast<uint32_t>(physW), std::memory_order_release);
    runtimeState.height.store(static_cast<uint32_t>(physH), std::memory_order_release);
    runtimeState.resizeSequence.fetch_add(1, std::memory_order_acq_rel);
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    runtimeState.lastResizeEventNs.store(nowNs, std::memory_order_release);
}

void VulkanViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (!event || event->isAutoRepeat()) {
        QRhiWidget::keyPressEvent(event);
        return;
    }

    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (key == Qt::Key_Shift) {
        runtimeState.shiftPressed.store(true, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Key;
    inputEvent.key = static_cast<int>(key);
    inputEvent.pressed = true;
    runtimeState.pushInputEvent(inputEvent);

    QRhiWidget::keyPressEvent(event);
}

void VulkanViewportWidget::keyReleaseEvent(QKeyEvent* event) {
    if (!event || event->isAutoRepeat()) {
        QRhiWidget::keyReleaseEvent(event);
        return;
    }

    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (key == Qt::Key_Shift) {
        runtimeState.shiftPressed.store(false, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Key;
    inputEvent.key = static_cast<int>(key);
    inputEvent.pressed = false;
    runtimeState.pushInputEvent(inputEvent);

    QRhiWidget::keyReleaseEvent(event);
}

void VulkanViewportWidget::mousePressEvent(QMouseEvent* event) {
    if (!event) {
        QRhiWidget::mousePressEvent(event);
        return;
    }

    const qreal dpr = devicePixelRatio();
    const float x = static_cast<float>(event->position().x() * dpr);
    const float y = static_cast<float>(event->position().y() * dpr);
    runtimeState.mouseX.store(x, std::memory_order_release);
    runtimeState.mouseY.store(y, std::memory_order_release);

    if (event->button() == Qt::MiddleButton) {
        runtimeState.middleButtonPressed.store(true, std::memory_order_release);
    }

    if (event->button() == Qt::LeftButton) {
        WindowInputEvent inputEvent{};
        inputEvent.type = WindowInputEventType::MousePress;
        inputEvent.button = static_cast<int>(event->button());
        inputEvent.x = x;
        inputEvent.y = y;
        inputEvent.shiftPressed = (event->modifiers() & Qt::ShiftModifier) != 0;
        runtimeState.pushInputEvent(inputEvent);
    }

    QRhiWidget::mousePressEvent(event);
}

void VulkanViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!event) {
        QRhiWidget::mouseReleaseEvent(event);
        return;
    }

    const qreal dpr = devicePixelRatio();
    const float x = static_cast<float>(event->position().x() * dpr);
    const float y = static_cast<float>(event->position().y() * dpr);
    runtimeState.mouseX.store(x, std::memory_order_release);
    runtimeState.mouseY.store(y, std::memory_order_release);

    if (event->button() == Qt::MiddleButton) {
        runtimeState.middleButtonPressed.store(false, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseRelease;
    inputEvent.button = static_cast<int>(event->button());
    inputEvent.x = x;
    inputEvent.y = y;
    runtimeState.pushInputEvent(inputEvent);

    QRhiWidget::mouseReleaseEvent(event);
}

void VulkanViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!event) {
        QRhiWidget::mouseMoveEvent(event);
        return;
    }

    const qreal dpr = devicePixelRatio();
    const float x = static_cast<float>(event->position().x() * dpr);
    const float y = static_cast<float>(event->position().y() * dpr);
    runtimeState.mouseX.store(x, std::memory_order_release);
    runtimeState.mouseY.store(y, std::memory_order_release);

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseMove;
    inputEvent.x = x;
    inputEvent.y = y;
    runtimeState.pushInputEvent(inputEvent);

    QRhiWidget::mouseMoveEvent(event);
}

void VulkanViewportWidget::wheelEvent(QWheelEvent* event) {
    if (!event) {
        QRhiWidget::wheelEvent(event);
        return;
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Scroll;
    inputEvent.xOffset = event->angleDelta().x() / 120.0;
    inputEvent.yOffset = event->angleDelta().y() / 120.0;
    runtimeState.pushInputEvent(inputEvent);

    QRhiWidget::wheelEvent(event);
}
