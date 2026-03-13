#pragma once

#include "ContactInterface.hpp"
#include "contact/ContactTypes.hpp"
#include "util/Structs.hpp"

#include <vector>

class Remesher;
class ResourceManager;

class ContactSystem {
public:
    struct Result {
        bool hasContact = false;
        std::vector<ContactPairGPU> pairs;
        std::vector<ContactInterface::ContactLineVertex> outlineVertices;
        std::vector<ContactInterface::ContactLineVertex> correspondenceVertices;
    };

    ContactSystem(ResourceManager& resourceManager, Remesher& remesher);

    bool compute(const ConfiguredContactPair& pair, Result& outResult, bool forceRebuild = false);
    void clearCache();

private:
    struct CachedResult {
        ConfiguredContactPair pair{};
        Result result{};
    };

    CachedResult* findCachedResult(const ConfiguredContactPair& pair);
    static bool hasUsableContactPairs(const std::vector<ContactPairGPU>& pairs);

    ResourceManager& resourceManager;
    ContactInterface contactInterface;
    std::vector<CachedResult> cachedResults;
};
