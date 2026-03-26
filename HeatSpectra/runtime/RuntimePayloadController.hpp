#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "domain/RemeshData.hpp"
#include "runtime/RuntimePackageCompiler.hpp"

class CameraController;
class ContactSystemController;
class HeatSystemController;
class NodePayloadRegistry;
class RenderRuntime;
class ResourceManager;
class RuntimeIntrinsicCache;
class SceneController;
class SwapchainManager;
class VulkanDevice;
class VoronoiSystemController;

class RuntimePayloadController {
public:
    RuntimePayloadController(
        VulkanDevice& vulkanDevice,
        SwapchainManager& swapchainManager,
        ResourceManager& resourceManager,
        RuntimeIntrinsicCache& runtimeIntrinsicCache,
        RenderRuntime& renderRuntime,
        std::atomic<bool>& isOperating);

    void setHeatSystemController(HeatSystemController* heatSystemController);
    void setContactSystemController(ContactSystemController* contactSystemController);
    void setVoronoiSystemController(VoronoiSystemController* voronoiSystemController);
    void setSceneController(SceneController* sceneController);
    bool resolveRuntimeModel(const GeometryData& geometry, uint32_t& outRuntimeModelId) const;
    void applyGeometryPayload(const GeometryData& geometry);
    void removeGeometryPayload(uint32_t nodeModelId);
    void applyRemeshPayload(const GeometryData& geometry, const IntrinsicMeshData& intrinsic);
    void removeIntrinsicPayload(const NodeDataHandle& intrinsicHandle);
    void rebuildHeatSystemSinks();
    void applyHeatPayload(const NodePayloadRegistry& payloadRegistry, const HeatData& heat);
    void applyHeatPayload(const HeatData& heat);
    void applyContactPayload(const NodePayloadRegistry& payloadRegistry, const ContactData& contact);
    void applyContactPayload(const ContactData& contact);
    void applyVoronoiPayload(const NodePayloadRegistry& payloadRegistry, const VoronoiData& voronoi);
    void applyVoronoiPayload(const VoronoiData& voronoi);

private:
    class OperatingScope {
    public:
        explicit OperatingScope(std::atomic<bool>& isOperating);
        ~OperatingScope();

    private:
        std::atomic<bool>& isOperating;
        bool previousState = false;
    };

    static bool areVoronoiPackagesEquivalent(const VoronoiPackage& lhs, const VoronoiPackage& rhs);
    uint32_t materializeRuntimeModelSink(const GeometryData& geometry);
    void applyHeatPackageToSystem();
    void applyVoronoiPackageToSystem();

    VulkanDevice& vulkanDevice;
    SwapchainManager& swapchainManager;
    ResourceManager& resourceManager;
    RuntimeIntrinsicCache& runtimeIntrinsicCache;
    RenderRuntime& renderRuntime;
    std::atomic<bool>& isOperating;
    RuntimePackageCompiler packageCompiler;
    std::unordered_map<uint32_t, GeometryPackage> geometryPackagesByNodeModelId;
    HeatPackage heatPackage;
    ContactPackage contactPackage;
    VoronoiPackage voronoiPackage;
    bool hasProjectedHeatPayload = false;
    ContactSystemController* contactSystemController = nullptr;
    HeatSystemController* heatSystemController = nullptr;
    VoronoiSystemController* voronoiSystemController = nullptr;
    SceneController* sceneController = nullptr;
};
