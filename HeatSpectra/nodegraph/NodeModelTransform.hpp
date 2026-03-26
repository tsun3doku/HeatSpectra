#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>

namespace NodeModelTransform {

std::array<float, 16> identityMatrixArray();
glm::mat4 toMat4(const std::array<float, 16>& values);
std::array<float, 16> toMatrixArray(const glm::mat4& matrix);
std::string serializeLocalToWorld(const std::array<float, 16>& values);
bool tryParseLocalToWorld(const std::string& serialized, std::array<float, 16>& outValues);

}
