#pragma once

#include "heat/ContactInterface.hpp"
#include "ContactTypes.hpp"

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
    bool compute(const ContactPairPayloadConfig& pair, Result& outResult);

private:
    static bool hasUsableContactPairs(const std::vector<ContactPair>& pairs);

    ContactInterface contactInterface;
};
