#pragma once

#include "contact/ContactTypes.hpp"
#include <vulkan/vulkan.h>

#include <utility>
#include <vector>

class ContactCouplingRuntime {
public:
    void setCoupling(
        ContactCoupling updatedCoupling,
        VkBuffer updatedContactPairBuffer,
        VkDeviceSize updatedContactPairBufferOffset,
        std::vector<ContactLineVertex> updatedOutlineVertices,
        std::vector<ContactLineVertex> updatedCorrespondenceVertices);
    void clear();

    bool isValid() const;
    const ContactCoupling* getContactCoupling() const { return isValid() ? &coupling : nullptr; }
    VkBuffer getContactPairBuffer() const { return contactPairBuffer; }
    VkDeviceSize getContactPairBufferOffset() const { return contactPairBufferOffset; }
    const std::vector<ContactLineVertex>& getOutlineVertices() const { return outlineVertices; }
    const std::vector<ContactLineVertex>& getCorrespondenceVertices() const { return correspondenceVertices; }

private:
    ContactCoupling coupling{};
    VkBuffer contactPairBuffer = VK_NULL_HANDLE; 
    VkDeviceSize contactPairBufferOffset = 0;
    std::vector<ContactLineVertex> outlineVertices;
    std::vector<ContactLineVertex> correspondenceVertices;
};
