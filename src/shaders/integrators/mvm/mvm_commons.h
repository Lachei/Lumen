#ifndef MVM_COMMONS_HOST_DEVICE
#define MVM_COMMONS_HOST_DEVICE

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
using i16vec3 = glm::i16vec3;
using uint = uint32_t;
#define floor glm::floor
#define ALIGN16 alignas(16)
#define INLINE inline
#define INOUT_OCCUPANCY OccupancyEntry&
#define IN_OCCUPANCY const OccupancyEntry&
#define INOUT_SKIP EmptySkipEntry&
#define IN_SKIP const EmptySkipEntry&
#define bit_count(x) std::popcount(x)
#else
#define ALIGN16
#define INLINE
#define INOUT_OCCUPANCY inout OccupancyEntry
#define IN_OCCUPANCY in OccupancyEntry
#define INOUT_SKIP inout EmptySkipEntry
#define IN_SKIP in EmptySkipEntry
#define bit_count(x) bitCount(x)
#define assert(x)
#endif

// ----------------------------------------------------------------
// constant settings
// ----------------------------------------------------------------
const uint workgroup_size_x = 32;
const uint workgroup_size_y = 32;

// ----------------------------------------------------------------
// structures
// ----------------------------------------------------------------
struct PC{
    mat4    cam_view_inv;
    mat4    cam_proj_inv;
    uint    size_x;
    uint    size_y;
    uint    amt_multi_views;
    uint    frame_number;
    uint64_t multi_view_infos_addr; // matrix infos of the cameras
};

struct MultiViewInfo{
    mat4 cam_view_proj;
    vec4 cam_origin;
    uint size_x;
    uint size_y;
    uint depth_texture_index;
    uint color_texture_index;
};

#endif