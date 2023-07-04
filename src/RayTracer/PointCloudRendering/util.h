#pragma once
#include <ranges>

inline auto s_range(const auto& v){return std::ranges::iota_view(size_t(0), v.size());}
inline auto i_range(auto v){return std::ranges::iota_view(decltype(v)(0), v);}

inline dvec3 el_wise_min(const dvec3& a, const dvec3& b){
	return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline dvec3 el_wise_max(const dvec3& a, const dvec3& b){
	return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
inline void normalize_positions_inplace(std::span<vec3> positions, dvec3& bounds_min, dvec3& bounds_max){
	dvec3 diffs = bounds_max - bounds_min;
	double div = std::max(std::max(diffs.x, diffs.y), diffs.z) * .01;
	dvec3 min_pos;
	dvec3 max_pos;
	for(auto& p: positions){
		p = (dvec3(p) - bounds_min) / div * 2. - diffs / div;
		min_pos = el_wise_min(min_pos, p);
		max_pos = el_wise_max(max_pos, p);
	}
	bounds_min = -diffs / div;
	bounds_max = diffs / div;
}