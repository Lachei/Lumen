#pragma once
#include <ranges>

inline auto s_range(const auto& v){return std::ranges::iota_view(size_t(0), v.size());}
inline auto i_range(auto v){return std::ranges::iota_view(decltype(v)(0), v);}