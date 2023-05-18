#include "../../LumenPCH.h"
#include "BDPTResampled.h"

void BDPTResampled::init() {
	Integrator::init();
	light_path_buffer.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
		instance->width * instance->height * (config.path_length + 1) * sizeof(PathVertex));

	camera_path_buffer.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
		instance->width * instance->height * (config.path_length + 1) * sizeof(PathVertex));

	color_storage_buffer.create(
		&instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE, instance->width * instance->height * 3 * 4);

	global_light_reservoir_buffer.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE, 
			instance->width * instance->height * sizeof(LightResampleReservoir));

	global_light_spatial_reservoir_buffer.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE, 
			instance->width * instance->height * sizeof(LightResampleReservoir));

	SceneDesc desc;
	desc.vertex_addr = vertex_buffer.get_device_address();
	desc.index_addr = index_buffer.get_device_address();
	desc.normal_addr = normal_buffer.get_device_address();
	desc.uv_addr = uv_buffer.get_device_address();
	desc.material_addr = materials_buffer.get_device_address();
	desc.prim_info_addr = prim_lookup_buffer.get_device_address();
	// BDPT
	desc.light_path_addr = light_path_buffer.get_device_address();
	desc.camera_path_addr = camera_path_buffer.get_device_address();
	desc.color_storage_addr = color_storage_buffer.get_device_address();
	desc.global_light_reservoirs_addr = global_light_reservoir_buffer.get_device_address();
	desc.global_light_spatial_addr = global_light_spatial_reservoir_buffer.get_device_address();

	scene_desc_buffer.create(
		&instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE, sizeof(SceneDesc), &desc, true);
	pc_ray.frame_num = 0;
	pc_ray.size_x = instance->width;
	pc_ray.size_y = instance->height;
	assert(instance->vkb.rg->settings.shader_inference == true);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, prim_info_addr, &prim_lookup_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_path_addr, &light_path_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, camera_path_addr, &camera_path_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, color_storage_addr, &color_storage_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, global_light_reservoirs_addr, &global_light_reservoir_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, global_light_spatial_addr, &global_light_spatial_reservoir_buffer, instance->vkb.rg);
}

void BDPTResampled::render() {
	constexpr bool use_spatial_reservoirs = true;

	CommandBuffer cmd(&instance->vkb.ctx, /*start*/ true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	pc_ray.num_lights = (int)lights.size();
	pc_ray.time = rand() % UINT_MAX;
	pc_ray.max_depth = config.path_length;
	pc_ray.sky_col = config.sky_col;
	pc_ray.total_light_area = total_light_area;
	pc_ray.light_triangle_count = total_light_triangle_cnt;
	pc_ray.dir_light_idx = lumen_scene->dir_light_idx;
	pc_ray.env_tex_idx = lumen_scene->env_tex_idx;
	
	// executing the spatial resampling
	const uint32_t spatial_dim_x = static_cast<uint32_t>(std::ceil(instance->width * instance->height / 1024.f));
	std::vector<ShaderMacro> resample_macros;
	if(use_spatial_reservoirs){
		instance->vkb.rg
			->add_compute("Spatial Resample", {.shader = Shader("src/shaders/integrators/bdpt/bdpt_resample_spatial.comp"),
											   .dims = {spatial_dim_x, 1, 1}})
			.push_constants(&pc_ray)
			.bind(scene_desc_buffer);
		resample_macros.emplace_back("USE_SPATIAL_RESERVOIRS");
	}

	// doing the reaytracing + temporal resampling update
	instance->vkb.rg
		->add_rt("BDPTResampled",
				 {

					 .shaders = {{"src/shaders/integrators/bdpt/bdpt_resample.rgen"},
								 {"src/shaders/ray.rmiss"},
								 {"src/shaders/ray_shadow.rmiss"},
								 {"src/shaders/ray.rchit"},
								 {"src/shaders/ray.rahit"}},
					 .macros = std::move(resample_macros),
					 .dims = {instance->width, instance->height},
					 .accel = instance->vkb.tlas.accel})
		.zero(light_path_buffer)
		.zero(camera_path_buffer)
		//.read(light_path_buffer) // Needed if shader inference is disabled
		//.read(camera_path_buffer)
		.push_constants(&pc_ray)
		//.write(output_tex)
		.bind(std::initializer_list<ResourceBinding>{
			output_tex,
			scene_ubo_buffer,
			scene_desc_buffer,
		})
		.bind(mesh_lights_buffer)
		.bind_texture_array(scene_textures)
		.bind_tlas(instance->vkb.tlas);

	instance->vkb.rg->run_and_submit(cmd);
}

bool BDPTResampled::update() {
	pc_ray.frame_num++;
	bool updated = Integrator::update();
	if (updated) {
		pc_ray.frame_num = 0;
		//std::cout << "Reset frame number" << std::endl;
	}
	return updated;
}

void BDPTResampled::destroy() {
	const auto device = instance->vkb.ctx.device;
	Integrator::destroy();
	std::vector<Buffer*> buffer_list = {&light_path_buffer, &camera_path_buffer, &color_storage_buffer, &global_light_reservoir_buffer, &global_light_spatial_reservoir_buffer};
	for (auto b : buffer_list) {
		b->destroy();
	}
}
