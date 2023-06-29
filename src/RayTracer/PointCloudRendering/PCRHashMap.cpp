#include "LumenPCH.h"
#include "PCRHashMap.h"
#include "LASFile.h"
#include "BuildHashMap.h"

//#define PRINT_FRAME_TIME

void PCRHashMap::init() {
	// Loading the point cload data
	if(!config.count("point_cloud"))
		std::cout << "Missing point cloud file for point cloud rendering" << std::endl;
	LASFile las_file(config["point_cloud"].get<std::string>());
	auto data = las_file.load_point_cloud_data();
	vec3 bounds_min= vec3(las_file.header.min_x, las_file.header.min_z, las_file.header.min_y);
	vec3 bounds_max= vec3(las_file.header.max_x, las_file.header.max_z, las_file.header.max_y);
	constexpr uint bins_per_side = 4000;
	constexpr uint levels = 6; // amount of empty skip layers above the base layer (The top level has 2^levels * 8 blocks as children)
	float delta_grid = std::max({(bounds_max.x - bounds_min.x) / bins_per_side, (bounds_max.y - bounds_min.y) / bins_per_side, (bounds_max.z - bounds_min.z) / bins_per_side});

	auto [map_size, map, empty_skip_maps, empty_skip_sizes, occupancies, color_data] = create_hash_map(data.positions, data.colors, bounds_min, bounds_max, levels, delta_grid);
	//auto new_dat = data;
	//for(int i: i_range(3)){
	//	new_dat.positions.insert(new_dat.positions.end(), data.positions.begin(), data.positions.end());
	//	new_dat.colors.insert(new_dat.colors.begin(), data.colors.begin(), data.colors.end());
	//}
	//data = std::move(new_dat);
	point_count = data.positions.size();
	std::cout << "Loaded " << data.positions.size() << " data points" << std::endl;

	Integrator::init();

	hash_map_buffer.create("Hash map", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  map.size() * sizeof(map[0]), map.data(), true); 
							  
	occupancies_buffer.create("Occupancies", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  occupancies.size() * sizeof(occupancies[0]), occupancies.data(), true); 
								  
	data_buffer.create("Point colors", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  color_data.size() * sizeof(color_data[0]), color_data.data(), true); 

	int c{};
	std::vector<uint> empty_skip_infos_data{levels};
	for(auto& empty_skip_buffer: empty_skip_buffers){
		empty_skip_buffer.create(("Empty skip buffer " + std::to_string(c)).c_str(), &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
								VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
								empty_skip_maps.front().size() * sizeof(empty_skip_maps.front()[0]), empty_skip_maps.front().data(), true); 
		empty_skip_infos_data.emplace_back(empty_skip_sizes[c++]);
		// CARE: the address might be wrong endian
		uint64_t addr = empty_skip_buffer.get_device_address();
		empty_skip_infos_data.emplace_back(uint(addr >> 32));
		empty_skip_infos_data.emplace_back(uint(addr));
	}

	empty_skip_infos.create("Empty skip infos", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  sizeof(EmptySkipInfos::levels) + sizeof(EmptySkipInfo) * levels, empty_skip_infos_data.data(), true);

	HashMapConstants constant_infos{
		.bounds_min = bounds_min,
		.bounds_max = bounds_max,
		.delta_grid = delta_grid,
		.hash_map_size = uint(map_size),
		.hash_map_addr = hash_map_buffer.get_device_address(),
		.occupancies_addr = occupancies_buffer.get_device_address(),
		.data_addr = data_buffer.get_device_address(),
		.empty_infos_addr = empty_skip_infos.get_device_address()
	};
	constant_infos_buffer.create("Constant infos", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
							  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  sizeof(constant_infos), &constant_infos, true);

	pc.size_x = instance->width;
	pc.size_y = instance->height;
	pc.info_addr = constant_infos_buffer.get_device_address();
	assert(instance->vkb.rg->settings.shader_inference == true);

	// For shader resource dependency inference, use this macro to register a buffer address to the rendergraph
	REGISTER_BUFFER_WITH_ADDRESS(PC, pc, info_addr, &constant_infos_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(HashMapConstants, constant_infos, hash_map_addr, &hash_map_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(HashMapConstants, constant_infos, occupancies_addr, &occupancies_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(HashMapConstants, constant_infos, data_addr, &data_buffer, instance->vkb.rg);
}

void PCRHashMap::render() {
	CommandBuffer cmd(&instance->vkb.ctx, /*start*/ true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	pc.cam_matrix = camera->projection * camera->view;
	pc.cam_view_inv = glm::inverse(camera->view);
	pc.cam_proj_inv = glm::inverse(camera->projection);
	uint32_t size_x = static_cast<uint32_t>(std::ceil(pc.size_x / float(hash_map_size_xy.x)));
	uint32_t size_y = static_cast<uint32_t>(std::ceil(pc.size_y / float(hash_map_size_xy.y)));
	instance->vkb.rg
		->add_compute("Render Points",
					 {.shader = Shader("src/shaders/pcr/pcr_hash_map.comp"),
					  .dims = {size_x, size_y, 1}})
		.push_constants(&pc)
		.bind(output_tex);

#ifdef PRINT_FRAME_TIME
	auto start = std::chrono::system_clock::now();
	vk::check(vkDeviceWaitIdle(instance->vkb.ctx.device), "Device wait error");
#endif

	instance->vkb.rg->run_and_submit(cmd);

#ifdef PRINT_FRAME_TIME
	vk::check(vkDeviceWaitIdle(instance->vkb.ctx.device), "Device wait error");
	auto end = std::chrono::system_clock::now();
	auto dur = std::chrono::duration<double>(end - start).count();
	static double average = 0;
	static int count = 0;
	++count;
	average = 1. / count * dur + (count - 1.) / count * average;
	dur = average;
	std::cout << "Frametime: " << dur << " s, " << 1 / dur << " fps" << std::endl;
#endif
}

bool PCRHashMap::update() {
	bool updated = Integrator::update();
	return updated;
}

void PCRHashMap::destroy() { 
	Integrator::destroy(); 
	constant_infos_buffer.destroy();
	hash_map_buffer.destroy();
	occupancies_buffer.destroy();
	data_buffer.destroy();
	empty_skip_infos.destroy();
	for(auto& empty_skip_buffer: empty_skip_buffers)
		empty_skip_buffer.destroy();
}
