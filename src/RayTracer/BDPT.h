#pragma once
#include "Integrator.h"
#include "SceneConfig.h"

struct BDPTConfig : SceneConfig {
	BDPTConfig() : SceneConfig("BDPT") {}
};

class BDPT : public Integrator {
   public:
	BDPT(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->config), integrator_config(lumen_scene->integrator_config) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	PCBDPT pc_ray{};
	Buffer light_path_buffer;
	Buffer camera_path_buffer;
	Buffer color_storage_buffer;
	SceneConfig& config;
	nlohmann::json integrator_config;
};
