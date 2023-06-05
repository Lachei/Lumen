#include "../../LumenPCH.h"
#include "BDPTResampled.h"
#include <ranges>

#define DEBUG_RESAMPLE

//#ifdef DEBUG_RESAMPLE
Buffer resample_positions;
struct Weights{float w, w_sum, p_h; uint m;};
Buffer resample_weights;	// contains weights as well as weight sum p_hat and m (tuple of <float, float, float, uint>)
#endif

template<typename T>
inline std::ranges::iota_view<size_t> s_range(const T& v){return std::ranges::iota_view(size_t(0), v.size());}

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
}

void BDPTResampled::render() {
	constexpr bool use_spatial_reservoirs = false;

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
		//.zero(light_path_buffer)
		//.zero(camera_path_buffer)
		.push_constants(&pc_ray)
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
