#include "ContactSystemRuntime.hpp"

#include "ContactSystemController.hpp"

#include <vector>

void ContactSystemRuntime::clearResolvedContacts() {
    resolvedContacts.clear();
}

void ContactSystemRuntime::rebuildResolvedContacts(
    ContactSystemController* contactSystemController,
    const std::vector<RuntimeContactBinding>& configuredContacts) {
    clearResolvedContacts();
    if (!contactSystemController) {
        return;
    }

    for (const RuntimeContactBinding& request : configuredContacts) {
        if (!request.contactPair.hasValidContact ||
            request.emitterRuntimeModelId == 0 ||
            request.receiverRuntimeModelId == 0 ||
            request.emitterRuntimeModelId == request.receiverRuntimeModelId) {
            continue;
        }

        std::vector<ContactPair> pairs;
        if (!contactSystemController->computePairs(request.payloadPair, pairs, false) ||
            pairs.empty()) {
            continue;
        }

        RuntimeContactResult resolvedContact{};
        resolvedContact.binding = request;
        resolvedContact.contactPairsCPU = std::move(pairs);
        resolvedContacts.push_back(std::move(resolvedContact));
    }
}
