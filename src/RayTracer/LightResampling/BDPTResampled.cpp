#include "../../LumenPCH.h"
#include "BDPTResampled.h"
#include <ranges>

//#define DEBUG_RESAMPLE

#ifdef DEBUG_RESAMPLE
Buffer resample_positions;
struct Weights{float w, w_sum, p_h; uint m;};
Buffer resample_weights;	// contains weights as well as weight sum p_hat and m (tuple of <float, float, float, uint>)
#endif

// changing the implementation to use less lighting reservoirs than pixels.
// this means that the whole sampling spawn process will be updated:
// 		1. Adding a new compute pipeline to create new spawn points.
//		   This pipeline is added after the spatial resampling step and simply samples a new light, and combines that sample with the
//		   spatial reservoirs. The resulting sample is stored in a separate buffer which holds only the seed and the probability for each sample
// 		2. The raygen shader for the integration stage then takes these samples and calculates lighting with them
//		3. The luminance for each sample is stored in an extra buffer (contains only 1 float per pixel)
//		4. A compute shader averages the lighting contributions for each lighting reservoir
//		   and uses the luminance to update the reservoirs accordingly

template<typename T>
inline std::ranges::iota_view<size_t> s_range(const T& v){return std::ranges::iota_view(size_t(0), v.size());}

constexpr uint32_t reduction_fac = 2;
void BDPTResampled::init() {
	light_spawn_position_width = (instance->width + reduction_fac - 1) / reduction_fac;
	light_spawn_position_height = (instance->height + reduction_fac - 1) / reduction_fac;

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

	light_spawn_positions.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
		light_spawn_position_width * light_spawn_position_height * sizeof(LightStartingPosition));
	
	light_spawn_position_weights.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
		instance->width * instance->height * sizeof(float));
	
#ifdef DEBUG_RESAMPLE
	resample_positions.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_SHARING_MODE_EXCLUSIVE,
			instance->width * instance->height * sizeof(vec3));
	resample_weights.create(
		&instance->vkb.ctx,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_SHARING_MODE_EXCLUSIVE,
			instance->width * instance->height * sizeof(Weights));
#endif

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
	desc.light_spawn_position_addr = light_spawn_positions.get_device_address();
	desc.light_spawn_position_weights_addr = light_spawn_position_weights.get_device_address();

#ifdef DEBUG_RESAMPLE
	desc.debug_resample_positions_addr = resample_positions.get_device_address();
	desc.debug_resample_weights_addr = resample_weights.get_device_address();
#endif

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
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_spawn_positionn_addr, &light_spawn_positions, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_spawn_positon_weights_addr, &light_spawn_position_weights, instance->vkb.rg);
}

void BDPTResampled::render() {
	constexpr bool use_spatial_reservoirs = true;

#ifdef DEBUG_RESAMPLE
	if(pc_ray.frame_num && pc_ray.frame_num < 10){
		std::cout << "Light reservoir output for frame " << pc_ray.frame_num << std::endl;

		std::ofstream file("reservoirs/temporal" + std::to_string(pc_ray.frame_num) + ".csv");
		assert(file);

		file << "pos_x,pos_y,pos_z,w_sum,w,p_h,m\n";

		resample_positions.map(); // maps the memory to the data pointer
		std::vector<vec3> reservoir_positons(instance->width * instance->height);
		vec3* data_p = reinterpret_cast<vec3*>(resample_positions.data);
		std::copy(data_p, data_p + reservoir_positons.size(), reservoir_positons.begin());
		resample_positions.unmap();

		resample_weights.map();
		std::vector<Weights> reservoir_weights(reservoir_positons.size());
		Weights* weight_p = reinterpret_cast<Weights*>(resample_weights.data);
		std::copy(weight_p, weight_p + reservoir_weights.size(), reservoir_weights.begin());
		resample_weights.unmap();
		// writeout into csv file
		for(size_t i: std::views::iota(size_t(0), reservoir_positons.size()))
			// assembling the line for writeout			
			file << reservoir_positons[i].x << ',' << reservoir_positons[i].y << ',' << reservoir_positons[i].z << ','
				 << reservoir_weights[i].w << ',' << reservoir_weights[i].w_sum << ',' << reservoir_weights[i].p_h << ',' << reservoir_weights[i].m << '\n';
		
	}
#endif
	

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
	const uint32_t reduced_dim_x = static_cast<uint32_t>(std::ceil(instance->width * instance->height / 1024.f / reduction_fac / reduction_fac));
	std::vector<ShaderMacro> resample_macros;
	if(use_spatial_reservoirs)
		resample_macros.emplace_back("USE_SPATIAL_RESERVOIRS", "BDPT_RESAMPLE");

	// create the new sample
	instance->vkb.rg
		->add_compute("Spatial Resample", {.shader = Shader("src/shaders/integrators/bdpt/bdpt_resample_spatial.comp"),
											.dims = {reduced_dim_x, 1, 1}})
		.push_constants(&pc_ray)
		.bind(scene_desc_buffer);

	// doing the reaytracing + temporal resampling update
	instance->vkb.rg
		->add_rt("BDPTResampled",
				 {
					 .shaders = {{"src/shaders/integrators/bdpt/bdpt_resample.rgen"},
								 {"src/shaders/ray.rmiss"},
								 {"src/shaders/ray_shadow.rmiss"},
								 {"src/shaders/ray.rchit"},
								 {"src/shaders/ray.rahit"}},
					 .macros = resample_macros,
					 .dims = {instance->width, instance->height},
					 .accel = instance->vkb.tlas.accel})
		.push_constants(&pc_ray)
		.bind(std::initializer_list<ResourceBinding>{
			output_tex,
			scene_ubo_buffer,
			scene_desc_buffer,
		})
		.bind(mesh_lights_buffer)
		.bind_texture_array(scene_textures)
		.bind_tlas(instance->vkb.tlas);

	// updating the reservoirs with the ray traced lighting information
	instance->vkb.rg
		->add_compute("Reservoir update", {.shader = Shader("src/shader/integrators/bdpt/update_light_samples.comp"),
										   .macros = resample_macros,
										   .dims = {reduced_dim_x, 1, 1}})
		.push_constants(&pc_ray)
		.bind(scene_desc_buffer);

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
