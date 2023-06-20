#pragma once
#include <vector>
#include <iostream>
#include <robin-hood/robin_hood.h>
#include "../../shaders/pcr/pcr_commons.h"
#include "util.h"
#include <functional>

// to hash into the hashmap use hash_p()
using HashMap = std::vector<HashMapEntry>;
template<> struct std::hash<ivec3>{
    std::size_t operator()(const ivec3& v) const{
        std::size_t a = std::hash<int>{}(v.x);
        std::size_t b = std::hash<int>{}(v.y);
        std::size_t c = std::hash<int>{}(v.z);
        return c ^ (((a << 1) ^ (b << 2)));
    }
};

inline HashMap create_hash_map(const std::vector<vec3>& points, float delta_grid){
    auto start = std::chrono::system_clock::now();
    uint32_t map_size = points.size() / 100;//std::ceil(points.size() / float(box_per_hash_box_cube));
    uint32_t longest_link{};
    robin_hood::unordered_set<uint> used_buckets;
    robin_hood::unordered_set<ivec3> different_buckets;
    // trying to create the hash map, if not increasing the map size and retry
    HashMap map(map_size, HashMapEntry{.key = {box_unused, 0, 0}, .occupancy = {}, .next = uint(-1)});
    for(const auto& p: points){
        ivec3 bucket = bucket_pos(p, delta_grid);
        vec3 bucket_b = bucket_base(bucket, delta_grid);
        uint h = hash(bucket);
        uint index = hash_table_index(h, map_size);
        used_buckets.insert(index);
        //std::cout << "Bucket[" << bucket.x << ", " << bucket.y << ", " << bucket.z << "], index " << index << std::endl;
        auto map_entry = &map[index];
        const bool empty = map_entry->key.x == box_unused;
        const bool same_box = !empty && map_entry->key == bucket;
        // if empty fill the bucket index
        if(empty){
            map_entry->key = bucket;
        }
        // if not empty and the bucket inside the map is another, add to the linked list a new item
        if(!empty && !same_box){
            uint link_length{};
            while(map_entry->key != bucket && map_entry->next != uint(-1)){
                ++link_length;
                map_entry = &map[map_entry->next];
            }
            longest_link = std::max(longest_link, link_length);
            map_entry->next = map.size();
            map.emplace_back(HashMapEntry{bucket, {}, uint(-1)});
            map_entry = &map.back();
        }
        set_bit(*map_entry, bucket_b, p, delta_grid);
    }
    // checking the percentage of used buckets
    //uint used_buckets{};
    //for(size_t i: i_range(map_size))
    //    if(map[i].key.x != box_unused)
    //        ++used_buckets;
    std::cout << "Overall collisions: " << map.size() - map_size << std::endl;
    std::cout << "Overall map size: " << map.size() << "(original: " << map_size << ")" << std::endl;
    std::cout << "Longest linked list: " << longest_link << std::endl;
    std::cout << "Bucket usage rate: " << float(used_buckets.size()) / map_size << std::endl;
    map.shrink_to_fit();
    auto end = std::chrono::system_clock::now();
    std::cout << "Hash map creation took " << std::chrono::duration<double>(end - start).count() << " s" << std::endl;
    return map;
}
