#include "MousePicker.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

Ray MousePicker::screenToWorldRay(float mouseX, float mouseY, uint32_t screenWidth, uint32_t screenHeight,
                                    const glm::mat4& view, const glm::mat4& proj) {
    // Convert screen coordinates to normalized device coordinates (NDC)
    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight;  // Flip Y axis
    float z = 1.0f;  // Far plane
    
    glm::vec3 rayNDS = glm::vec3(x, y, z);
    
    // Convert to clip space
    glm::vec4 rayClip = glm::vec4(rayNDS.x, rayNDS.y, -1.0f, 1.0f);
    
    // Convert to eye/view space
    glm::vec4 rayEye = glm::inverse(proj) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    
    // Convert to world space
    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    rayWorld = glm::normalize(rayWorld);
    
    // Ray origin is camera position
    glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]);
    
    return Ray{ cameraPos, rayWorld };
}

bool MousePicker::rayIntersectsAABB(const Ray& ray, const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    
    for (int i = 0; i < 3; i++) {
        if (std::abs(ray.direction[i]) < 0.0001f) {
            // Ray is parallel to slab
            if (ray.origin[i] < aabbMin[i] || ray.origin[i] > aabbMax[i]) {
                return false;
            }
        } else {
            float t1 = (aabbMin[i] - ray.origin[i]) / ray.direction[i];
            float t2 = (aabbMax[i] - ray.origin[i]) / ray.direction[i];
            
            if (t1 > t2) std::swap(t1, t2);
            
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            
            if (tMin > tMax) {
                return false;
            }
        }
    }
    
    return true;
}

bool MousePicker::rayIntersectsSphere(const Ray& ray, const glm::vec3& center, float radius) {
    glm::vec3 oc = ray.origin - center;
    float a = glm::dot(ray.direction, ray.direction);
    float b = 2.0f * glm::dot(oc, ray.direction);
    float c = glm::dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4 * a * c;
    
    return discriminant >= 0.0f;
}
