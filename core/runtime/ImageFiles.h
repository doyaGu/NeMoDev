#pragma once
#include <string>
#include <vector>
namespace vdev {
bool WriteBgraPng(const std::string &, int width, int height, const unsigned char *pixels);
bool ReadImageRgba(const std::string &, std::vector<unsigned char> &, int &width, int &height);
bool DecodeImageRgba(const std::vector<unsigned char> &, std::vector<unsigned char> &, int &width, int &height);
} // namespace vdev
