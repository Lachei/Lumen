#pragma once
#include "Integrator.h"
#include "SceneConfig.h"

struct ReSTIRConfig : SceneConfig {
	ReSTIRConfig() : SceneConfig("ReSTIR") {}
};

class ReSTIR : public Integrator {
   public:
	ReSTIR(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->config), integrator_config(lumen_scene->integrator_config) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	Buffer g_buffer;
	Buffer passthrough_reservoir_buffer;
	Buffer temporal_reservoir_buffer;
	Buffer spatial_reservoir_buffer;
	Buffer tmp_col_buffer;
	PCReSTIR pc_ray{};

	bool do_spatiotemporal = false;
	SceneConfig& config;
	nlohmann::json integrator_config;
};
