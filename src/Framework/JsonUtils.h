#pragma once
#include <tinygltf/json.hpp>

namespace json_util{
template<typename T>
T get_or(const nlohmann::json& json, const std::string& field, const T& default_val){
    if(!json.is_null() && json.count(field) > 1)
        return json[field].get<T>();
    return default_val;
}
}