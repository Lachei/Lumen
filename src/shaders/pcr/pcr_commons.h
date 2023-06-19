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
using uvec2 = glm::uvec2;
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
    mat4     cam_view_inv;
    mat4     cam_proj_inv;
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


const uvec2 hash_map_size_xy = uvec2(32, 32);
struct HashMapConstants{
    // settings/infos
    vec3     bounds_min; // minimum world bounding box (to know where to start)
    vec3     bounds_max;
    float    d;

    // output buffers
    uint64_t positions_addr;
    uint64_t colors_addr;
    uint64_t depth_image_addr;
};
    
const uint occupancy_size = 2;
struct HashMapEntry{
    ivec3 key;      // needed to check if block is correct
    uint occupancy[2]; // occupancy for 32 blocks
};

ivec3 bucket_pos(vec3 p, float d){
    return ivec3(p / d);
}

uint hash(ivec3 b){
    const int p1 = 31, p2 = 37, p3 = 41, p4 = 43;
    int h = (((((p1 + b.x) * p2) + b.y) * p3) + b.z) * p4;
#ifdef __cplusplus
    return reinterpret_cast<uint&>(h);
#else
    return h;
#endif
}

uint hash_p(vec3 p, float d){
    return hash(bucket_pos(p, d));
}

#endif