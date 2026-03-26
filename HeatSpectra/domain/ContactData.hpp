#pragma once

#include "contact/ContactTypes.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"

#include <cstdint>
#include <vector>

enum class ContactPairRole : uint8_t {
    Source = 0,
    Receiver = 1
};

struct ContactPairEndpoint {
    ContactPairRole role = ContactPairRole::Receiver;
    NodeDataHandle payloadHandle{};
    NodeDataHandle geometryHandle{};
};

struct ContactPairData {
    bool hasValidContact = false;
    ContactCouplingType kind = ContactCouplingType::SourceToReceiver;
    ContactPairEndpoint endpointA{};
    ContactPairEndpoint endpointB{};

    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;
    NodeDataHandle contactPairsHandle{};
};

struct ContactBindingData {
    ContactPairData pair{};
};

struct ContactData {
    std::vector<ContactBindingData> bindings;
    bool active = false;

    std::size_t size() const {
        return bindings.size();
    }
};
