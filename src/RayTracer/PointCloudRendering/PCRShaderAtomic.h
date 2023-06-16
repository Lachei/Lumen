#pragma once
#include "../Integrator.h"
#include "../../shaders/pcr/pcr_commons.h"

class PCRShaderAtomic: public Integrator {
	public:
    PCRShaderAtomic(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->integrator_config) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
    uint64_t		point_count;
	Buffer			point_positions{};
	Buffer			point_colors{};
	Buffer			image_depth_buffer{};
	Buffer 			constant_infos_buffer{};
	PC 				pc{};
	nlohmann::json 	config;
};
