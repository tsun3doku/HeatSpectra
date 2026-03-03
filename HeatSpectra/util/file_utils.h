#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <fstream>
#include <vector>
#include <cstring>
#include <string>

bool readFile(const std::string& filename, std::vector<char>& outBuffer);
std::vector<char> readFile(const std::string& filename);

#endif 
