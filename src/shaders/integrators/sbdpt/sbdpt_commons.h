#ifndef SBDPT_COMMONS_HOST_DEVICE
#define SBDPT_COMMONS_HOST_DEVICE

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

struct LightHitSample {
    // wo = -wi
    vec3 L_connect;
    float d_vcm;
    vec3 wi;
    float d_vc;
    vec3 n_s;
    float d_vm;
    vec3 n_g;
    float area;
    vec3 light_hit_pos;  
    float mis_weight;
    vec3 throughput;
    float light_pdf_fwd;
    vec2 uv; // for material
    float light_pdf_rev;
    float cam_pdf_fwd;
    vec3 cam_hit_pos;
    float cam_pdf_rev;
    vec3 cam_hit_normal;
    float sampling_pdf_emit;
    uint material_idx;
    // side not needed, sample is always on diffuse surface
};

struct LightHitReservoir {
    LightHitSample light_hit_sample;
    //vec3 prev_cam_hit_pos;
    uint M;
	float W;
    float test;
	//float w_sum;
};

struct LightTransferState {
    vec3 wi;
    vec3 n_s;
    vec3 pos;
    vec2 uv;
    vec3 throughput;
    uint material_idx;
    float area;
    float d_vcm;
    float d_vc;
    float d_vm;
};

struct LightPathReservoir {
    // TODO hardcoded array size change to depth needed
    // TODO is in bdpt path length 6 3 cam/3 light vertices or 6/6 ?
    vec3 cam_hit_pos;
    uint M;
    vec3 cam_hit_normal;
    float W;
    //reservoir samples are in extra buffer
    uint path_vertex_count;
};
#endif // define SBDPT_COMMONS