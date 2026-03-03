#pragma once

#include <glm/glm.hpp>

namespace render {

struct SceneView {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float cameraFov = 45.0f;
};

} 
