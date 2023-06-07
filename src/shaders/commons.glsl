#ifndef COMMONS_DEVICE
#define COMMONS_DEVICE
#include "commons.h"
#include "utils.glsl"
#include "atmosphere/atmosphere.glsl"


#ifndef SCENE_TEX_IDX
#define SCENE_TEX_IDX 4
#endif

layout(location = 0) rayPayloadEXT HitPayload payload;
layout(location = 1) rayPayloadEXT AnyHitPayload any_hit_payload;
layout(binding = 0, rgba32f) uniform image2D image;
layout(binding = 1) uniform SceneUBOBuffer { SceneUBO ubo; };
layout(binding = 2) buffer SceneDesc_ { SceneDesc scene_desc; };
layout(binding = 3, scalar) readonly buffer Lights { Light lights[]; };
layout(binding = SCENE_TEX_IDX) uniform sampler2D scene_textures[];


layout(set = 1, binding = 0) uniform accelerationStructureEXT tlas;
layout(buffer_reference, scalar) readonly buffer InstanceInfo {
    PrimMeshInfo d[];
};
layout(buffer_reference, scalar) readonly buffer Vertices { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer Indices { uint i[]; };
layout(buffer_reference, scalar) readonly buffer Normals { vec3 n[]; };
layout(buffer_reference, scalar) readonly buffer TexCoords { vec2 t[]; };
layout(buffer_reference, scalar) readonly buffer Materials { Material m[]; };

Indices indices = Indices(scene_desc.index_addr);
Vertices vertices = Vertices(scene_desc.vertex_addr);
Normals normals = Normals(scene_desc.normal_addr);
Materials materials = Materials(scene_desc.material_addr);
InstanceInfo prim_infos = InstanceInfo(scene_desc.prim_info_addr);

#include "lighting_commons.glsl"

vec4 sample_camera(in vec2 d) {
    vec4 target = ubo.inv_projection * vec4(d.x, d.y, 1, 1);
    return ubo.inv_view * vec4(normalize(target.xyz), 0); // direction
}

vec3 eval_albedo(const Material m) {
    vec3 albedo = m.albedo;
    if (m.texture_id > -1) {
        albedo *= texture(scene_textures[m.texture_id], payload.uv).xyz;
    }
    return albedo;
}

float correct_shading_normal(const vec3 n_g, const vec3 n_s, const vec3 wi,
                             const vec3 wo, int mode) {
    if (mode == 0) {
        float num = abs(dot(wo, n_s) * abs(dot(wi, n_g)));
        float denom = abs(dot(wo, n_g) * abs(dot(wi, n_s)));
        if (denom == 0)
            return 0.;
        return num / denom;
    } else {
        return 1.;
    }
}
#endif