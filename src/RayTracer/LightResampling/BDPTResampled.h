#pragma once
#include "../Integrator.h"

struct BDPTResampledConfig : SceneConfig {
	BDPTResampledConfig() : SceneConfig("BDPTResampled") {}
};

class BDPTResampled : public Integrator {
   public:
	BDPTResampled(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->config),  integrator_config(lumen_scene->integrator_config){}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	PCBDPT pc_ray{};
	Buffer light_path_buffer;
	Buffer camera_path_buffer;
	Buffer color_storage_buffer;
	Buffer global_light_reservoir_buffer;
	Buffer global_light_spatial_reservoir_buffer;
	SceneConfig& config;
	nlohmann::json integrator_config;
};
