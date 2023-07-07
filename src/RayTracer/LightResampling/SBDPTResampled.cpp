#include "LumenPCH.h"
#include "SBDPTResampled.h"
#include "shaders/integrators/sbdpt/sbdpt_commons.h"

constexpr bool use_vc = true;
void SBDPTResampled::init() { 
	Integrator::init();
	light_state_buffer.create("Light States", &instance->vkb.ctx,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							instance->width * instance->height * sizeof(LightState));
	light_vertices_buffer.create("Light Vertices", &instance->vkb.ctx,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							instance->width * instance->height * (lumen_scene->config.path_length + 1) * sizeof(VCMVertex));
	color_storage_buffer.create("Color Storage", &instance->vkb.ctx,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							instance->width * instance->height * 3 * sizeof(float));
	light_path_cnt_buffer.create("Light Path Count", &instance->vkb.ctx,
								 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
									 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
								 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
								 instance->width * instance->height * sizeof(float));
	temporal_light_origin_reservoirs.create("Temporal Reservoirs", &instance->vkb.ctx,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							2 * instance->width * instance->height * sizeof(LightHitReservoir));
	light_transfer_buffer.create("Light Transfer Storage", &instance->vkb.ctx,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							instance->width * instance->height * sizeof(LightTransferState));
	 spatial_light_origin_reservoirs.create("Spatial Reservoirs", &instance->vkb.ctx,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
								VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							2 * instance->width * instance->height * sizeof(LightHitReservoir));

	light_vertices_reservoirs.create(
		"Light Vertices Reservoirs", &instance->vkb.ctx,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
							2 * instance->width * instance->height * (lumen_scene->config.path_length + 1) * sizeof(VCMVertex));
	light_path_reservoirs.create("Light Path Reservoirs", &instance->vkb.ctx,
											VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
											2 * instance->width * instance->height * sizeof(LightPathReservoir));

	temporal_gi_reservoir_buffer.create("Temporal GI Reservoirs", &instance->vkb.ctx,
									 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
										 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
									 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
									 2 * instance->width * instance->height * sizeof(Reservoir));

	restir_gi_samples_buffer.create("SBDPT GI Samples", &instance->vkb.ctx,
								 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
									 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
								 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
								 instance->width * instance->height * sizeof(ReservoirSample));

	SceneDesc desc;
	desc.vertex_addr = vertex_buffer.get_device_address();
	desc.index_addr = index_buffer.get_device_address();
	desc.normal_addr = normal_buffer.get_device_address();
	desc.uv_addr = uv_buffer.get_device_address();
	desc.material_addr = materials_buffer.get_device_address();
	desc.prim_info_addr = prim_lookup_buffer.get_device_address();

	desc.light_state_addr = light_state_buffer.get_device_address();
	desc.light_vertices_addr = light_vertices_buffer.get_device_address();
	desc.color_storage_addr = color_storage_buffer.get_device_address();
	desc.path_cnt_addr = light_path_cnt_buffer.get_device_address();
	desc.temporal_light_origin_reservoirs_addr = temporal_light_origin_reservoirs.get_device_address();
	desc.light_transfer_addr = light_transfer_buffer.get_device_address();
	//desc.spatial_light_origin_reservoirs_addr = spatial_light_origin_reservoirs.get_device_address();
	desc.light_vertices_reservoirs_addr = light_vertices_reservoirs.get_device_address();
	desc.light_path_reservoirs_addr = light_path_reservoirs.get_device_address();
	desc.temporal_reservoir_addr = temporal_gi_reservoir_buffer.get_device_address();
	desc.restir_samples_addr = restir_gi_samples_buffer.get_device_address();

	scene_desc_buffer.create(
		&instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE, sizeof(SceneDesc), &desc, true);
	pc_ray.frame_num = 1;
	pc_ray.total_frame_num = 0;
	pc_ray.world_radius = lumen_scene->m_dimensions.radius;
	pc_ray.size_x = instance->width;
	pc_ray.size_y = instance->height;

	assert(instance->vkb.rg->settings.shader_inference == true);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_state_addr, &light_state_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_vertices_addr, &light_vertices_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, color_storage_addr, &color_storage_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, path_cnt_addr, &light_path_cnt_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, temporal_light_origin_reservoirs_addr,
									&temporal_light_origin_reservoirs,
								 instance->vkb.rg);	
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_transfer_addr, &light_transfer_buffer, instance->vkb.rg);
	/* REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, spatial_light_origin_reservoirs_addr,
								 &spatial_light_origin_reservoirs, instance->vkb.rg);*/
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_vertices_reservoirs_addr, &light_vertices_reservoirs,
								 instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, light_path_reservoirs_addr, &light_path_reservoirs,
								 instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, temporal_reservoir_addr, &temporal_gi_reservoir_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, restir_samples_addr, &restir_gi_samples_buffer,
								 instance->vkb.rg);
}

void SBDPTResampled::render() {
	CommandBuffer cmd(&instance->vkb.ctx, /*start*/ true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	//pc_ray.light_pos = scene_ubo.light_pos;
	//pc_ray.light_type = 0;
	//pc_ray.light_intensity = 10;
	pc_ray.num_lights = (int)lights.size();
	pc_ray.time = rand() % UINT_MAX;
	pc_ray.max_depth = lumen_scene->config.path_length;
	pc_ray.sky_col = lumen_scene->config.sky_col;
	pc_ray.total_light_area = total_light_area;
	// TODO fix triangle light and total light distinction
	pc_ray.light_triangle_count = total_light_triangle_cnt;
	//pc_ray.use_vc = true;  // use_vc;
	//pc_ray.num_textures = lumen_scene->textures.size()+1;
	pc_ray.do_spatiotemporal = do_spatiotemporal;
	pc_ray.random_num = rand() % UINT_MAX;

#define TEMP_RESAMPLING
#define SPATIAL_RESAMPLING
#define LIGHT_PATH_RESAMPLING
#define GI_RESAMPLING
#ifdef TEMP_RESAMPLING
	instance->vkb.rg
		->add_rt("SBDPT - Init",
				 {

					 .shaders = {{"src/shaders/integrators/sbdpt/sbdpt_light_first.rgen"},
								 {"src/shaders/ray.rmiss"},
								 {"src/shaders/ray_shadow.rmiss"},
								 {"src/shaders/ray.rchit"},
								 {"src/shaders/ray.rahit"}},
					 .dims = {instance->width, instance->height},
					 .accel = instance->vkb.tlas.accel})

		.push_constants(&pc_ray)
		//.zero(temporal_light_origin_reservoirs, !do_spatiotemporal)
		//.zero(light_vertices_buffer)
		.bind(std::initializer_list<ResourceBinding>{
			output_tex,
			prim_lookup_buffer,
			scene_ubo_buffer,
			scene_desc_buffer,
		})
		.bind(mesh_lights_buffer)
		.bind_texture_array(scene_textures)
		.bind_tlas(instance->vkb.tlas);
#endif
#ifdef SPATIAL_RESAMPLING
	  instance->vkb.rg
		->add_rt("SBDPT - Spatial",
				 {

					 .shaders = {{"src/shaders/integrators/sbdpt/sbdpt_light_spatial.rgen"},
								 {"src/shaders/ray.rmiss"},
								 {"src/shaders/ray_shadow.rmiss"},
								 {"src/shaders/ray.rchit"},
								 {"src/shaders/ray.rahit"}},
					 .dims = {instance->width, instance->height},
					 .accel = instance->vkb.tlas.accel})

		.push_constants(&pc_ray)
		//.zero(temporal_light_origin_reservoirs, !do_spatiotemporal)
		//.zero(light_vertices_buffer)
		.bind(std::initializer_list<ResourceBinding>{
			output_tex,
			prim_lookup_buffer,
			scene_ubo_buffer,
			scene_desc_buffer,
		})
		.bind(mesh_lights_buffer)
		.bind_texture_array(scene_textures)
		.bind_tlas(instance->vkb.tlas);
#endif
	  instance->vkb.rg
		->add_rt("SBDPT - Light Vertices",
				 {

					 .shaders = {{"src/shaders/integrators/sbdpt/sbdpt_light.rgen"},
								 {"src/shaders/ray.rmiss"},
								 {"src/shaders/ray_shadow.rmiss"},
								 {"src/shaders/ray.rchit"},
								 {"src/shaders/ray.rahit"}},
					 .dims = {instance->width, instance->height},
					 .accel = instance->vkb.tlas.accel})
		
		.push_constants(&pc_ray)
		//.zero(temporal_light_origin_reservoirs, !do_spatiotemporal)
		//.zero(light_vertices_buffer)
		.bind(std::initializer_list<ResourceBinding>{
			output_tex,
			prim_lookup_buffer,
			scene_ubo_buffer,
			scene_desc_buffer,
		})
		.bind(mesh_lights_buffer)
		.bind_texture_array(scene_textures)
		.bind_tlas(instance->vkb.tlas);

	   instance->vkb.rg
		   ->add_rt("SBDPT - Trace Eye", {.shaders = {{"src/shaders/integrators/sbdpt/sbdpt_eye.rgen"},
													  {"src/shaders/ray.rmiss"},
													  {"src/shaders/ray_shadow.rmiss"},
													  {"src/shaders/ray.rchit"},
													  {"src/shaders/ray.rahit"}},
										  .dims = {instance->width, instance->height},
										  .accel = instance->vkb.tlas.accel})
		   .push_constants(&pc_ray)
		   .bind(std::initializer_list<ResourceBinding>{
			   output_tex,
			   prim_lookup_buffer,
			   scene_ubo_buffer,
			   scene_desc_buffer,
		   })
		   //.bind_texture_array(diffuse_textures)
		   .bind(mesh_lights_buffer)
		   .bind_texture_array(scene_textures)
		   .bind_tlas(instance->vkb.tlas);

#ifdef LIGHT_PATH_RESAMPLING
	  instance->vkb.rg
		  ->add_rt("SBDPT - Light Path Resampling",
				   {

					   .shaders = {{"src/shaders/integrators/sbdpt/sbdpt_light_path_resample.rgen"},
								   {"src/shaders/ray.rmiss"},
								   {"src/shaders/ray_shadow.rmiss"},
								   {"src/shaders/ray.rchit"},
								   {"src/shaders/ray.rahit"}},
					   .dims = {instance->width, instance->height},
					   .accel = instance->vkb.tlas.accel})

		  .push_constants(&pc_ray)
		  //.zero(temporal_light_origin_reservoirs, !do_spatiotemporal)
		  //.zero(light_vertices_buffer)
		  .bind(std::initializer_list<ResourceBinding>{
			  output_tex,
			  prim_lookup_buffer,
			  scene_ubo_buffer,
			  scene_desc_buffer,
		  })
		  .bind(mesh_lights_buffer)
		  .bind_texture_array(scene_textures)
		  .bind_tlas(instance->vkb.tlas);
#endif

#ifdef GI_RESAMPLING
	  instance->vkb.rg
		  ->add_rt("SBDPT - GI Resampling", {.shaders = {{"src/shaders/integrators/sbdpt/sbdpt_gi_temporal_reuse.rgen"},
													 {"src/shaders/ray.rmiss"},
													 {"src/shaders/ray_shadow.rmiss"},
													 {"src/shaders/ray.rchit"},
													 {"src/shaders/ray.rahit"}},
										 .dims = {instance->width, instance->height},
										 .accel = instance->vkb.tlas.accel})
		  .push_constants(&pc_ray)
		  .bind(std::initializer_list<ResourceBinding>{
			  output_tex,
			  prim_lookup_buffer,
			  scene_ubo_buffer,
			  scene_desc_buffer,
		  })
		  //.bind_texture_array(diffuse_textures)
		  .bind(mesh_lights_buffer)
		  .bind_texture_array(scene_textures)
		  .bind_tlas(instance->vkb.tlas);
		instance->vkb.rg
		  ->add_rt("SBDPT - GI Spatial Resampling", {.shaders = {{"src/shaders/integrators/sbdpt/sbdpt_gi_spatial_reuse.rgen"},
														 {"src/shaders/ray.rmiss"},
														 {"src/shaders/ray_shadow.rmiss"},
														 {"src/shaders/ray.rchit"},
														 {"src/shaders/ray.rahit"}},
											 .dims = {instance->width, instance->height},
											 .accel = instance->vkb.tlas.accel})
		  .push_constants(&pc_ray)
		  .bind(std::initializer_list<ResourceBinding>{
			  output_tex,
			  prim_lookup_buffer,
			  scene_ubo_buffer,
			  scene_desc_buffer,
		  })
		  //.bind_texture_array(diffuse_textures)
		  .bind(mesh_lights_buffer)
		  .bind_texture_array(scene_textures)
		  .bind_tlas(instance->vkb.tlas);

#endif


	if (!do_spatiotemporal) {
		do_spatiotemporal = true;
	}
	pc_ray.total_frame_num++;

	instance->vkb.rg->run_and_submit(cmd);
}

bool SBDPTResampled::update() { 
	pc_ray.frame_num++;
	bool updated = Integrator::update();
	
		
	if (updated) {
		pc_ray.frame_num = 1;
	}
	
	return updated;
}

void SBDPTResampled::destroy() {
	const auto device = instance->vkb.ctx.device;
	Integrator::destroy();
	std::vector<Buffer*> buffer_list = {&light_path_cnt_buffer, 
										&color_storage_buffer,
										&light_state_buffer,
										&light_vertices_buffer,
										&temporal_light_origin_reservoirs,
										&light_transfer_buffer,
										&spatial_light_origin_reservoirs,
										&light_vertices_reservoirs,
										&light_path_reservoirs,
										&temporal_gi_reservoir_buffer,
										&restir_gi_samples_buffer};
	for (auto b : buffer_list) {
		b->destroy();
	}
	vkDestroyDescriptorSetLayout(device, desc_set_layout, nullptr);
	vkDestroyDescriptorPool(device, desc_pool, nullptr);
}