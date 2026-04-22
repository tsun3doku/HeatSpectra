#pragma once

#include "contact/ContactTypes.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class MemoryAllocator;
class VulkanDevice;
class ContactSystem;

class ContactSystemComputeController {
public:
    struct Config {
        ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
        float minNormalDot = -0.65f;
        float contactRadius = 0.01f;
        uint32_t emitterModelId = 0;
        std::array<float, 16> emitterLocalToWorld{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        SupportingHalfedge::IntrinsicMesh emitterIntrinsicMesh;
        uint32_t receiverModelId = 0;
        std::array<float, 16> receiverLocalToWorld{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        SupportingHalfedge::IntrinsicMesh receiverIntrinsicMesh;
        uint32_t emitterRuntimeModelId = 0;
        uint32_t receiverRuntimeModelId = 0;
        std::vector<uint32_t> receiverTriangleIndices;
        uint64_t computeHash = 0;

        bool isValid() const {
            return emitterRuntimeModelId != 0 &&
                receiverRuntimeModelId != 0 &&
                emitterRuntimeModelId != receiverRuntimeModelId &&
                emitterModelId != 0 &&
                receiverModelId != 0 &&
                !emitterIntrinsicMesh.vertices.empty() &&
                !receiverIntrinsicMesh.vertices.empty() &&
                !receiverTriangleIndices.empty();
        }
    };

    ContactSystemComputeController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator);
    ~ContactSystemComputeController();

    void configure(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();
    bool exportProduct(uint64_t socketKey, ContactProduct& outProduct);

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    std::unordered_map<uint64_t, std::unique_ptr<ContactSystem>> activeSystems;
    std::unordered_map<uint64_t, Config> configuredConfigs;
};

inline uint64_t buildComputeHash(const ContactSystemComputeController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint32_t>(config.couplingType));
    hash = RuntimeProductHash::mixPod(hash, config.minNormalDot);
    hash = RuntimeProductHash::mixPod(hash, config.contactRadius);
    hash = RuntimeProductHash::mixPod(hash, config.emitterModelId);
    hash = RuntimeProductHash::mixPod(hash, config.emitterLocalToWorld);
    hash = RuntimeProductHash::mixPodVector(hash, config.emitterIntrinsicMesh.vertices);
    hash = RuntimeProductHash::mixPodVector(hash, config.emitterIntrinsicMesh.indices);
    hash = RuntimeProductHash::mixPodVector(hash, config.emitterIntrinsicMesh.faceIds);
    hash = RuntimeProductHash::mixPodVector(hash, config.emitterIntrinsicMesh.triangles);
    hash = RuntimeProductHash::mixPod(hash, config.receiverModelId);
    hash = RuntimeProductHash::mixPod(hash, config.receiverLocalToWorld);
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverIntrinsicMesh.vertices);
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverIntrinsicMesh.indices);
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverIntrinsicMesh.faceIds);
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverIntrinsicMesh.triangles);
    hash = RuntimeProductHash::mixPod(hash, config.emitterRuntimeModelId);
    hash = RuntimeProductHash::mixPod(hash, config.receiverRuntimeModelId);
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverTriangleIndices);
    return hash;
}