#include "MaterialSystem.hpp"

#include "UniformBufferManager.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

MaterialSystem::MaterialSystem(UniformBufferManager& uniformBufferManager)
    : uniformBufferManager(uniformBufferManager) {
    materials.reserve(8);
    defaultMaterialHandle = createMaterial(createDefaultMaterial());
}

MaterialSystem::MaterialHandle MaterialSystem::createMaterial(const MaterialData& material) {
    materials.push_back(material);
    return static_cast<MaterialHandle>(materials.size() - 1);
}

void MaterialSystem::setDefaultMaterial(MaterialHandle materialHandle) {
    if (materialHandle >= materials.size()) {
        throw std::out_of_range("MaterialSystem::setDefaultMaterial received invalid handle");
    }
    defaultMaterialHandle = materialHandle;
}

void MaterialSystem::setDefaultMaterialData(const MaterialData& material) {
    if (defaultMaterialHandle >= materials.size()) {
        throw std::runtime_error("MaterialSystem has no default material");
    }
    materials[defaultMaterialHandle] = material;
}

const MaterialData& MaterialSystem::getDefaultMaterial() const {
    if (defaultMaterialHandle >= materials.size()) {
        throw std::runtime_error("MaterialSystem has no default material");
    }
    return materials[defaultMaterialHandle];
}

void MaterialSystem::update(uint32_t frameIndex) {
    const auto& mappedBuffers = uniformBufferManager.getMaterialBuffersMapped();
    if (frameIndex >= mappedBuffers.size() || mappedBuffers[frameIndex] == nullptr) {
        return;
    }

    const MaterialUniformBufferObject packed = packMaterial(getDefaultMaterial());
    std::memcpy(mappedBuffers[frameIndex], &packed, sizeof(packed));
}

MaterialData MaterialSystem::createDefaultMaterial() {
    MaterialData material{};
    material.baseColor = glm::vec3(0.62f, 0.64f, 0.66f);
    material.roughness = 0.40f;
    material.specularF0 = 0.035f;
    return material;
}

MaterialUniformBufferObject MaterialSystem::packMaterial(const MaterialData& material) const {
    MaterialUniformBufferObject packed{};
    const glm::vec3 clampedColor = glm::clamp(material.baseColor, glm::vec3(0.0f), glm::vec3(1.0f));
    const float roughness = std::clamp(material.roughness, 0.04f, 1.0f);
    const float specularF0 = std::clamp(material.specularF0, 0.0f, 1.0f);

    packed.baseColorRoughness = glm::vec4(clampedColor, roughness);
    packed.specular = glm::vec4(specularF0, 0.0f, 0.0f, 0.0f);
    return packed;
}
