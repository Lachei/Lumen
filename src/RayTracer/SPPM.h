#pragma once
#include "Integrator.h"
#include "SceneConfig.h"

struct SPPMConfig : SceneConfig {
	float base_radius = 0.03f;
	SPPMConfig() : SceneConfig("SPPM") {}
};

class SPPM : public Integrator {
   public:
	SPPM(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->config), integrator_config(lumen_scene->integrator_config) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	PCSPPM pc_ray{};
	VkDescriptorPool desc_pool{};
	VkDescriptorSetLayout desc_set_layout{};
	VkDescriptorSet desc_set;

	Buffer sppm_data_buffer;
	Buffer atomic_data_buffer;
	Buffer photon_buffer;
	Buffer residual_buffer;
	Buffer counter_buffer;
	Buffer hash_buffer;
	Buffer tmp_col_buffer;

	SceneConfig& config;
	nlohmann::json integrator_config;
};
