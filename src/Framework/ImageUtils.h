#pragma once
#include <string_view>

float* load_exr(const char* img_name, int& width, int& height);
void save_exr(const float* rgb, std::string_view components, int width, int height, const char* outfilename);
