#pragma once
#include "../Integrator.h"
class SBDPTResampled : public Integrator {
   public:
	SBDPTResampled(LumenInstance* scene, LumenScene* lumen_scene) : Integrator(scene, lumen_scene) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	PCReSTIRGI pc_ray{};
	VkDescriptorPool desc_pool;
	VkDescriptorSetLayout desc_set_layout;
	VkDescriptorSet desc_set;
	Buffer light_state_buffer;
	Buffer light_vertices_buffer;
	Buffer light_path_cnt_buffer;
	Buffer temporal_light_origin_reservoirs;
	Buffer color_storage_buffer;
	Buffer light_transfer_buffer;
	Buffer spatial_light_origin_reservoirs;
	Buffer light_vertices_reservoirs;
	Buffer light_path_reservoirs;
	Buffer temporal_gi_reservoir_buffer;
	Buffer restir_gi_samples_buffer;

	bool do_spatiotemporal = false;
};