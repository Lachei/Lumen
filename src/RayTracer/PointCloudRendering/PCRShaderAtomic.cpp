#include "LumenPCH.h"
#include "PCRShaderAtomic.h"
#include "LASFile.h"
#include "util.h"

#define PRINT_FRAME_TIME

void PCRShaderAtomic::init() {
	// Loading the point cload data
	if(!config.count("point_cloud"))
		std::cout << "Missing point cloud file for point cloud rendering" << std::endl;
	LASFile las_file(config["point_cloud"].get<std::string>());
	auto data = las_file.load_point_cloud_data();
	dvec3 bounds_min= dvec3(las_file.header.min_x, las_file.header.min_z, las_file.header.min_y);
	dvec3 bounds_max= dvec3(las_file.header.max_x, las_file.header.max_z, las_file.header.max_y);
	normalize_positions_inplace(data.positions, bounds_min, bounds_max);
	//auto new_dat = data;
	//for(int i: i_range(3)){
	//	new_dat.positions.insert(new_dat.positions.end(), data.positions.begin(), data.positions.end());
	//	new_dat.colors.insert(new_dat.colors.begin(), data.colors.begin(), data.colors.end());
	//}
	//data = std::move(new_dat);
	point_count = data.positions.size();
	std::cout << "Loaded " << data.positions.size() << " data points" << std::endl;

	Integrator::init();

	point_positions.create("Point positions", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  data.positions.size() * sizeof(data.positions[0]), data.positions.data(), true);
	point_colors.create("Point colors", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  data.colors.size() * sizeof(data.colors[0]), data.colors.data(), true); 
	image_depth_buffer.create("Image depth buffer", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  sizeof(uint64_t) * instance->width * instance->height);
								  
	ShaderAtomic constant_infos{
		.point_count = uint(data.positions.size()),
		.positions_addr = point_positions.get_device_address(),
		.colors_addr = point_colors.get_device_address(),
		.depth_image_addr = image_depth_buffer.get_device_address(),
	};
	constant_infos_buffer.create("Constant infos", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
							  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							  sizeof(constant_infos), &constant_infos, true);

	pc.size_x = instance->width;
	pc.size_y = instance->height;
	pc.info_addr = constant_infos_buffer.get_device_address();
	assert(instance->vkb.rg->settings.shader_inference == true);

	// For shader resource dependency inference, use this macro to register a buffer address to the rendergraph
	REGISTER_BUFFER_WITH_ADDRESS(ShaderAtomic, constant_infos, positions_addr, &point_positions, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(ShaderAtomic, constant_infos, colors_addr, &point_colors, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(ShaderAtomic, constant_infos, depth_image_addr, &image_depth_buffer, instance->vkb.rg);
}

void PCRShaderAtomic::render() {
	CommandBuffer cmd(&instance->vkb.ctx, /*start*/ true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	pc.cam_matrix = camera->projection * camera->view;
	instance->vkb.rg
		->add_compute("Render Points",
					 {.shader = Shader("src/shaders/pcr/pcr_shader_atomic.comp"),
					  .dims = {(uint32_t)std::ceil(point_count / float(shader_atomic_size_x)), 1, 1}})
		.zero(image_depth_buffer)
		.push_constants(&pc);

	instance->vkb.rg
		->add_compute("Resolve Color",
					 {.shader = Shader("src/shaders/pcr/pcr_resolve_color.comp"),
					  .dims = {(uint32_t)std::ceil(pc.size_x * pc.size_y / float(shader_atomic_size_x)), 1, 1}})
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

bool PCRShaderAtomic::update() {
	bool updated = Integrator::update();
	return updated;
}

void PCRShaderAtomic::destroy() { 
	Integrator::destroy(); 
	point_positions.destroy();
	point_colors.destroy();
	image_depth_buffer.destroy();
	constant_infos_buffer.destroy();
}
