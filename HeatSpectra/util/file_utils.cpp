#include "file_utils.h"

#include <iostream>

bool readFile(const std::string& filename, std::vector<char>& outBuffer) {
    outBuffer.clear();

    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "readFile: failed to open file: " << filename << std::endl;
        return false;
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    outBuffer.resize(fileSize);

    file.seekg(0);
    file.read(outBuffer.data(), static_cast<std::streamsize>(fileSize));

    if (!file.good() && !file.eof()) {
        std::cerr << "readFile: failed to read file: " << filename << std::endl;
        outBuffer.clear();
        return false;
    }

    return true;
}

std::vector<char> readFile(const std::string& filename) {
    std::vector<char> buffer;
    readFile(filename, buffer);
    return buffer;
}
