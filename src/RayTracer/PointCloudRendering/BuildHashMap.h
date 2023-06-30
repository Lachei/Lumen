#pragma once
#include <vector>
#include <iostream>
#include <robin-hood/robin_hood.h>
#include "../../shaders/pcr/pcr_commons.h"
#include "util.h"
#include <bit>

constexpr bool use_linked_list = true;      // uses linked list for the next elements in the hash map, else linear probing is used
constexpr bool compact_linked_list = true;  // compacts the linked list after hash map creation (puts linked lists from the back of the hashmap in empty spots inside the hashmap

// to hash into the hashmap use hash_p()
using HashMap = std::vector<HashMapEntry>;
using EmptySkipMap = std::vector<EmptySkipEntry>;
using OccupVec = std::vector<OccupancyEntry>;
using Data = std::vector<DataEntry>;
struct HashMapInfos{
    size_t                    hash_map_size; // size of the standard hash map table, extra size of hash_map is due to linked lists
    HashMap                   hash_map;
    std::vector<EmptySkipMap> empty_skip_maps;
    std::vector<uint>         empty_skip_sizes;
    OccupVec                  occupancies;
    Data                      data;
};

template<> struct std::hash<ivec3>{
    std::size_t operator()(const ivec3& v) const{
        std::size_t a = std::hash<int>{}(v.x);
        std::size_t b = std::hash<int>{}(v.y);
        std::size_t c = std::hash<int>{}(v.z);
        return c ^ (((a << 1) ^ (b << 2)));
    }
};

inline HashMapInfos create_hash_map(const std::vector<vec3>& points, const std::vector<uint>& colors, dvec3 bounds_min, dvec3 bounds_max, uint empty_skip_layer, float delta_grid){
    struct ColorInfo{uint color, count;};
    auto start = std::chrono::system_clock::now();
    vec3 diff = bounds_max - bounds_min;
    float highest_diff = std::max(std::max(diff.x, diff.y), diff.z);
    uint32_t map_size = points.size() / 10;//std::ceil(points.size() / float(box_per_hash_box_cube));
    uint32_t longest_link{}, longest_skip_link{};
    robin_hood::unordered_set<uint> used_buckets;
    robin_hood::unordered_map<uint, std::vector<ColorInfo>> index_to_colors;
    // trying to create the hash map, if not increasing the map size and retry
    HashMap map(map_size, HashMapEntry{.key = {box_unused, 0, 0}, .next = NEXT_T(-1), .occupancy_index = uint(-1)});
    std::vector<EmptySkipMap> empty_skip_maps(empty_skip_layer, EmptySkipMap(map_size / 8, EmptySkipEntry{.key = {box_unused, 0, 0}, .used_children = {}, .next = NEXT_T(-1)}));
    std::vector<uint> empty_skip_sizes(empty_skip_layer, map_size / 8);
    OccupVec occupancies;
    std::cout << "Starting hash map creation" << std::endl;
    for(size_t point: s_range(points)){
        auto p = points[point];
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
            map_entry->occupancy_index = occupancies.size();
            occupancies.emplace_back();
        }
        // if not empty and the bucket inside the map is another, add to the linked list a new item
        if(!empty && !same_box){
            uint link_length{};
            if constexpr (use_linked_list){
                while(map_entry->key != bucket && map_entry->next != NEXT_T(-1)){
                    ++link_length; 
                    index = map_entry->next;
                    map_entry = &map[index];
                }

                // check if new bucket is needed
                if(map_entry->key != bucket){
                    longest_link = std::max(longest_link, link_length);
                    map_entry->next = NEXT_T(map.size());
                    map.emplace_back(HashMapEntry{.key = bucket, .next = NEXT_T(-1), .occupancy_index = uint(occupancies.size())});
                    map_entry = &map.back();
                    occupancies.emplace_back();
                }
            }
            else{
                // now linear search for free bucket
                while(map_entry->key != bucket && map_entry->next != NEXT_T(-1)){
                    ++link_length;
                    index = (index + map_entry->next) % map_size;
                    map_entry = &map[index];
                }

                // if the bucket does not yet exist, crate new bucket at next free position
                // this includes the new occupancy position
                if(map_entry->key != bucket){
                    while(map[index].key.x != box_unused)
                        index = ++index % map_size;

                    longest_link = std::max(longest_link, link_length);

                    map_entry->next = ((index + map_size) - (map_entry - map.data())) / map_size;
                    map_entry = &map[index];
                    map_entry->occupancy_index = occupancies.size();
                    occupancies.emplace_back();
                }
            }
        }
        // adding the point to the empty skip maps
        assert(empty_skip_maps.size() == empty_skip_sizes.size());
        for(size_t i: s_range(empty_skip_maps)){
            float cur_delta = delta_grid * (2 << i); // delta starts at 2 times the standard delta
            i16vec3 b = bucket_pos(p, cur_delta);
            vec3 b_base = bucket_base(b, cur_delta);
            uint h_empty = hash(b);
            uint empty_index = hash_table_index(h_empty, empty_skip_sizes[i]);

            auto empty_entry = &empty_skip_maps[i][empty_index];
            const bool empty_empty = empty_entry->key.x == box_unused;
            const bool empty_same_box = !empty_empty && empty_entry->key == b;

            uint link_length = 0;
            
            if(empty_empty)
                empty_entry->key = b;
            if(!empty_empty && !empty_same_box){
                // find correct box in linear array
                while(empty_entry->key != b && empty_entry->next != NEXT_T(-1)){
                    empty_index = empty_entry->next;
                    empty_entry = &empty_skip_maps[i][empty_index];
                    ++link_length;
                }
                
                if(empty_entry->key != b){
                    empty_entry->next = NEXT_T(empty_skip_maps[i].size());
                    empty_skip_maps[i].emplace_back(EmptySkipEntry{.key = b, .next = NEXT_T(-1)});
                    empty_entry = &empty_skip_maps[i].back();
                }
            }
            longest_skip_link = std::max(longest_skip_link, link_length);
            set_child_bit(*empty_entry, b_base, p, cur_delta);
        }
        
        auto& occupancy = occupancies[map_entry->occupancy_index];
        bool is_contained = check_bit(occupancy, bucket_b, p, delta_grid);
        set_bit(occupancy, bucket_b, p, delta_grid);
        {
            BIT_CALCS(bucket_b, p, delta_grid);
            uint bit_index = calc_bit_offset(occupancy, bucket_b, p, delta_grid);
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
        occupancies[map[index].occupancy_index].data_index = start_index;
    }
    datas.shrink_to_fit();
    // compacting the whole hash map (trying to move linked list stuff from the back into the map
    if constexpr (use_linked_list && compact_linked_list){
        robin_hood::unordered_map<uint, uint> indices_from_to;
        uint front = 0;
        while(true){
            // advance front pointer to next free bucket
            while(map[front].key.x != box_unused && front < map_size)
                ++front;
            if(front >= map_size || map.size() <= map_size)
                break;
            map[front] = map.back();
            map.pop_back();
            indices_from_to[uint(map.size())] = front;
            used_buckets.insert(front);
        }
        // exchanging the indices which are contained in indices_from_to form key to data value
        for(auto& e: map)
            if(indices_from_to.contains(e.next))
                e.next = indices_from_to[e.next];
        
        // compacting the zero skip maps
        for(size_t i: s_range(empty_skip_maps)){
            front = 0;
            indices_from_to = {};
            auto& cur_map = empty_skip_maps[i];
            uint cur_map_size = empty_skip_sizes[i];
            while(true){
                while(cur_map[front].key.x != box_unused && front < cur_map_size)
                    ++front;
                if(front >= cur_map_size || cur_map.size() <= cur_map_size)
                    break;
                cur_map[front] = cur_map.back();
                cur_map.pop_back();
                indices_from_to[uint(cur_map.size())] = front;
            }
            for(auto& e: cur_map)
                if(indices_from_to.contains(e.next))
                    e.next = indices_from_to[e.next];
        }
    }

    std::cout << "Overall collisions: " << map.size() - map_size << std::endl;
    std::cout << "Overall map size: " << map.size() << "(original: " << map_size << ")" << std::endl;
    std::cout << "Longest linked list: " << longest_link << ", longest skip link " << longest_skip_link << std::endl;
    std::cout << "Bucket usage rate: " << float(used_buckets.size()) / map_size << std::endl;
    map.shrink_to_fit();
    auto end = std::chrono::system_clock::now();
    std::cout << "Hash map creation took " << std::chrono::duration<double>(end - start).count() << " s" << std::endl;
    return {map_size, std::move(map), std::move(empty_skip_maps), std::move(empty_skip_sizes), std::move(occupancies), std::move(datas)};
}
