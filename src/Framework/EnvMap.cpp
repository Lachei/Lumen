#include "EnvMap.h"

#include <stb_image/stb_image.h>
#include <LumenPCH.h>
#include "VkUtils.h"
#include <ranges>
#include <span>

#define LUMINANCE(r, g, b) (r * 0.2126f + g * 0.7152f + b * 0.0722f)

inline bool power_of_two(int n){ return (n & (n - 1)) == 0;}
inline std::vector<uint16_t> convert_to_u16(std::span<const float> vals){
    std::vector<uint16_t> r(vals.size());
    for(size_t i: std::views::iota(size_t(0), vals.size()))
        r[i] = static_cast<uint16_t>(vals[i] * 0xffff);
    return r;
}

EnvMap::EnvMap(std::string_view texture_path){
    int n;
	uint8_t* data = stbi_load(texture_path.data(), &width, &height, &n, 4);
    // create a vector with the data
    image_data = std::vector(data, data + width * height * n);

    // create the prefix sum on the cpu on load up
    constexpr int map_factor = 8;
    constexpr int float_per_hdr_rgba = 4;
    if(!power_of_two(width) || !power_of_two(height))
        LUMEN_ERROR("Width or height of environment map are not powers of two. Currently only aligned images are supported");
    
    
    int importance_size_x = width / map_factor;
	int importance_size_y = height / map_factor;

    // importance data is in the range from 0-1 and contains the averaged luminance values for a smaller texture
    std::vector<float> importance_data(importance_size_x * importance_size_y);

    for (int i = 0; i < importance_size_y; i++) {
		for (int j = 0; j < importance_size_x; j++) {

			int region_start_index = (j + i * width) * map_factor; // index in element range
			float temp_imp = 0;

			for (int y = 0; y < map_factor; y++) {
				for (int x = 0; x < map_factor; x++) {
					
					int element_start_index = (region_start_index + x + y * width) * float_per_hdr_rgba;
					float r = data[element_start_index]     / 255.f;
					float g = data[element_start_index + 1] / 255.f;
					float b = data[element_start_index + 2] / 255.f;
					
					temp_imp += LUMINANCE(r, g, b);
				}
			}
			importance_data[j + importance_size_x * i] = temp_imp / (map_factor * map_factor);
		}
	}

    int mip_map_levels = calc_mip_levels({width, height});

	importance_data.resize(mip_map_levels);

    importance_mips[0] = convert_to_u16(importance_data);
	
	int prev_width = importance_size_x;
	importance_size_x = importance_size_x > 1 ? importance_size_x / 2 : 1;
	importance_size_y = importance_size_y > 1 ? importance_size_y / 2 : 1;

	std::vector<std::vector<float>> mips(mip_map_levels - 1);

	mips[0] = std::vector<float>(importance_size_x * importance_size_y);
	for (int y = 0; y < importance_size_y; y++) {
		for (int x = 0; x < importance_size_x; x++) {

			int region_start_index = 2*x + 2*y*prev_width;
			float temp_imp = importance_data[region_start_index];
			temp_imp += importance_data[region_start_index + 1];
			temp_imp += importance_data[region_start_index + prev_width];
			temp_imp += importance_data[region_start_index + prev_width + 1];
			mips[0][x + y * importance_size_x] = temp_imp / 4;
		}
	}

	prev_width = importance_size_x;
	int prev_height = importance_size_y;
	importance_size_x = importance_size_x > 1 ? importance_size_x / 2 : 1;
	importance_size_y = importance_size_y > 1 ? importance_size_y / 2 : 1;			
	

	for (int i = 1; i < mips.size(); i++) {

		mips[i] = std::vector<float>(importance_size_x * importance_size_y);

		for (int y = 0; y < importance_size_y; y++) {
			for (int x = 0; x < importance_size_x; x++) {
				if (prev_width == 1) {
					int region_start_index = 2 * x + 2 * y * prev_width;
					float temp_imp = mips[i - 1][region_start_index];
					temp_imp += mips[i - 1][region_start_index + prev_width];
					mips[i][x + y * importance_size_x] = temp_imp / 2;
					
				}
				else if (prev_height == 1) {
					int region_start_index = 2 * x + 2 * y * prev_width;
					float temp_imp = mips[i - 1][region_start_index];
					temp_imp += mips[i - 1][region_start_index + 1];
					mips[i][x + y * importance_size_x] = temp_imp / 2;
					
				} else {
					int region_start_index = 2 * x + 2 * y * prev_width;
					float temp_imp = mips[i-1][region_start_index];
					temp_imp += mips[i - 1][region_start_index + 1];
					temp_imp += mips[i - 1][region_start_index + prev_width];
					temp_imp += mips[i - 1][region_start_index + prev_width + 1];
					mips[i][x + y * importance_size_x] = temp_imp / 4;
				}
			}
		}

		prev_width = importance_size_x;
		prev_height = importance_size_y;
		importance_size_x = importance_size_x > 1 ? importance_size_x / 2 : 1;
		importance_size_y = importance_size_y > 1 ? importance_size_y / 2 : 1;
	}

    for(int i: std::ranges::iota_view(0, mip_map_levels))
        importance_mips[i + 1] = convert_to_u16(mips[i]);

    // stbi stuff ...
    stbi_image_free(data);
}
