#ifndef PCR_COMMONS_HOST_DEVICE
#define PCR_COMMONS_HOST_DEVICE

#ifdef __cplusplus
#include <glm/glm.hpp>
// GLSL Type
using vec2 = glm::vec2;
using ivec3 = glm::ivec3;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4; 
using uvec4 = glm::uvec4;
using ivec2 = glm::ivec2;
using uint = unsigned int;
#define ALIGN16 alignas(16)
#else
#define ALIGN16
#endif

// only contains the main camera info and the address to the
// more specific info buffer containing specific settings and
// output buffer addresses
struct PC{
    mat4     cam_matrix; // this is the view_projection_matrix
    uint     size_x;
    uint     size_y;
    uint64_t info_addr;
};

const uint shader_atomic_size_x = 1024;
struct ShaderAtomic{
    // settings
    uint    point_count;

    // output buffers
    uint64_t positions_addr;
    uint64_t colors_addr;
    uint64_t depth_image_addr;
};
    
#endif