#pragma once

#include "domain/GeometryData.hpp"
#include "domain/RemeshData.hpp"
#include "runtime/RuntimeContactTypes.hpp"
#include "runtime/RuntimePackages.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class CommandPool;
class HeatSourceRuntime;
class MemoryAllocator;
class VulkanDevice;

class HeatSystemRuntime {
public:
    HeatSystemRuntime() = default;
    ~HeatSystemRuntime() = default;

    struct SourceBinding {
        GeometryPackage geometryPackage{};
        std::unique_ptr<HeatSourceRuntime> heatSource;
    };

    const SourceBinding* findBaseSourceBinding() const;

    void initializeModelBindings(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& renderCommandPool,
        const HeatPackage& heatPackage);

    std::vector<SourceBinding>& getSourceBindingsMutable() { return sourceBindings; }

    void cleanupModelBindings();

private:
    std::vector<SourceBinding> sourceBindings;
};
