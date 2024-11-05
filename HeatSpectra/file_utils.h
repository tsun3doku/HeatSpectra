#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>

std::vector<char> readFile(const std::string& filename);

#endif 