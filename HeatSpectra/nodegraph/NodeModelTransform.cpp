#include "NodeModelTransform.hpp"

#include <charconv>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace {

constexpr std::array<float, 16> IdentityMatrixArray{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

}

namespace NodeModelTransform {

std::array<float, 16> identityMatrixArray() {
    return IdentityMatrixArray;
}

glm::mat4 toMat4(const std::array<float, 16>& values) {
    glm::mat4 matrix(1.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            matrix[column][row] = values[static_cast<std::size_t>(column) * 4 + static_cast<std::size_t>(row)];
        }
    }
    return matrix;
}

std::array<float, 16> toMatrixArray(const glm::mat4& matrix) {
    return {
        matrix[0][0], matrix[0][1], matrix[0][2], matrix[0][3],
        matrix[1][0], matrix[1][1], matrix[1][2], matrix[1][3],
        matrix[2][0], matrix[2][1], matrix[2][2], matrix[2][3],
        matrix[3][0], matrix[3][1], matrix[3][2], matrix[3][3]
    };
}

std::string serializeLocalToWorld(const std::array<float, 16>& values) {
    std::ostringstream stream;
    stream << std::setprecision(9);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }
        stream << values[index];
    }
    return stream.str();
}

bool tryParseLocalToWorld(const std::string& serialized, std::array<float, 16>& outValues) {
    if (serialized.empty()) {
        outValues = identityMatrixArray();
        return true;
    }

    std::array<float, 16> parsedValues{};
    std::size_t valueIndex = 0;
    std::size_t start = 0;
    while (start <= serialized.size() && valueIndex < parsedValues.size()) {
        const std::size_t end = serialized.find(',', start);
        const std::string_view token(
            serialized.data() + start,
            (end == std::string::npos) ? (serialized.size() - start) : (end - start));

        float parsedValue = 0.0f;
        const char* begin = token.data();
        const char* finish = token.data() + token.size();
        const auto result = std::from_chars(begin, finish, parsedValue);
        if (result.ec != std::errc{} || result.ptr != finish) {
            return false;
        }

        parsedValues[valueIndex++] = parsedValue;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    if (valueIndex != parsedValues.size()) {
        return false;
    }

    outValues = parsedValues;
    return true;
}

}
