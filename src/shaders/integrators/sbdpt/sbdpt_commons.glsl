
#ifndef SBDPT_COMMONS
#define SBDPT_COMMONS


vec3 vcm_connect_cam(const vec3 cam_pos, const vec3 cam_nrm, vec3 n_s,
                     const float cam_A, const vec3 pos, const in VCMState state,
                     const float eta_vm, const vec3 wo, const Material mat,
                     out ivec2 coords) {
    vec3 L = vec3(0);
    vec3 dir = cam_pos - pos;
    float len = length(dir);
    dir /= len;
    float cos_y = dot(dir, n_s);
    float cos_theta = dot(cam_nrm, -dir);
    if (cos_theta <= 0.) {
        return L;
    }

    // if(dot(n_s, dir) < 0) {
    //     n_s *= -1;
    // }
    // pdf_rev / pdf_fwd
    // in the case of light coming to camera
    // simplifies to abs(cos(theta)) / (A * cos^3(theta) * len^2)
    float cos_3_theta = cos_theta * cos_theta * cos_theta;
    const float cam_pdf_ratio = abs(cos_y) / (cam_A * cos_3_theta * len * len);
    vec3 ray_origin = offset_ray2(pos, n_s);
    float pdf_rev, pdf_fwd;
    const vec3 f = eval_bsdf(n_s, wo, mat, 0, dot(payload.n_s, wo) > 0, dir,
                             pdf_fwd, pdf_rev, cos_y);
    if (f == vec3(0)) {
        return L;
    }
    if (cam_pdf_ratio > 0.0) {
        any_hit_payload.hit = 1;
        traceRayEXT(tlas,
                    gl_RayFlagsTerminateOnFirstHitEXT |
                        gl_RayFlagsSkipClosestHitShaderEXT,
                    0xFF, 1, 0, 1, ray_origin, 0, dir, len - EPS, 1);
        if (any_hit_payload.hit == 0) {
            const float w_light = (cam_pdf_ratio / (screen_size)) *
                                  (eta_vm + state.d_vcm + pdf_rev * state.d_vc);

            const float mis_weight = 1. / (1. + w_light);
            // We / pdf_we * abs(cos_theta) = cam_pdf_ratio
            L = mis_weight * state.throughput * cam_pdf_ratio * f / screen_size;
            // if(isnan(luminance(L))) {
            //     debugPrintfEXT("%v3f\n", state.throughput);
            // }
        }
    }
    dir = -dir;
    vec4 target = ubo.view * vec4(dir.x, dir.y, dir.z, 0);
    target /= target.z;
    target = -ubo.projection * target;
    vec2 screen_dims = vec2(pc_ray.size_x, pc_ray.size_y);
    coords = ivec2(0.5 * (1 + target.xy) * screen_dims - 0.5);
    if (coords.x < 0 || coords.x >= pc_ray.size_x || coords.y < 0 ||
        coords.y >= pc_ray.size_y || dot(dir, cam_nrm) < 0) {
        return vec3(0);
    }
    return L;
}

bool generate_light_sample_for_hit_resampling(in float eta_vc, out VCMState light_state,
                               out bool finite, out float pdf_emit, out float pdf_pos) {
    // Sample light
    uint light_idx;
    uint light_triangle_idx;
    uint light_material_idx;
    vec2 uv_unused;
    LightRecord light_record;
    vec3 wi, pos;
    float pdf_direct, pdf_dir;
    float cos_theta;

    vec3 Le = sample_light_Le(
        seed, pc_ray.num_lights, pc_ray.light_triangle_count, cos_theta,
        light_record, pos, wi, pdf_pos, pdf_dir, pdf_emit, pdf_direct, pc_ray.num_textures);

    if (pdf_dir <= 0) {
        return false;
    }
    light_state.pos = pos;
    light_state.area = 1.0 / pdf_pos;
    light_state.wi = wi;
    

    // in contrast to the regular sampling leave out division by pdf, this will be done when needed before and after resampling with
    // the pdf here before and the new pdf after resampling 1/W after
    light_state.throughput = Le * cos_theta;

    // Both pdf_direct and pdf_emit change in the resampling process
    //light_state.d_vcm = pdf_direct;
    light_state.d_vcm = 0;
    

    finite = is_light_finite(light_record.flags);
    if (!is_light_delta(light_record.flags)) {
        light_state.d_vc = (finite ? cos_theta : 1);
    } else {
        light_state.d_vc = 0;
    }
    light_state.d_vm = light_state.d_vc * eta_vc;
    return true;
}

// TODO test if pdf are right, check if we have to divide by lightPickProb
bool generate_light_sample(float eta_vc, out VCMState light_state,
                               out bool finite, out float pdf_emit) {
    // Sample light
    uint light_idx;
    uint light_triangle_idx;
    uint light_material_idx;
    vec2 uv_unused;
    LightRecord light_record;
    vec3 wi, pos;
    float pdf_pos, pdf_dir, pdf_direct;
    float cos_theta;

    const vec3 Le = sample_light_Le(
        seed, pc_ray.num_lights, pc_ray.light_triangle_count, cos_theta,
        light_record, pos, wi, pdf_pos, pdf_dir, pdf_emit, pdf_direct, pc_ray.num_textures);

    if (pdf_dir <= 0) {
        return false;
    }
    light_state.pos = pos;
    light_state.area = 1.0 / pdf_pos;
    light_state.wi = wi;
    //light_state.throughput = Le * cos_theta / (pdf_dir * pdf_pos);


    light_state.throughput = Le * cos_theta;
#define NO_RESAMPLING
#ifdef NO_RESAMPLING    
        light_state.throughput /= pdf_emit;
#endif
    // Partially evaluate pdfs (area formulation)
    // At s = 0 this is p_rev / p_fwd, in the case of area lights:
    // p_rev = p_connect = 1/area, p_fwd = cos_theta / (PI * area)
    // Note that pdf_fwd is in area formulation, so cos_y / r^2 is missing
    // currently.
    light_state.d_vcm = pdf_direct / pdf_emit;
    // g_prev / p_fwd
    // Note that g_prev component in d_vc and d_vm lags by 1 iter
    // So we initialize g_prev to cos_theta of the current iter
    // Also note that 1/r^2 in the geometry term cancels for vc and vm
    // By convention pdf_fwd sample the i'th vertex from i-1
    // g_prev or pdf_prev samples from i'th vertex to i-1
    // In that sense, cos_theta terms will be common in g_prev and pdf_pwd
    // Similar argument, with the eta

    finite = is_light_finite(light_record.flags);
    if (!is_light_delta(light_record.flags)) {
        light_state.d_vc = (finite ? cos_theta : 1) / pdf_emit;
    } else {
        light_state.d_vc = 0;
    }
    light_state.d_vm = light_state.d_vc * eta_vc;
    return true;
}

vec3 get_environment_radiance(in const VCMState camera_state, in int d, in float world_radius, in int texture_offset) {

    // TODO when adding multilighttype scenes add lightpickprobability
    const int num_lights = 1;
    const float light_pick_prob = 1.f / num_lights;

    float directPdfA;
    float emissionPdfW;
    vec3 radiance = importance_sample_env_light_pdf(camera_state.wi, directPdfA, texture_offset);
    emissionPdfW = directPdfA * INV_PI / (world_radius * world_radius);
    
    if (d == 1) {
        return radiance;
    }

    directPdfA *= light_pick_prob;
    emissionPdfW *= light_pick_prob;

    const float w_camera = directPdfA * camera_state.d_vcm + emissionPdfW * camera_state.d_vc;

    const float mis_weight = 1. / (1. + w_camera);

    return radiance * mis_weight;
}

// Only for triangle lights, but no other emitters get hit 
vec3 vcm_get_light_radiance(in const Material mat,
                            in const VCMState camera_state, int d) {
    if (d == 1) {
        return mat.emissive_factor;
    }
    const float pdf_light_pos =
        1.0 / (payload.area * pc_ray.light_triangle_count);

    const float pdf_light_dir = abs(dot(payload.n_s, -camera_state.wi)) / PI;
    const float w_camera =
        pdf_light_pos * camera_state.d_vcm +
        (pc_ray.use_vc == 1 || pc_ray.use_vm == 1
             ? (pdf_light_pos * pdf_light_dir) * camera_state.d_vc
             : 0);
    const float mis_weight = 1. / (1. + w_camera);
    return mis_weight * mat.emissive_factor;
}

vec3 vcm_connect_light(vec3 n_s, vec3 wo, Material mat, bool side, float eta_vm,
                       VCMState camera_state, out float pdf_rev, out vec3 f) {
    vec3 wi;
    float wi_len;
    float pdf_pos_w; //directPdfW
    float pdf_pos_dir_w; //emissionPdfW
    LightRecord record;
    float cos_y;
    vec3 res = vec3(0);

    const vec3 Le =
        sample_light_Li_dir_w(seed, payload.pos, pc_ray.num_lights, wi, wi_len,
                        pdf_pos_w, pdf_pos_dir_w, record, cos_y, pc_ray.num_textures);

    const float cos_x = dot(wi, n_s);
    const vec3 ray_origin = offset_ray2(payload.pos, n_s);
    any_hit_payload.hit = 1;
    float pdf_fwd;
    f = eval_bsdf(n_s, wo, mat, 1, side, wi, pdf_fwd, pdf_rev, cos_x);
    if (f != vec3(0)) {
        traceRayEXT(tlas,
                    gl_RayFlagsTerminateOnFirstHitEXT |
                        gl_RayFlagsSkipClosestHitShaderEXT,
                    0xFF, 1, 0, 1, ray_origin, 0, wi, wi_len - EPS, 1);
        const bool visible = any_hit_payload.hit == 0;
        if (visible) {
            if (is_light_delta(record.flags)) {
                pdf_fwd = 0;
            }
            const float w_light =
                pdf_fwd / (pdf_pos_w);
            const float w_cam =
                pdf_pos_dir_w * abs(cos_x) / (pdf_pos_w * cos_y) *
                (eta_vm + camera_state.d_vcm + camera_state.d_vc * pdf_rev);
            const float mis_weight = 1. / (1. + w_light + w_cam);
            if (mis_weight > 0) {
                res = mis_weight * abs(cos_x) * f * camera_state.throughput *
                      Le / (pdf_pos_w);
            }
        }
    }
    return res;
}

#define light_vtx(i) vcm_lights.d[i]
vec3 vcm_connect_light_vertices(uint light_path_len, in uint light_path_idx,
                                int depth, vec3 n_s, vec3 wo, Material mat,
                                bool side, float eta_vm, in VCMState camera_state,
                                float pdf_rev) {
    vec3 res = vec3(0);
	
    //for (int i = 0; i < 1; i++) {
    for (int i = 0; i < light_path_len; i++) { 
        //cam_hit_pos = payload.pos;
        if(light_path_len == 0) {
            break;
        }
        
        uint s = light_vtx(light_path_idx + i).path_len;
        uint mdepth = s + depth - 1;
        
        if (mdepth >= pc_ray.max_depth + 1) {
            break;
        }
        vec3 dir = light_vtx(light_path_idx + i).pos - payload.pos;
        const float len = length(dir);
        const float len_sqr = len * len;
        dir /= len;
        const float cos_cam = dot(n_s, dir);
        const float cos_light = dot(light_vtx(light_path_idx + i).n_s, -dir);
        const float G = cos_light * cos_cam / len_sqr;


        
        if (G > 0) {
            
            float cam_pdf_fwd, light_pdf_fwd, light_pdf_rev;
            const vec3 f_cam =
                eval_bsdf(n_s, wo, mat, 1, side, dir, cam_pdf_fwd, pdf_rev, cos_cam);
            const Material light_mat =
                load_material(light_vtx(light_path_idx + i).material_idx,
                              light_vtx(light_path_idx + i).uv);
            // TODO: what about anisotropic BSDFS?
            const vec3 f_light =
                eval_bsdf(light_vtx(light_path_idx + i).n_s,
                          light_vtx(light_path_idx + i).wo, light_mat, 0,
                          light_vtx(light_path_idx + i).side == 1, -dir,
                          light_pdf_fwd, light_pdf_rev, cos_light);
            if (f_light != vec3(0) && f_cam != vec3(0)) {
                cam_pdf_fwd *= abs(cos_light) / len_sqr;
                light_pdf_fwd *= abs(cos_cam) / len_sqr;
                const float w_light =
                    cam_pdf_fwd *
                    (eta_vm + light_vtx(light_path_idx + i).d_vcm +
                     light_pdf_rev * light_vtx(light_path_idx + i).d_vc);
                const float w_camera =
                    light_pdf_fwd *
                    (eta_vm + camera_state.d_vcm + pdf_rev * camera_state.d_vc);
                
                const vec3 ray_origin = offset_ray2(payload.pos, n_s);
                any_hit_payload.hit = 1;
                traceRayEXT(tlas,
                            gl_RayFlagsTerminateOnFirstHitEXT |
                                gl_RayFlagsSkipClosestHitShaderEXT,
                            0xFF, 1, 0, 1, ray_origin, 0, dir, len - EPS, 1);
                const bool visible = any_hit_payload.hit == 0;
                if (visible) {

                    const float mis_weight = 1. / (1 + w_camera + w_light);
                    //res = vec3(1);
					//if (i == 0) //res += vec3(G) * 2000;
						// camera_state.throughput;
						res += mis_weight * G *camera_state.throughput * f_cam * light_vtx(light_path_idx + i).throughput * f_light;
                }

            }
        }
    }
    
    //if(depth == 2)
            //return (test);
    return res;
}
#undef light_vtx

/*void init_light_hit_reservoir(out LightHitReservoir lhr) {

    lhr.M = 0;
    lhr.W = 0.f;
    LightHitSample s;
    s.wi, s.L_connect, s.n_s, s.n_g, s.light_hit_pos, s.throughput = vec3(0);
    s.uv = vec2(0);
    s.light_pdf_fwd, s.light_pdf_rev, s.cam_pdf_fwd, s.cam_pdf_rev = 0.f;
    s.d_vcm, s.d_vc, s.d_vm, s.area, s.mis_weight = 0.f;
    s.material_idx = 0;
    lhr.light_hit_sample = s;
}

bool update_light_hit_reservoir(inout LightHitReservoir r, inout float w_sum, in LightHitSample s, in float w_i) {
    w_sum += w_i;
    r.M++;
    if (rand(seed) * w_sum <= w_i) {
        r.light_hit_sample = s;
        return true;
    }
    return false;
}

vec3 resample_light_hit(in VCMState camera_state, inout VCMState light_first_hit, in vec3 light_hit_n_g, inout float sampling_pdf_emit, in float sampling_pdf_pos) {

    // calculate camera connection and forward
    traceRayEXT(tlas, flags, 0xFF, 0, 0, 0, camera_state.pos, tmin,
        camera_state.wi, tmax, 0);
    if (payload.material_idx == -1) {
        // no light connections will be made
        // TODO forward environment radiance or compute in trace_eye
        // TODO FIX
        return vec3(0);
    }
    
    vec3 wo = camera_state.pos - payload.pos;
    float dist = length(payload.pos - camera_state.pos);
    float dist_sqr = dist * dist;
    wo /= dist;
    vec3 n_s = payload.n_s;
    vec3 n_g = payload.n_g;
    bool side = true;
    if (dot(payload.n_g, wo) < 0.)
        n_g = -n_g;
    if (dot(n_g, n_s) < 0) {
        n_s = -n_s;
        side = false;
    }
    float cos_wo = abs(dot(wo, n_s));
    const Material mat = load_material(payload.material_idx, payload.uv);
    // TODO handle specular, at the moment only test diffuse scene
    const bool mat_specular =
        (mat.bsdf_props & BSDF_SPECULAR) == BSDF_SPECULAR;
    camera_state.d_vcm *= dist_sqr;
    camera_state.d_vcm /= cos_wo;
    camera_state.d_vc /= cos_wo;
    camera_state.d_vm /= cos_wo;

    LightHitSample light_hit_sample;
    light_hit_sample.wi = light_first_hit.wi;
    light_hit_sample.light_hit_pos = light_first_hit.pos;
    light_hit_sample.n_g = light_hit_n_g;
    light_hit_sample.n_s = light_first_hit.n_s;
    light_hit_sample.material_idx = light_first_hit.material_idx;
    light_hit_sample.uv = light_first_hit.uv;
    light_hit_sample.area = light_first_hit.area;


    // throughput has to be adjusted after resampling
    light_hit_sample.throughput = light_first_hit.throughput;
    // also has to be adjusted, multiply with new mis_weight and W after resampling
    light_hit_sample.L_connect = vec3(0);
    // TODO factors have to be adjusted after resampling (vcm, vc, vm)
    light_hit_sample.d_vcm = light_first_hit.d_vcm;
    light_hit_sample.d_vc = light_first_hit.d_vc;
    light_hit_sample.d_vm = light_first_hit.d_vm;
    // adjust factors for complete mis calculation of current sample
    // pdf_emit / pdf_pos = pdf_direct
    light_first_hit.d_vcm = 1.f / sampling_pdf_pos;//(sampling_pdf_emit/sampling_pdf_pos) / sampling_pdf_emit;
    //light_first_hit.d_vcm /= sampling_pdf_emit;
    light_first_hit.d_vc /= sampling_pdf_emit;
    light_first_hit.d_vm /= sampling_pdf_emit;


    // wi
    float L_connect_complete = 0;

    // connect vertices
    vec3 dir = light_first_hit.pos - payload.pos;
    const float len = length(dir);
    const float len_sqr = len * len;
    dir /= len;
    const float cos_cam = dot(n_s, dir);
    const float cos_light = dot(light_first_hit.n_s, -dir);
    // TODO G stays the same only when cam hit is the same -> spatial resampling has to adjust G, jacobian factor?
    const float G = cos_light * cos_cam / len_sqr;

    if (G > 0) {
        float cam_pdf_fwd, light_pdf_fwd, light_pdf_rev, pdf_rev;
        const vec3 f_cam =
            eval_bsdf(n_s, wo, mat, 1, side, dir, cam_pdf_fwd, pdf_rev, cos_cam);
        const Material light_mat =
            load_material(light_first_hit.material_idx,
                            light_first_hit.uv);

        const vec3 f_light =
            eval_bsdf(light_first_hit.n_s,
                        -light_first_hit.wi, light_mat, 0,
                        false, -dir,
                        light_pdf_fwd, light_pdf_rev, cos_light);

        if (f_light != vec3(0) && f_cam != vec3(0)) {
            cam_pdf_fwd *= abs(cos_light) / len_sqr;
            light_pdf_fwd *= abs(cos_cam) / len_sqr;

            light_hit_sample.cam_pdf_fwd = cam_pdf_fwd;
            light_hit_sample.light_pdf_fwd = light_pdf_fwd;
            light_hit_sample.light_pdf_rev = light_pdf_rev;
            light_hit_sample.cam_pdf_rev = pdf_rev;

            const float w_light =
                cam_pdf_fwd *
                (light_first_hit.d_vcm +
                    light_pdf_rev * light_first_hit.d_vc);
            const float w_camera = 
                light_pdf_fwd *
                (camera_state.d_vcm + pdf_rev * camera_state.d_vc);
            const float mis_weight = 1. / (1 + w_camera + w_light);
            // TODO check validity of offset function
            const vec3 ray_origin = offset_ray2(payload.pos, n_s);
            // Visibility can be resampled here, the sample that is resampled does not change,
            // we connect to the first camera hit, that connection has to be rechecked, not 
            // between the camera hit and light hit
            any_hit_payload.hit = 1;
            traceRayEXT(tlas,
                        gl_RayFlagsTerminateOnFirstHitEXT |
                            gl_RayFlagsSkipClosestHitShaderEXT,
                        0xFF, 1, 0, 1, ray_origin, 0, dir, len - EPS, 1);
            const bool visible = any_hit_payload.hit == 0;
            if (visible) {
                // TODO does camera throughput change on camera movement?
                light_hit_sample.L_connect = G * camera_state.throughput *
                        light_first_hit.throughput * f_cam *
                        f_light;
                // complete sample aka wi for resampling
                light_hit_sample.mis_weight = mis_weight;
                L_connect_complete = sampling_pdf_emit <= 0.f ? 0.f : luminance(light_hit_sample.L_connect) * mis_weight / sampling_pdf_emit;
            }
        }
    }

    
    LightHitReservoir lhr;
    

    if (pc_ray.do_spatiotemporal == 0) {
        
        init_light_hit_reservoir(lhr);
    } else {
        lhr = light_hit_reservoirs_temporal.d[pixel_idx];
    }

    float wi = L_connect_complete;
    
    float temporal_tf = luminance(lhr.light_hit_sample.L_connect);

    float w_sum = lhr.W * lhr.M * temporal_tf * lhr.light_hit_sample.mis_weight;

    bool reservoir_sample_changed = update_light_hit_reservoir(lhr, w_sum, light_hit_sample, wi);


    w_sum /= lhr.M;
    lhr.M = min(lhr.M, 50);
    float current_tf = luminance(lhr.light_hit_sample.L_connect);
    float weighted_mis = current_tf <= 0.f ? 0.f : w_sum / current_tf;
    lhr.W = lhr.light_hit_sample.mis_weight <= 0.f ? 0.f : weighted_mis / lhr.light_hit_sample.mis_weight;

    light_hit_reservoirs_temporal.d[pixel_idx] = lhr;
    //light_hit_reservoirs_temporal.d[pixel_idx] = tr;

    // set resampled sample
    light_hit_sample = lhr.light_hit_sample;
    
    //float post_resampling_pdf_emit = 1.f / W;
    // recalculate d_vc, d_vcm, d_vm and mis_weight with new pdf emit
    // lhr.W is the resampled 1/sampling_pdf_emit
    // TODO check if this works right for every light type
    light_hit_sample.d_vcm = sampling_pdf_pos <= 0.f ? 0.f : 1.f / sampling_pdf_pos;//lhr.W <= 0.f ? 0.f : ((1.f/lhr.W) / sampling_pdf_pos) * lhr.W;
    light_hit_sample.d_vc *= lhr.W;
    light_hit_sample.d_vm *= lhr.W;
    light_hit_sample.throughput *= lhr.W;
    // new mis_weight
    const float w_light = light_hit_sample.cam_pdf_fwd * (light_hit_sample.d_vcm + light_hit_sample.light_pdf_rev * light_hit_sample.d_vc);
    // TODO w_camera stays the same, no influence by W, can be saved in sample
    const float w_camera = light_hit_sample.light_pdf_fwd * (camera_state.d_vcm + light_hit_sample.cam_pdf_rev * camera_state.d_vc);

    //light_hit_sample.mis_weight = 1. / (1 + w_camera + w_light);
    float resampled_mis_weight = 1. / (1 + w_camera + w_light);
    

    

    // adjust the light_state for further computation in next bounces (vcm_state in fill_light)
    light_first_hit.wi = light_hit_sample.wi;
    light_first_hit.n_s = light_hit_sample.n_s;
    light_first_hit.pos = light_hit_sample.light_hit_pos;
    light_first_hit.uv = light_hit_sample.uv;
    // TODO not considering non resampling case
    light_first_hit.throughput = light_hit_sample.throughput;
    light_first_hit.material_idx = light_hit_sample.material_idx;
    light_first_hit.area = light_hit_sample.area;
    light_first_hit.d_vcm = light_hit_sample.d_vcm;
    light_first_hit.d_vc = light_hit_sample.d_vc;
    light_first_hit.d_vm = light_hit_sample.d_vm;
    
    //return vec3(abs(light_hit_sample.mis_weight - resampled_mis_weight));
    return light_hit_sample.L_connect * resampled_mis_weight * lhr.W;
    //return abs(light_hit_sample.L_connect * light_hit_sample.mis_weight * lhr.W - light_hit_sample.L_connect * resampled_mis_weight * lhr.W);
    //return light_hit_sample.L_connect * light_hit_sample.mis_weight / sampling_pdf_emit;//light_hit_sample.L_connect * light_hit_sample.mis_weight * lhr.W;
}

vec3 fill_light_with_lighthit_resampling(vec3 origin, VCMState vcm_state, bool finite_light,
                     float eta_vcm, float eta_vc, float eta_vm, in VCMState camera_state, in float sample_pdf_emit, in float sample_pdf_pos) {
#define light_vtx(i) vcm_lights.d[vcm_light_path_idx + i]
    const vec3 cam_pos = origin;
    const vec3 cam_nrm = vec3(-ubo.inv_view * vec4(0, 0, 1, 0));
    const float radius = pc_ray.radius;
    const float radius_sqr = radius * radius;
    vec4 area_int = (ubo.inv_projection * vec4(2. / gl_LaunchSizeEXT.x,
                                               2. / gl_LaunchSizeEXT.y, 0, 1));
    area_int /= (area_int.w);
    const float cam_area = abs(area_int.x * area_int.y);
    int depth;
    int path_idx = 0;
    bool specular = false;
    light_vtx(path_idx).path_len = 0;
    for (depth = 1;; depth++) {
        traceRayEXT(tlas, flags, 0xFF, 0, 0, 0, vcm_state.pos, tmin,
                    vcm_state.wi, tmax, 0);
        if (payload.material_idx == -1) {
            break;
        }
        vec3 wo = vcm_state.pos - payload.pos;

        vec3 n_s = payload.n_s;
        vec3 n_g = payload.n_g;
        bool side = true;
        if (dot(payload.n_g, wo) <= 0.)
            n_g = -n_g;
        if (dot(n_g, n_s) < 0) {
            n_s = -n_s;
            side = false;
        }
        float cos_wo = dot(wo, n_s);
        float dist = length(payload.pos - vcm_state.pos);
        float dist_sqr = dist * dist;
        wo /= dist;
        Material mat = load_material(payload.material_idx, payload.uv);
        bool mat_specular =
            (mat.bsdf_props & BSDF_SPECULAR) == BSDF_SPECULAR;
        // Complete the missing geometry terms
        float cos_theta_wo = abs(dot(wo, n_s));

        if (depth > 1 || finite_light) {
            vcm_state.d_vcm *= dist_sqr;
        }
        


        vcm_state.d_vcm /= cos_theta_wo;
        vcm_state.d_vc /= cos_theta_wo;
        vcm_state.d_vm /= cos_theta_wo;
        vcm_state.n_s = n_s;
        // TODO double check if this should be moved down, area before was set later
        vcm_state.area = payload.area;
        vcm_state.material_idx = payload.material_idx;
        vcm_state.pos = payload.pos;
        vcm_state.uv = payload.uv;
        //vcm_state.side = uint(side);



        if ((!mat_specular && (pc_ray.use_vc == 1 || pc_ray.use_vm == 1))) {
            
            
            

            if (depth == 1) {
                // calculate camera connection and forward
                return resample_light_hit(camera_state, vcm_state, n_g, sample_pdf_emit, sample_pdf_pos);
                // reload mat (only idx in sample saved)
                // TODO consider specular case
                mat = load_material(vcm_state.material_idx, vcm_state.uv);
                mat_specular = (mat.bsdf_props & BSDF_SPECULAR) == BSDF_SPECULAR;
                wo = -vcm_state.wi;
            }

            VCMVertex light_vertex;
            light_vertex.wo = wo;
            light_vertex.n_s = vcm_state.n_s;
            light_vertex.pos = vcm_state.pos;
            light_vertex.uv = vcm_state.uv;
            light_vertex.material_idx = vcm_state.material_idx;
            light_vertex.area = vcm_state.area;
            light_vertex.throughput = vcm_state.throughput;
            light_vertex.d_vcm = vcm_state.d_vcm;
            light_vertex.d_vc = vcm_state.d_vc;
            light_vertex.d_vm = vcm_state.d_vm;
            light_vertex.path_len = depth + 1;
            // TODO side is not resampled
            light_vertex.side = uint(side);
            
            // Copy to light vertex buffer
            light_vtx(path_idx) = light_vertex;
                
            
            path_idx++;
        }
        if (depth >= pc_ray.max_depth) {
            break;
        }
        // Reverse pdf in solid angle form, since we have geometry term
        // at the outer paranthesis
        if (!mat_specular && (pc_ray.use_vc == 1 && depth < pc_ray.max_depth)) {
            // Connect to camera
            ivec2 coords;
            vec3 splat_col =
                vcm_connect_cam(cam_pos, cam_nrm, vcm_state.n_s, cam_area, vcm_state.pos,
                                vcm_state, eta_vm, wo, mat, coords);
			
            if (luminance(splat_col) > 0) {
                uint idx = coords.x * gl_LaunchSizeEXT.y +
                        coords.y;
                // TODO REENABLE
                //if(depth == 1)
                    //tmp_col.d[idx] += splat_col;
            }
//#endif
        }

        // Continue the walk
        float pdf_dir;
        float cos_theta;
		
        const vec3 f = sample_bsdf(vcm_state.n_s, wo, mat, 0, side, vcm_state.wi, pdf_dir,
                                   cos_theta, seed);

        const bool same_hemisphere = same_hemisphere(vcm_state.wi, wo, vcm_state.n_s);

        const bool mat_transmissive =
            (mat.bsdf_props & BSDF_TRANSMISSIVE) == BSDF_TRANSMISSIVE;
        if (f == vec3(0) || pdf_dir == 0 ||
            (!same_hemisphere && !mat_transmissive)) {
            break;
        }
        float pdf_rev = pdf_dir;
        if (!mat_specular) {
            pdf_rev = bsdf_pdf(mat, vcm_state.n_s, vcm_state.wi, wo);
        }
        const float abs_cos_theta = abs(cos_theta);
        //TODO test if offset would do anything
        //vcm_state.pos = offset_ray(vcm_state.pos, vcm_state.n_g);
        // Note, same cancellations also occur here from now on
        // see _vcm_generate_light_sample_
        if (!mat_specular) {
            vcm_state.d_vc =
                (abs_cos_theta / pdf_dir) *
                (eta_vm + vcm_state.d_vcm + pdf_rev * vcm_state.d_vc);
            vcm_state.d_vm =
                (abs_cos_theta / pdf_dir) *
                (1 + vcm_state.d_vcm * eta_vc + pdf_rev * vcm_state.d_vm);
            vcm_state.d_vcm = 1.0 / pdf_dir;
        } else {
            // Specular pdf has value = inf, so d_vcm = 0;
            vcm_state.d_vcm = 0;
            // pdf_fwd = pdf_rev = delta -> cancels
            vcm_state.d_vc *= abs_cos_theta;
            vcm_state.d_vm *= abs_cos_theta;
            specular = true;
        }
        vcm_state.throughput *= f * abs_cos_theta / pdf_dir;
        //vcm_state.n_s = n_s;
        //vcm_state.area = payload.area;
        //vcm_state.material_idx = payload.material_idx;
    }
    light_path_cnts.d[pixel_idx] = path_idx;

    // TODO REMOVE
    return vec3(0);
}
#undef light_vtx*/

void fill_light_continued(vec3 origin, VCMState vcm_state, bool finite_light,
                     float eta_vcm, float eta_vc, float eta_vm) {
#define light_vtx(i) vcm_lights.d[vcm_light_path_idx + i]
    if (light_path_cnts.d[pixel_idx] == 0) {
        return;
    }
    const vec3 cam_pos = origin;
    const vec3 cam_nrm = vec3(-ubo.inv_view * vec4(0, 0, 1, 0));
    const float radius = pc_ray.radius;
    const float radius_sqr = radius * radius;
    vec4 area_int = (ubo.inv_projection * vec4(2. / gl_LaunchSizeEXT.x,
                                               2. / gl_LaunchSizeEXT.y, 0, 1));
    area_int /= (area_int.w);
    const float cam_area = abs(area_int.x * area_int.y);
    int depth;
    int path_idx = 1;
    bool specular = false;
    light_vtx(path_idx).path_len = 1;
    for (depth = 2;; depth++) {
        traceRayEXT(tlas, flags, 0xFF, 0, 0, 0, vcm_state.pos, tmin,
                    vcm_state.wi, tmax, 0);
        if (payload.material_idx == -1) {
            break;
        }
        vec3 wo = vcm_state.pos - payload.pos;

        vec3 n_s = payload.n_s;
        vec3 n_g = payload.n_g;
        bool side = true;
        if (dot(payload.n_g, wo) <= 0.)
            n_g = -n_g;
        if (dot(n_g, n_s) < 0) {
            n_s = -n_s;
            side = false;
        }
        float cos_wo = dot(wo, n_s);
        float dist = length(payload.pos - vcm_state.pos);
        float dist_sqr = dist * dist;
        wo /= dist;
        const Material mat = load_material(payload.material_idx, payload.uv);
        const bool mat_specular =
            (mat.bsdf_props & BSDF_SPECULAR) == BSDF_SPECULAR;
        // Complete the missing geometry terms
        float cos_theta_wo = abs(dot(wo, n_s));

        if (depth > 1 || finite_light) {
            vcm_state.d_vcm *= dist_sqr;
        }
        


        vcm_state.d_vcm /= cos_theta_wo;
        vcm_state.d_vc /= cos_theta_wo;
        vcm_state.d_vm /= cos_theta_wo;
        if ((!mat_specular && (pc_ray.use_vc == 1 || pc_ray.use_vm == 1))) {
            
            // Copy to light vertex buffer
            // light_vtx(path_idx).wi = vcm_state.wi;
            light_vtx(path_idx).wo = wo; //-vcm_state.wi;
            light_vtx(path_idx).n_s = n_s;
            light_vtx(path_idx).pos = payload.pos;
            light_vtx(path_idx).uv = payload.uv;
            light_vtx(path_idx).material_idx = payload.material_idx;
            light_vtx(path_idx).area = payload.area;
            light_vtx(path_idx).throughput = vcm_state.throughput;
            light_vtx(path_idx).d_vcm = vcm_state.d_vcm;
            light_vtx(path_idx).d_vc = vcm_state.d_vc;
            
            light_vtx(path_idx).d_vm = vcm_state.d_vm;
            light_vtx(path_idx).path_len = depth + 1;
            light_vtx(path_idx).side = uint(side);
            path_idx++;
        }
        if (depth >= pc_ray.max_depth) {
            break;
        }
        // Reverse pdf in solid angle form, since we have geometry term
        // at the outer paranthesis
        if (!mat_specular && (pc_ray.use_vc == 1 && depth < pc_ray.max_depth)) {
            // Connect to camera
            ivec2 coords;
            vec3 splat_col =
                vcm_connect_cam(cam_pos, cam_nrm, n_s, cam_area, payload.pos,
                                vcm_state, eta_vm, wo, mat, coords);
			
            if (luminance(splat_col) > 0) {
                uint idx = coords.x * gl_LaunchSizeEXT.y +
                        coords.y;
                // TODO REENABLE
                //if(depth == 1)
                    //tmp_col.d[idx] += splat_col;
            }
//#endif
        }

        // Continue the walk
        float pdf_dir;
        float cos_theta;
		
        const vec3 f = sample_bsdf(n_s, wo, mat, 0, side, vcm_state.wi, pdf_dir,
                                   cos_theta, seed);

        const bool same_hemisphere = same_hemisphere(vcm_state.wi, wo, n_s);

        const bool mat_transmissive =
            (mat.bsdf_props & BSDF_TRANSMISSIVE) == BSDF_TRANSMISSIVE;
        if (f == vec3(0) || pdf_dir == 0 ||
            (!same_hemisphere && !mat_transmissive)) {
            break;
        }
        float pdf_rev = pdf_dir;
        if (!mat_specular) {
            pdf_rev = bsdf_pdf(mat, n_s, vcm_state.wi, wo);
        }
        const float abs_cos_theta = abs(cos_theta);

        vcm_state.pos = offset_ray(payload.pos, n_g);
        // Note, same cancellations also occur here from now on
        // see _vcm_generate_light_sample_
        if (!mat_specular) {
            vcm_state.d_vc =
                (abs_cos_theta / pdf_dir) *
                (eta_vm + vcm_state.d_vcm + pdf_rev * vcm_state.d_vc);
            vcm_state.d_vm =
                (abs_cos_theta / pdf_dir) *
                (1 + vcm_state.d_vcm * eta_vc + pdf_rev * vcm_state.d_vm);
            vcm_state.d_vcm = 1.0 / pdf_dir;
        } else {
            // Specular pdf has value = inf, so d_vcm = 0;
            vcm_state.d_vcm = 0;
            // pdf_fwd = pdf_rev = delta -> cancels
            vcm_state.d_vc *= abs_cos_theta;
            vcm_state.d_vm *= abs_cos_theta;
            specular = true;
        }
        vcm_state.throughput *= f * abs_cos_theta / pdf_dir;
        vcm_state.n_s = n_s;
        vcm_state.area = payload.area;
        vcm_state.material_idx = payload.material_idx;
    }
    light_path_cnts.d[pixel_idx] = path_idx;
}
#undef light_vtx


void fill_light(vec3 origin, VCMState vcm_state, bool finite_light,
                     float eta_vcm, float eta_vc, float eta_vm, in float pdf_emit) {
#define light_vtx(i) vcm_lights.d[vcm_light_path_idx + i]
    const vec3 cam_pos = origin;
    const vec3 cam_nrm = vec3(-ubo.inv_view * vec4(0, 0, 1, 0));
    const float radius = pc_ray.radius;
    const float radius_sqr = radius * radius;
    vec4 area_int = (ubo.inv_projection * vec4(2. / gl_LaunchSizeEXT.x,
                                               2. / gl_LaunchSizeEXT.y, 0, 1));
    area_int /= (area_int.w);
    const float cam_area = abs(area_int.x * area_int.y);
    int depth;
    int path_idx = 0;
    bool specular = false;
	//light_path_cnts.d[pixel_idx] = 0;
    light_vtx(path_idx).path_len = 0;
    for (depth = 1;; depth++) {
        traceRayEXT(tlas, flags, 0xFF, 0, 0, 0, vcm_state.pos, tmin,
                    vcm_state.wi, tmax, 0);
        if (payload.material_idx == -1) {
            break;
        }
        vec3 wo = vcm_state.pos - payload.pos;

        vec3 n_s = payload.n_s;
        vec3 n_g = payload.n_g;
        bool side = true;
        if (dot(payload.n_g, wo) <= 0.)
            n_g = -n_g;
        if (dot(n_g, n_s) < 0) {
            n_s = -n_s;
            side = false;
        }
        float cos_wo = dot(wo, n_s);
        float dist = length(payload.pos - vcm_state.pos);
        float dist_sqr = dist * dist;
        wo /= dist;
        const Material mat = load_material(payload.material_idx, payload.uv);
        const bool mat_specular =
            (mat.bsdf_props & BSDF_SPECULAR) == BSDF_SPECULAR;
        // Complete the missing geometry terms
        float cos_theta_wo = abs(dot(wo, n_s));

        if (depth > 1 || finite_light) {
            vcm_state.d_vcm *= dist_sqr;
        }
        


        vcm_state.d_vcm /= cos_theta_wo;
        vcm_state.d_vc /= cos_theta_wo;
        vcm_state.d_vm /= cos_theta_wo;
        if ((!mat_specular && (pc_ray.use_vc == 1 || pc_ray.use_vm == 1))) {
            
            // Copy to light vertex buffer
            light_vtx(path_idx).wi = vcm_state.wi;
            light_vtx(path_idx).wo = wo; //-vcm_state.wi;
            light_vtx(path_idx).n_s = n_s;
            light_vtx(path_idx).pos = payload.pos;
            light_vtx(path_idx).uv = payload.uv;
            light_vtx(path_idx).material_idx = payload.material_idx;
            light_vtx(path_idx).area = payload.area;
            light_vtx(path_idx).throughput = vcm_state.throughput;
            light_vtx(path_idx).d_vcm = vcm_state.d_vcm;
            light_vtx(path_idx).d_vc = vcm_state.d_vc;
            light_vtx(path_idx).d_vm = vcm_state.d_vm;
            light_vtx(path_idx).path_len = depth + 1;
            light_vtx(path_idx).side = uint(side);
            // add resampling necessary data
            light_vtx(path_idx).n_g = n_g;
            light_vtx(path_idx).pdf_emit = pdf_emit;
            path_idx++;
        }
        if (depth >= pc_ray.max_depth) {
            break;
        }
        // Reverse pdf in solid angle form, since we have geometry term
        // at the outer paranthesis
        if (!mat_specular && (pc_ray.use_vc == 1 && depth < pc_ray.max_depth)) {
            // Connect to camera
            ivec2 coords;
            vec3 splat_col =
                vcm_connect_cam(cam_pos, cam_nrm, n_s, cam_area, payload.pos,
                                vcm_state, eta_vm, wo, mat, coords);
			
            if (luminance(splat_col) > 0) {
                uint idx = coords.x * gl_LaunchSizeEXT.y +
                        coords.y;
                    
                //tmp_col.d[idx] += splat_col;
            }
//#endif
        }

        // Continue the walk
        float pdf_dir;
        float cos_theta;
		
        const vec3 f = sample_bsdf(n_s, wo, mat, 0, side, vcm_state.wi, pdf_dir,
                                   cos_theta, seed);

        const bool same_hemisphere = same_hemisphere(vcm_state.wi, wo, n_s);

        const bool mat_transmissive =
            (mat.bsdf_props & BSDF_TRANSMISSIVE) == BSDF_TRANSMISSIVE;
        if (f == vec3(0) || pdf_dir == 0 ||
            (!same_hemisphere && !mat_transmissive)) {
            break;
        }
        float pdf_rev = pdf_dir;
        if (!mat_specular) {
            pdf_rev = bsdf_pdf(mat, n_s, vcm_state.wi, wo);
        }
        const float abs_cos_theta = abs(cos_theta);

        vcm_state.pos = offset_ray(payload.pos, n_g);
        // Note, same cancellations also occur here from now on
        // see _vcm_generate_light_sample_
        if (!mat_specular) {
            vcm_state.d_vc =
                (abs_cos_theta / pdf_dir) *
                (eta_vm + vcm_state.d_vcm + pdf_rev * vcm_state.d_vc);
            vcm_state.d_vm =
                (abs_cos_theta / pdf_dir) *
                (1 + vcm_state.d_vcm * eta_vc + pdf_rev * vcm_state.d_vm);
            vcm_state.d_vcm = 1.0 / pdf_dir;
        } else {
            // Specular pdf has value = inf, so d_vcm = 0;
            vcm_state.d_vcm = 0;
            // pdf_fwd = pdf_rev = delta -> cancels
            vcm_state.d_vc *= abs_cos_theta;
            vcm_state.d_vm *= abs_cos_theta;
            specular = true;
        }
        vcm_state.throughput *= f * abs_cos_theta / pdf_dir;
        vcm_state.n_s = n_s;
        vcm_state.area = payload.area;
        vcm_state.material_idx = payload.material_idx;
    }
	light_path_cnts.d[pixel_idx] = path_idx;
}
#undef light_vtx



vec3 vcm_trace_eye(VCMState camera_state, float eta_vcm, float eta_vc,
                   float eta_vm) {

    float avg_len = 0;
    uint cnt = 1;
    const float radius = pc_ray.radius;
    const float radius_sqr = radius * radius;

    // TODO no randomization needed
    //uint light_path_idx = uint(rand(seed) * screen_size);
	uint light_path_idx = (gl_LaunchIDEXT.x * gl_LaunchSizeEXT.y + gl_LaunchIDEXT.y);
	//uint light_path_idx = vcm_light_path_idx;
    uint light_path_len = light_path_cnts.d[light_path_idx];
    light_path_idx *= (pc_ray.max_depth + 1);
    vec3 col = vec3(0);

    
    int depth;
    const float normalization_factor = 1. / (PI * radius_sqr * screen_size);

    for (depth = 1;; depth++) {
        //if(depth == 2) {
          //  break;
        //}

        traceRayEXT(tlas, flags, 0xFF, 0, 0, 0, camera_state.pos, tmin,
                    camera_state.wi, tmax, 0);
        

        if (payload.material_idx == -1) {
            //if(depth==1)
                //col += camera_state.throughput * get_environment_radiance(camera_state, depth, pc_ray.world_radius, pc_ray.num_textures);//pc_ray.sky_col;
            break;
        }
        

        vec3 cam_hit_pos = payload.pos;
        
        vec3 wo = camera_state.pos - payload.pos;
        float dist = length(payload.pos - camera_state.pos);
        float dist_sqr = dist * dist;
        wo /= dist;
        vec3 n_s = payload.n_s;
        vec3 n_g = payload.n_g;
        bool side = true;
        if (dot(payload.n_g, wo) < 0.)
            n_g = -n_g;
        if (dot(n_g, n_s) < 0) {
            n_s = -n_s;
            side = false;
        }
        float cos_wo = abs(dot(wo, n_s));
        
        const Material mat = load_material(payload.material_idx, payload.uv);
        const bool mat_specular =
            (mat.bsdf_props & BSDF_SPECULAR) == BSDF_SPECULAR;
        // Complete the missing geometry terms
        camera_state.d_vcm *= dist_sqr;
        camera_state.d_vcm /= cos_wo;
        camera_state.d_vc /= cos_wo;
        camera_state.d_vm /= cos_wo;
        // Get the radiance
        if (luminance(mat.emissive_factor) > 0) {


            if (depth > 1)
                col += camera_state.throughput * vcm_get_light_radiance(mat, camera_state, depth);


             if (pc_ray.use_vc == 1 || pc_ray.use_vm == 1) {
                  break;
            }
        }
        
        // Connect to light
        float pdf_rev;
        vec3 f;
        if (!mat_specular && depth < pc_ray.max_depth) {
			if (depth > 1)
                col += vcm_connect_light(n_s, wo, mat, side, eta_vm, camera_state,
                                 pdf_rev, f);
            
        }
        
        // Connect to light vertices
        if (!mat_specular) {

            //if(depth == 1) {
                vec3 connect = vcm_connect_light_vertices(light_path_len, light_path_idx,
                                              depth, n_s, wo, mat, side, eta_vm,
                                              camera_state, pdf_rev);
            
                col += connect;
                //col = vec3(1,1,1);
            //}
                
        }
        
        
        if (depth >= pc_ray.max_depth) {
            break;
        }

        // Scattering
        float pdf_dir;
        float cos_theta;

        f = sample_bsdf(n_s, wo, mat, 1, side, camera_state.wi, pdf_dir,
                        cos_theta, seed);
        

        const bool mat_transmissive =
            (mat.bsdf_props & BSDF_TRANSMISSIVE) == BSDF_TRANSMISSIVE;
        const bool same_hemisphere = same_hemisphere(camera_state.wi, wo, n_s);
        if (f == vec3(0) || pdf_dir == 0 ||
            (!same_hemisphere && !mat_transmissive)) {
            break;
        }
        pdf_rev = pdf_dir;
        if (!mat_specular) {
            pdf_rev = bsdf_pdf(mat, n_s, camera_state.wi, wo);
        }
        const float abs_cos_theta = abs(cos_theta);
        
        camera_state.pos = offset_ray(payload.pos, n_g);
        
        // Note, same cancellations also occur here from now on
        // see _vcm_generate_light_sample_


        if (!mat_specular) {
            camera_state.d_vc =
                ((abs_cos_theta) / pdf_dir) *
                (eta_vm + camera_state.d_vcm + pdf_rev * camera_state.d_vc);
            camera_state.d_vm =
                ((abs_cos_theta) / pdf_dir) *
                (1 + camera_state.d_vcm * eta_vc + pdf_rev * camera_state.d_vm);
            camera_state.d_vcm = 1.0 / pdf_dir;
        } else {
            camera_state.d_vcm = 0;
            camera_state.d_vc *= abs_cos_theta;
            camera_state.d_vm *= abs_cos_theta;
        }


        camera_state.throughput *= f * abs_cos_theta / pdf_dir;
        camera_state.n_s = n_s;
        camera_state.area = payload.area;
        cnt++;
    }

#undef splat

    
    return col;
}
#endif