
#include "ddgi_commons.glsl"

layout(binding = 0) buffer SceneDesc_ { SceneDesc scene_desc; };

layout(binding = 1, rgba16f) uniform image2D output_irradiance;
layout(binding = 2, rg16f) uniform image2D output_depth;

layout(binding = 3) uniform sampler2D input_irradiance;
layout(binding = 4) uniform sampler2D input_depth;

layout(binding = 5) uniform _DDGIUniforms { DDGIUniforms ddgi_ubo; };
layout(binding = 6) uniform sampler2D radiance_img;
layout(binding = 7) uniform sampler2D dir_dist_img;
layout(push_constant) uniform _PushConstantRay { PCDDGI pc; };
layout(buffer_reference, scalar) buffer ProbeOffset { vec4 d[]; };
ProbeOffset probe_offsets = ProbeOffset(scene_desc.probe_offsets_addr);

#define CACHE_SIZE 32

#if defined(IRRADIANCE_UPDATE)
#define NUM_THREADS_X 8
#define NUM_THREADS_Y 8
#define PROBE_SIDE_LENGTH 8
#define PROBE_WITH_BORDER_SIDE 10
#else
#define NUM_THREADS_X 16
#define NUM_THREADS_Y 16
#define PROBE_SIDE_LENGTH 16
#define PROBE_WITH_BORDER_SIDE 18
#endif

layout(local_size_x = NUM_THREADS_X, local_size_y = NUM_THREADS_Y,
       local_size_z = 1) in;

shared vec4 ray_direction_depth[CACHE_SIZE];
#if defined(IRRADIANCE_UPDATE)
shared vec3 ray_radiance[CACHE_SIZE];
#endif
const float tmax = 10000.0;
vec2 normalized_oct_coord() {
    // Local oct texture coords
    ivec2 oct_texture_coord = ivec2(gl_LocalInvocationID.xy);
    vec2 oct_frag_coord = ivec2(oct_texture_coord.x, oct_texture_coord.y);
    return (vec2(oct_frag_coord + 0.5)) * (2.0f / float(PROBE_SIDE_LENGTH)) -
           vec2(1.0f, 1.0f);
}

// Invocation size x = probe_counts.x * probe_counts.y
// Invocation size y = probe_counts.z
void main() {
    const int linear_probe_id =
        int(gl_WorkGroupID.x) + int(gl_WorkGroupID.y * gl_NumWorkGroups.x);
    float total_weight = 0.0f;
    const uint num_probes =
        uint(ddgi_ubo.probe_counts.x * ddgi_ubo.probe_counts.y *
             ddgi_ubo.probe_counts.z);
    uint remaining_rays = ddgi_ubo.rays_per_probe;
    uint offset = 0;
    vec4 result = vec4(0);
    int num_backfaces = 0;
    float probe_state = probe_offsets.d[linear_probe_id].w;
    if (probe_state == DDGI_PROBE_INACTIVE) {
        return;
    }

    while (remaining_rays > 0) {
        uint num_rays = min(CACHE_SIZE, remaining_rays);

        // Load ray_radiance & dir+depth into shared memory
        if (gl_LocalInvocationIndex < num_rays) {
            // Texel coords: X -> local offset, Y : global offset
            ivec2 C =
                ivec2(offset + uint(gl_LocalInvocationIndex), linear_probe_id);

            ray_direction_depth[gl_LocalInvocationIndex] =
                texelFetch(dir_dist_img, C, 0);
#if defined(IRRADIANCE_UPDATE)
            ray_radiance[gl_LocalInvocationIndex] =
                texelFetch(radiance_img, C, 0).xyz;
#endif
        }
        barrier();
        // Iterate all the rays per probe and calculate weights per thread
        for (int i = 0; i < num_rays; i++) {
            const vec3 ray_dir = ray_direction_depth[i].xyz;
            float ray_dist = ray_direction_depth[i].w;
#if defined(IRRADIANCE_UPDATE)
            if (ray_dist < 0) {
                num_backfaces++;
                if (num_backfaces / ddgi_ubo.rays_per_probe >
                    ddgi_ubo.backface_ratio) {
                    return;
                }
                continue;
            }
            const vec3 radiance = ray_radiance[i];

#endif
            // Get normalized per thread oct coords for the current probe
            vec3 texel_dir = oct_decode(normalized_oct_coord());
#if defined(IRRADIANCE_UPDATE)
            float weight = max(0.0, dot(texel_dir, ray_dir));
#else
            float weight = pow(max(0.0, dot(texel_dir, ray_dir)),
                               ddgi_ubo.depth_sharpness);
#endif
            if (weight >= EPS) {
#if defined(IRRADIANCE_UPDATE)
                result += vec4(radiance * weight, weight);
#else
                ray_dist = min(abs(ray_dist), ddgi_ubo.max_distance);
                result += vec4(ray_dist * weight, ray_dist * ray_dist * weight,
                               0, weight);
#endif
            }
        }
        barrier();
        remaining_rays -= num_rays;
        offset += num_rays;
    }
    if (result.w > EPS) {
        result.xyz /= result.w;
    }
    // Inner texture coords mapping
    const ivec2 tex_coord = ivec2(gl_GlobalInvocationID.xy) +
                            (ivec2(gl_WorkGroupID.xy) * ivec2(2)) + ivec2(1);
    // Temporal Accumulation
    vec3 prev_result;
#if defined(IRRADIANCE_UPDATE)
    prev_result = texelFetch(input_irradiance, tex_coord, 0).rgb;
#else
    prev_result = texelFetch(input_depth, tex_coord, 0).rgb;
#endif
    if (pc.first_frame == 0) {
        result.xyz = mix(result.xyz, prev_result, ddgi_ubo.hysteresis);
    }
#if defined(IRRADIANCE_UPDATE)
    imageStore(output_irradiance, tex_coord, vec4(result.xyz, 1.0));
#else
    imageStore(output_depth, tex_coord, vec4(result.xyz, 1.0));
#endif
}
