#pragma once
#include "Integrator.h"
#include "SceneConfig.h"

struct PathConfig : SceneConfig {};

class Path : public Integrator {
   public:
	Path(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->config), integrator_config(lumen_scene->integrator_config) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	PCPath pc_ray{};
	SceneConfig& config;
	nlohmann::json integrator_config;
};
