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
class UniformBufferManager;
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
        MemoryAllocator& memoryAllocator,
        UniformBufferManager& uniformBufferManager,
        uint32_t maxFramesInFlight);
    ~ContactSystemComputeController();

    void createContactSystem(VkExtent2D extent, VkRenderPass renderPass);
    void updateRenderContext(VkExtent2D extent, VkRenderPass renderPass);
    void updateRenderResources();
    void configure(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();
    bool exportProduct(uint64_t socketKey, ContactProduct& outProduct);
    ContactSystem* getContactSystem(uint64_t socketKey) const;
    std::vector<ContactSystem*> getActiveSystems() const;

private:
    std::unique_ptr<ContactSystem> buildContactSystem(VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
    uint32_t maxFramesInFlight = 0;
    VkExtent2D currentExtent{ 0, 0 };
    VkRenderPass currentRenderPass = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, std::unique_ptr<ContactSystem>> contactSystems;
};
