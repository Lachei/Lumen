#pragma once
#include <string_view>
#include <vector>

// loads and creates the data for the environment texture
class EnvMap{
public:
    EnvMap(std::string_view texture_path);
    std::vector<uint8_t>               image_data;      // contains the image data
    std::vector<std::vector<uint16_t>> importance_mips; // contains teh prefix sums
    int width, height;
};