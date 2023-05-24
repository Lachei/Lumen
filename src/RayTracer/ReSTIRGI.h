#pragma once
#include "Integrator.h"
#include "SceneConfig.h"

struct ReSTIRGIConfig : SceneConfig {
	ReSTIRGIConfig() : SceneConfig("ReSTIR GI") {}
};


class ReSTIRGI : public Integrator {
   public:
	ReSTIRGI(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->config), integrator_config(lumen_scene->integrator_config) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual bool gui() override;
	virtual void destroy() override;

   private:
	Buffer restir_samples_buffer;
	Buffer restir_samples_old_buffer;
	Buffer temporal_reservoir_buffer;
	Buffer spatial_reservoir_buffer;
	Buffer tmp_col_buffer;
	PCReSTIRGI pc_ray{};
	bool do_spatiotemporal = false;
	bool enable_accumulation = false;

	SceneConfig& config;
	nlohmann::json integrator_config;
};
