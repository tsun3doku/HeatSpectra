#pragma once

#include "runtime/RuntimeContactTypes.hpp"

#include <vector>

class ContactSystemController;

class ContactSystemRuntime {
public:
    const std::vector<RuntimeContactResult>& getResolvedContacts() const { return resolvedContacts; }

    void clearResolvedContacts();
    void rebuildResolvedContacts(
        ContactSystemController* contactSystemController,
        const std::vector<RuntimeContactBinding>& configuredContacts);

private:
    std::vector<RuntimeContactResult> resolvedContacts;
};
