#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera;
class Model;

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

class MousePicker {
public:
    // Convert screen coordinates to world-space ray
    static Ray screenToWorldRay(float mouseX, float mouseY, uint32_t screenWidth, uint32_t screenHeight,
                                 const glm::mat4& view, const glm::mat4& proj);
    
    // Check if ray intersects with model's bounding box (simple AABB test)
    static bool rayIntersectsAABB(const Ray& ray, const glm::vec3& aabbMin, const glm::vec3& aabbMax);
    
    // Check if ray intersects with a sphere (for simpler hit testing)
    static bool rayIntersectsSphere(const Ray& ray, const glm::vec3& center, float radius);
};
