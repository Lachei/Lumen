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
using uint = uint32_t;
#define floor glm::floor
#define ALIGN16 alignas(16)
#define INLINE inline
#define INOUT_MAP_ENTRY HashMapEntry&
#define IN_MAP_ENTRY const HashMapEntry&
#else
#define ALIGN16
#define INLINE
#define INOUT_MAP_ENTRY inout HashMapEntry
#define IN_MAP_ENTRY in HashMapEntry
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
const uint box_per_hash_box = 4; // has to be multiple of 4 (base compression block stores 4x4x4)
const uint box_per_hash_box_cube = box_per_hash_box * box_per_hash_box * box_per_hash_box;
const uint uints_per_hash = box_per_hash_box / 4;
const uint cubed_box_per_hash = uints_per_hash * uints_per_hash * uints_per_hash;
const int box_unused = 0x7fffffff;
struct HashMapConstants{
    // settings/infos
    vec3     bounds_min; // minimum world bounding box (to know where to start)
    vec3     bounds_max;
    float    delta_grid; // the delta distance for the underlying grid. The hash grid simply is 
    uint     hash_map_size;
    // output buffers
    uint64_t hash_map_addr;
};
    
const uint occupancy_size = 2;
struct HashMapEntry{
    ivec3 key;      // needed to check if block is correct
    uint occupancy[cubed_box_per_hash][2]; // occupancy for 32 blocks
    uint next;      // index to the next bin
};

#define BIT_CALCS(base_pos, p, d) vec3 rel = p - base_pos;\
    ivec3 index = ivec3(floor(rel / d));\
    ivec3 fxfblock = index / 4;\
    ivec3 residual = index % 4;\
    int lin_block = fxfblock.x * int(uints_per_hash * uints_per_hash) + fxfblock.y * int(uints_per_hash) + fxfblock.z;\
    int bank = int(residual.x >= 2);\
    int bit = (residual.x & 1) * 4 * 4 + residual.y * 4 + residual.z;
 
INLINE void set_bit(INOUT_MAP_ENTRY entry, vec3 base_pos, vec3 p, float d){
    BIT_CALCS(base_pos, p, d);
    entry.occupancy[lin_block][bank] |= 1 << bit;
}

INLINE bool check_bit(IN_MAP_ENTRY entry, vec3 base_pos, vec3 p, float d){
    BIT_CALCS(base_pos, p, d);
    return (entry.occupancy[lin_block][bank] & (1 << bit)) != 0;
}
#undef BIT_CALCS

INLINE ivec3 bucket_pos(vec3 p, float d){
    d *= box_per_hash_box;
    return ivec3(floor(p / d));
}

INLINE vec3 bucket_base(ivec3 bucket, float d){
    d *= box_per_hash_box;
    return vec3(bucket) * d;
}

INLINE uint hash_int(int i){
    uint x = uint(i);
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

INLINE uint hash(ivec3 b){
    int p1 = 73856093, p2 = 19349669, p3 = 83492791;
    //int h = ((p1 * b.x) ^ (b.y * p2) ^ (b.z * p3));
    uint h = (hash_int(b.x) << 2) ^ (hash_int(b.y) << 1) ^ hash_int(b.z);
#ifdef __cplusplus
    return reinterpret_cast<uint&>(h);
#else
    return uint(h);
#endif
}

INLINE uint hash_p(vec3 p, float d){
    return hash(bucket_pos(p, d));
}

INLINE uint hash_table_index(uint hash, uint table_size){
    return hash % table_size;
}

#endif