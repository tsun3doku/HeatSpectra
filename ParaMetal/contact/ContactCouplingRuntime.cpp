#include "ContactCouplingRuntime.hpp"

void ContactCouplingRuntime::setCoupling(
    ContactCoupling updatedCoupling,
    VkBuffer updatedContactPairBuffer,
    VkDeviceSize updatedContactPairBufferOffset,
    std::vector<ContactLineVertex> updatedOutlineVertices,
    std::vector<ContactLineVertex> updatedCorrespondenceVertices) {
    coupling = std::move(updatedCoupling);
    contactPairBuffer = updatedContactPairBuffer;
    contactPairBufferOffset = updatedContactPairBufferOffset;
    outlineVertices = std::move(updatedOutlineVertices);
    correspondenceVertices = std::move(updatedCorrespondenceVertices);
}

void ContactCouplingRuntime::clear() {
    coupling = {};
    contactPairBuffer = VK_NULL_HANDLE;
    contactPairBufferOffset = 0;
    outlineVertices.clear();
    correspondenceVertices.clear();
}

bool ContactCouplingRuntime::isValid() const {
    return coupling.isValid() && contactPairBuffer != VK_NULL_HANDLE;
}
