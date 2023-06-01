#ifndef BDPT_RESAMPLING_COMMONS
#define BDPT_RESAMPLING_COMMONS

#include "../commons.h"

#define store_light_resampled(array, reservoir) array.d[pixel_idx].w_sum = reservoir.w_sum;\
    array.d[pixel_idx].w = reservoir.w;\
    array.d[pixel_idx].m = reservoir.m;\
    array.d[pixel_idx].pos = reservoir.pos;\
    array.d[pixel_idx].dir = reservoir.dir;\
    array.d[pixel_idx].n = reservoir.n;\
    array.d[pixel_idx].L = reservoir.L;\
    array.d[pixel_idx].p_h = reservoir.p_h;

#define store_light_resampled_idx(array, reservoir, idx) array.d[idx].w_sum = reservoir.w_sum;\
    array.d[idx].w = reservoir.w;\
    array.d[idx].m = reservoir.m;\
    array.d[idx].pos = reservoir.pos;\
    array.d[idx].dir = reservoir.dir;\
    array.d[idx].n = reservoir.n;\
    array.d[idx].L = reservoir.L;\
    array.d[idx].p_h = reservoir.p_h;

#define load_light_resampled(array, reservoir) reservoir.w_sum = array.d[pixel_idx].w_sum;\
    reservoir.w = array.d[pixel_idx].w;\
    reservoir.m = array.d[pixel_idx].m;\
    reservoir.pos = array.d[pixel_idx].pos;\
    reservoir.dir = array.d[pixel_idx].dir;\
    reservoir.n = array.d[pixel_idx].n;\
    reservoir.L = array.d[pixel_idx].L;\
    reservoir.p_h = array.d[pixel_idx].p_h;

#define load_light_resampled_idx(array, reservoir, idx) reservoir.w_sum = array.d[idx].w_sum;\
    reservoir.w = array.d[idx].w;\
    reservoir.m = array.d[idx].m;\
    reservoir.pos = array.d[idx].pos;\
    reservoir.dir = array.d[idx].dir;\
    reservoir.n = array.d[idx].n;\
    reservoir.L = array.d[idx].L;\
    reservoir.p_h = array.d[idx].p_h;

// Adds a new sample to the reservoir. The reservoir is updated inplace
// @return true if the new sample is the new active sample
bool reservoir_add_light_sample(inout LightResampleReservoir reservoir, in float weight, in vec3 pos, in vec3 dir, in vec3 n, in vec3 L, float p_h){
    float rand = rand(seed);
    reservoir.w_sum += weight;
    reservoir.m     += 1;
    const float frac = weight / reservoir.w_sum;
    if(rand <= frac){   // smaller equal needed to make sure that if the reservoir was empty (resulting in frac = 1.) the new sample is always accepted
        // sample should be exchanged with the current sample
        reservoir.w = weight;
        reservoir.p_h = p_h;
        reservoir.pos = pos;
        reservoir.dir = dir;
        reservoir.n = n;
        reservoir.L = L;
        return true;
    }
    return false;
}

// combines reservoirs a and b and puts the resulting reservoir into a
// @ return true if the sample in a changed to the sample in b, false else
bool reservoirs_combine(inout LightResampleReservoir a, in LightResampleReservoir b) {
    float rand = rand(seed);   
    a.w_sum += b.w_sum;
    a.m     += b.m;
    const float frac = b.w_sum / a.w_sum;
    if(rand <= frac){
        a.w = b.w;
        a.p_h = b.p_h;
        a.pos = b.pos;
        a.dir = b.dir;
        a.n = b.n;
        a.L = b.L;
        return true;
    }
    return false;
}

#endif