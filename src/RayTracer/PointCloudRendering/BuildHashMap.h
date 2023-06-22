#pragma once
#include <vector>
#include <iostream>
#include <robin-hood/robin_hood.h>
#include "../../shaders/pcr/pcr_commons.h"
#include "util.h"
#include <bit>

// to hash into the hashmap use hash_p()
using HashMap = std::vector<HashMapEntry>;
using Data = std::vector<DataEntry>;
struct HashMapInfos{
    size_t hash_map_size; // size of the standard hash map table, extra size of hash_map is due to linked lists
    HashMap hash_map;
    Data data;
};
template<> struct std::hash<ivec3>{
    std::size_t operator()(const ivec3& v) const{
        std::size_t a = std::hash<int>{}(v.x);
        std::size_t b = std::hash<int>{}(v.y);
        std::size_t c = std::hash<int>{}(v.z);
        return c ^ (((a << 1) ^ (b << 2)));
    }
};

inline HashMapInfos create_hash_map(const std::vector<vec3>& points, const std::vector<uint>& colors, float delta_grid){
    struct ColorInfo{uint color, count;};
    auto start = std::chrono::system_clock::now();
    uint32_t map_size = points.size() / 10;//std::ceil(points.size() / float(box_per_hash_box_cube));
    uint32_t longest_link{};
    robin_hood::unordered_set<uint> used_buckets;
    robin_hood::unordered_map<uint, std::vector<ColorInfo>> index_to_colors;
    // trying to create the hash map, if not increasing the map size and retry
    HashMap map(map_size, HashMapEntry{.key = {box_unused, 0, 0}, .next = uint(-1), .data_index = uint(-1), .occupancy = {}});
    std::cout << "Starting hash map creation" << std::endl;
    for(size_t point: s_range(points)){
        const auto& p = points[point];
        auto col = colors[point];
        i16vec3 bucket = bucket_pos(p, delta_grid);
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
            // now linear search for free bucket
            while(map_entry->key != bucket && map_entry->next != int16_t(-1)){
                ++link_length;
                index = (index + map_entry->next) % map_size;
                map_entry = &map[index];
            }
            while(map_entry[index].key.x != box_unused)
                index = ++index % map_size;
            
            longest_link = std::max(longest_link, link_length);

            map_entry->next = ((index + map_size) - (map_entry - map.data())) / map_size;
            map_entry = &map[index];
        }
        bool is_contained = check_bit(*map_entry, bucket_b, p, delta_grid);
        set_bit(*map_entry, bucket_b, p, delta_grid);
        {
            BIT_CALCS(bucket_b, p, delta_grid);
            uint bit_index = calc_bit_offset(*map_entry, bucket_b, p, delta_grid);
            auto& cur_vec = index_to_colors[uint(map_entry - map.data())];
            if(!is_contained)
                cur_vec.insert(cur_vec.begin() + bit_index, ColorInfo{.color = col, .count = 1});
            else{
                cur_vec[bit_index].count++;
                uint c = cur_vec[bit_index].count;
                vec3 a = uint_col_to_vec(col);
                vec3 b = uint_col_to_vec(cur_vec[bit_index].color);
                a = 1.f / c * a + float(c - 1) / c * b;
                cur_vec[bit_index].color = vec_col_to_uint(a);
            }
        }

    }
    // collecting the colors into the final vector and setting the offset pointer in the map
    std::cout << "Starting conversion of color data" << std::endl;
    Data datas;
    for(const auto& [index, col_infos]: index_to_colors){
        uint start_index = uint(datas.size());
        for(const auto& col_info: col_infos)
            datas.emplace_back(col_info.color);
        map[index].data_index = start_index;
    }
    datas.shrink_to_fit();
    // compacting the whole hash map (trying to move linked list stuff from the back into the map
    //robin_hood::unordered_map<uint, uint> indices_from_to;
    //uint front = 0;
    //while(true){
    //    // advance front pointer to next free bucket
    //    while(map[front].key.x != box_unused && front < map_size)
    //        ++front;
    //    if(front >= map_size)
    //        break;
    //    map[front] = map.back();
    //    map.pop_back();
    //    indices_from_to[uint(map.size())] = front;
    //    used_buckets.insert(front);
    //}
    //// exchanging the indices which are contained in indices_from_to form key to data value
    //for(auto& e: map)
    //    if(indices_from_to.contains(e.next))
    //        e.next = indices_from_to[e.next];

    std::cout << "Overall collisions: " << map.size() - map_size << std::endl;
    std::cout << "Overall map size: " << map.size() << "(original: " << map_size << ")" << std::endl;
    std::cout << "Longest linked list: " << longest_link << std::endl;
    std::cout << "Bucket usage rate: " << float(used_buckets.size()) / map_size << std::endl;
    map.shrink_to_fit();
    auto end = std::chrono::system_clock::now();
    std::cout << "Hash map creation took " << std::chrono::duration<double>(end - start).count() << " s" << std::endl;
    return {map_size, std::move(map), std::move(datas)};
}
