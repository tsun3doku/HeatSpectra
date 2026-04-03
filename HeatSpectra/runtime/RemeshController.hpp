#pragma once

#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "domain/RemeshParams.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

class RenderRuntime;
class ResourceManager;
class SceneController;
class VulkanDevice;

class RemeshController {
public:
    struct Config {
        uint64_t socketKey = 0;
        GeometryData sourceGeometry;
        RemeshParams params{};
        NodeDataHandle remeshHandle{};
    };

    RemeshController(
        Remesher& remesher,
        VulkanDevice& vulkanDevice,
        ResourceManager& resourceManager,
        RenderRuntime& renderRuntime,
        std::atomic<bool>& isOperating);

    void setSceneController(SceneController* updatedSceneController);

    void configure(const Config& config);
    void disable(uint64_t socketKey);
    void disable();
    bool exportProduct(uint64_t socketKey, RemeshProduct& outProduct) const;

private:
    struct ActiveState {
        Config config{};
        GeometryData geometry{};
        std::vector<glm::vec3> geometryPositions;
        std::vector<uint32_t> geometryTriangleIndices;
        SupportingHalfedge::IntrinsicMesh intrinsicMesh;
        SupportingHalfedge::GPUResources intrinsicGpuResources;
        uint32_t runtimeModelId = 0;
        bool sinkOwned = false;
    };

    class OperatingScope {
    public:
        explicit OperatingScope(std::atomic<bool>& isOperating);
        ~OperatingScope();

    private:
        std::atomic<bool>& isOperating;
        bool previousState = false;
    };

    uint32_t materializeRuntimeModelSink(const GeometryData& geometry) const;
    void cleanupGpuResources(SupportingHalfedge::GPUResources& resources) const;
    void disableSocket(uint64_t socketKey, bool updateFocus);
    void flushRetiredStates(bool updateFocus);

    VulkanDevice& vulkanDevice;
    ResourceManager& resourceManager;
    RenderRuntime& renderRuntime;
    std::atomic<bool>& isOperating;
    Remesher& remesher;
    SceneController* sceneController = nullptr;
    std::unordered_map<uint64_t, ActiveState> activeStatesBySocket;
    std::vector<ActiveState> retiredStates;
};
