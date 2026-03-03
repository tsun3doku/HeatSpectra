#pragma once

#include <QRhiWidget>
#include <QElapsedTimer>
#include <QtGui/rhi/qshader.h>
#include <QtGui/rhi/qrhi.h>
#include <cstdint>
#include <unordered_map>

#include "render/WindowRuntimeState.hpp"

class App;
struct AppVulkanContext;
struct AppViewportOutput;
class QRhi;
class QRhiTexture;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiGraphicsPipeline;

class VulkanViewportWidget : public QRhiWidget {
    Q_OBJECT

public:
    explicit VulkanViewportWidget(QWidget* parent = nullptr);
    ~VulkanViewportWidget() override;

    void setApp(App* appPtr);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;

    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void releaseResources();
    void releaseImportedTextures();
    bool buildAppVulkanContext(AppVulkanContext& outContext) const;
    bool initializeAppFromRhi();
    bool ensureAppInitialized();
    bool ensureImportedTexture(const AppViewportOutput& output);
    bool updateShaderBindings(QRhiTexture* sceneTexture);
    bool ensurePipeline();
    QShader loadShader(const QString& path);

    App* app = nullptr;
    bool appInitialized = false;
    QElapsedTimer frameTimer;
    WindowRuntimeState runtimeState{};

    QRhi* currentRhi = nullptr;
    QRhiSampler* sampler = nullptr;
    QRhiShaderResourceBindings* srb = nullptr;
    QRhiGraphicsPipeline* pipeline = nullptr;
    QRhiTexture* activeTexture = nullptr;
    uint64_t activeTextureHandle = 0;
    uint64_t importedGeneration = 0;
    std::unordered_map<uint64_t, QRhiTexture*> importedTextures;

    QShader vertShader;
    QShader fragShader;

    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    bool pipelineReady = false;
};
