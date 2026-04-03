#pragma once

#include "heat/ContactInterface.hpp"
#include "ContactTypes.hpp"
#include "runtime/RuntimeContactTypes.hpp"

#include <vector>

class ContactSystem {
public:
    struct Result {
        bool hasContact = false;
        std::vector<ContactPair> pairs;
        std::vector<ContactInterface::ContactLineVertex> outlineVertices;
        std::vector<ContactInterface::ContactLineVertex> correspondenceVertices;
    };

    ContactSystem() = default;
    bool compute(const RuntimeContactPairConfig& pair, Result& outResult);

    ContactInterface contactInterface;
};
