#pragma once

#include <cstdint>
#include <vector>

#include "Structs.hpp"

class UniformBufferManager;

class MaterialSystem {
public:
    using MaterialHandle = uint32_t;

    explicit MaterialSystem(UniformBufferManager& uniformBufferManager);

    MaterialHandle createMaterial(const MaterialData& material);
    void setDefaultMaterial(MaterialHandle materialHandle);
    void setDefaultMaterialData(const MaterialData& material);

    const MaterialData& getDefaultMaterial() const;
    MaterialHandle getDefaultMaterialHandle() const { return defaultMaterialHandle; }

    void update(uint32_t frameIndex);

    static MaterialData createDefaultMaterial();

private:
    MaterialUniformBufferObject packMaterial(const MaterialData& material) const;

    UniformBufferManager& uniformBufferManager;
    std::vector<MaterialData> materials;
    MaterialHandle defaultMaterialHandle = 0;
};
